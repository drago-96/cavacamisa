// cvc_gpu.cu - exhaustive enumeration on an NVIDIA GPU.  Same arguments and
// same output files as cvc_enum, so orchestrate.py can drive either.
//
//   make cvc_gpu [ARCH=sm_90]
//   ./cvc_gpu --out=DIR --start=R --count=N [--rule=empty|must] [--stop-lower]
//             [--step-limit=L] [--batch=MASKS] [--device=D] [--tpm=T] [--block=B]
//   ./cvc_gpu --out=DIR --resume [--device=D]
//
// One launch covers --batch masks.  Thread t plays mask t / tpm, value
// patterns of slice t % tpm (34650 / tpm patterns), with the flat event loop
// of cvc_flat.h.  Histograms live in shared memory and are flushed once per
// block; the rare games over the step limit or the histogram range come back
// as records and are re-played exactly on the host.
//
// Compiled by a plain C++ compiler (make cvc_gpu_cpu) the kernel body runs on
// CPU threads instead: same code path, so it can be checked bit-for-bit
// against cvc_enum without a GPU.

#include <sys/stat.h>
#include <time.h>
#ifdef __CUDACC__
#include <cuda_runtime.h>
#endif
#include "cvc_flat.h"
#include "cvc_enum_common.h"

struct Params {
  uint64_t rank0;     // first mask rank of the launch
  uint32_t nmasks;    // masks in the launch
  uint32_t tpm, ppt;  // threads per mask, patterns per thread
  int rule, flags, step_limit;
  uint32_t max_rec;
};

struct DevOut {  // results of one launch
  unsigned long long counts[3];  // A, B, stopped (games not in records)
  unsigned long long steps[CVC_HIST_STEPS];
  unsigned long long caps[CVC_HIST_CAPS];
  unsigned nrec;
};

// Work of thread t of a launch.
CVC_FN void thread_work(const Params &P, const uint64_t (*binom)[13], const uint32_t *pats, uint64_t t,
                        cvc_flat_counts *cnt, cvc_flat_out out) {
  const uint64_t mi = t / P.tpm;
  if (mi >= P.nmasks) return;
  const int part = (int)(t % P.tpm);
  const int p0 = part * (int)P.ppt;
  const int p1 = part == (int)P.tpm - 1 ? CVC_NPAT : p0 + (int)P.ppt;
  cvc_flat_run(cvc_unrank(binom, P.rank0 + mi), (uint32_t)mi, pats, p0, p1, P.rule, P.flags, P.step_limit,
               cnt, out);
}

#ifdef __CUDACC__
// ------------------------------------------------------------------ CUDA

__constant__ uint64_t c_binom[41][13];

__global__ void cvc_kernel(Params P, const uint32_t *__restrict__ pats, DevOut *g, cvc_dev_rec *recs) {
  __shared__ unsigned s_steps[CVC_HIST_STEPS];
  __shared__ unsigned s_caps[CVC_HIST_CAPS];
  __shared__ unsigned s_cnt[3];
  for (int i = threadIdx.x; i < CVC_HIST_STEPS; i += blockDim.x) s_steps[i] = 0;
  for (int i = threadIdx.x; i < CVC_HIST_CAPS; i += blockDim.x) s_caps[i] = 0;
  if (threadIdx.x < 3) s_cnt[threadIdx.x] = 0;
  __syncthreads();

  cvc_flat_counts cnt = {0, 0, 0};
  cvc_flat_out out = {s_steps, s_caps, recs, &g->nrec, P.max_rec};
  thread_work(P, c_binom, pats, (uint64_t)blockIdx.x * blockDim.x + threadIdx.x, &cnt, out);
  atomicAdd(&s_cnt[0], cnt.a);
  atomicAdd(&s_cnt[1], cnt.b);
  atomicAdd(&s_cnt[2], cnt.stopped);
  __syncthreads();

  for (int i = threadIdx.x; i < CVC_HIST_STEPS; i += blockDim.x)
    if (s_steps[i]) atomicAdd(&g->steps[i], (unsigned long long)s_steps[i]);
  for (int i = threadIdx.x; i < CVC_HIST_CAPS; i += blockDim.x)
    if (s_caps[i]) atomicAdd(&g->caps[i], (unsigned long long)s_caps[i]);
  if (threadIdx.x < 3) atomicAdd(&g->counts[threadIdx.x], (unsigned long long)s_cnt[threadIdx.x]);
}

static void check(cudaError_t e, const char *what) {
  if (e != cudaSuccess) {
    fprintf(stderr, "CUDA error in %s: %s\n", what, cudaGetErrorString(e));
    exit(1);
  }
}

struct Backend {
  const uint32_t *d_pats;
  DevOut *d_out;
  cvc_dev_rec *d_recs;
  cudaEvent_t e0, e1;

  void init(int device, const uint32_t *pats, uint32_t max_rec) {
    check(cudaSetDevice(device), "cudaSetDevice");
    cudaDeviceProp prop;
    check(cudaGetDeviceProperties(&prop, device), "cudaGetDeviceProperties");
    fprintf(stderr, "device %d: %s, %d SMs\n", device, prop.name, prop.multiProcessorCount);
    check(cudaMemcpyToSymbol(c_binom, cvc_binom, sizeof cvc_binom), "copy binomials");
    uint32_t *p;
    check(cudaMalloc(&p, CVC_NPAT * sizeof(uint32_t)), "cudaMalloc patterns");
    check(cudaMemcpy(p, pats, CVC_NPAT * sizeof(uint32_t), cudaMemcpyHostToDevice), "copy patterns");
    d_pats = p;
    check(cudaMalloc(&d_out, sizeof(DevOut)), "cudaMalloc out");
    check(cudaMalloc(&d_recs, max_rec * sizeof(cvc_dev_rec)), "cudaMalloc records");
    check(cudaEventCreate(&e0), "event");
    check(cudaEventCreate(&e1), "event");
  }

  // Runs one launch; fills *out and recs; returns kernel seconds.
  double run(const Params &P, int block, DevOut *out, cvc_dev_rec *recs) {
    check(cudaMemset(d_out, 0, sizeof(DevOut)), "cudaMemset");
    const uint64_t threads = (uint64_t)P.nmasks * P.tpm;
    const unsigned blocks = (unsigned)((threads + block - 1) / block);
    check(cudaEventRecord(e0), "event");
    cvc_kernel<<<blocks, block>>>(P, d_pats, d_out, d_recs);
    check(cudaGetLastError(), "kernel launch");
    check(cudaEventRecord(e1), "event");
    check(cudaEventSynchronize(e1), "kernel");
    float ms = 0;
    check(cudaEventElapsedTime(&ms, e0, e1), "event time");
    check(cudaMemcpy(out, d_out, sizeof(DevOut), cudaMemcpyDeviceToHost), "copy out");
    const unsigned n = out->nrec < P.max_rec ? out->nrec : P.max_rec;
    if (n) check(cudaMemcpy(recs, d_recs, n * sizeof(cvc_dev_rec), cudaMemcpyDeviceToHost), "copy records");
    return ms / 1e3;
  }
};

#else
// ------------------------------------------------- CPU emulation (testing)
#include <pthread.h>

struct Backend;
struct EmuJob {
  const Params *P;
  const uint32_t *pats;
  DevOut *g;
  cvc_dev_rec *recs;
  uint64_t nblocks, next;
  int block;
  pthread_mutex_t lock;
};

static void *emu_worker(void *arg) {
  EmuJob *j = (EmuJob *)arg;
  unsigned *s_steps = (unsigned *)malloc(CVC_HIST_STEPS * sizeof(unsigned));
  unsigned *s_caps = (unsigned *)malloc(CVC_HIST_CAPS * sizeof(unsigned));
  for (;;) {  // one CUDA block at a time, its threads one after the other
    uint64_t b = __atomic_fetch_add(&j->next, 1, __ATOMIC_RELAXED);
    if (b >= j->nblocks) break;
    memset(s_steps, 0, CVC_HIST_STEPS * sizeof(unsigned));
    memset(s_caps, 0, CVC_HIST_CAPS * sizeof(unsigned));
    cvc_flat_counts cnt = {0, 0, 0};
    cvc_flat_out out = {s_steps, s_caps, j->recs, &j->g->nrec, j->P->max_rec};
    for (int t = 0; t < j->block; t++)
      thread_work(*j->P, (const uint64_t(*)[13])cvc_binom, j->pats, b * j->block + t, &cnt, out);
    pthread_mutex_lock(&j->lock);
    for (int i = 0; i < CVC_HIST_STEPS; i++) j->g->steps[i] += s_steps[i];
    for (int i = 0; i < CVC_HIST_CAPS; i++) j->g->caps[i] += s_caps[i];
    j->g->counts[0] += cnt.a;
    j->g->counts[1] += cnt.b;
    j->g->counts[2] += cnt.stopped;
    pthread_mutex_unlock(&j->lock);
  }
  free(s_steps);
  free(s_caps);
  return NULL;
}

struct Backend {
  const uint32_t *pats;
  cvc_dev_rec *recs;
  int threads;

  void init(int, const uint32_t *p, uint32_t max_rec) {
    pats = p;
    recs = (cvc_dev_rec *)malloc(max_rec * sizeof(cvc_dev_rec));
    fprintf(stderr, "CPU emulation of the GPU kernel, %d threads\n", threads);
  }

  double run(const Params &P, int block, DevOut *out, cvc_dev_rec *out_recs) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    memset(out, 0, sizeof *out);
    EmuJob j;
    j.P = &P; j.pats = pats; j.g = out; j.recs = recs; j.block = block; j.next = 0;
    j.nblocks = ((uint64_t)P.nmasks * P.tpm + block - 1) / block;
    pthread_mutex_init(&j.lock, NULL);
    pthread_t th[256];
    for (int i = 0; i < threads; i++) pthread_create(&th[i], NULL, emu_worker, &j);
    for (int i = 0; i < threads; i++) pthread_join(th[i], NULL);
    const unsigned n = out->nrec < P.max_rec ? out->nrec : P.max_rec;
    memcpy(out_recs, recs, n * sizeof(cvc_dev_rec));
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
  }
};
#endif

static uint32_t patterns[CVC_NPAT];

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

int main(int argc, char **argv) {
  const char *out = NULL;
  uint64_t start = 0, count = 0, batch = 131072;
  int resume = 0, device = 0, tpm = 30, block = 256, threads = 8;
  const uint32_t max_rec = 1u << 20;
  cvc_totals t;
  cvc_totals_init(&t);
  t.step_limit = 20000;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strncmp(a, "--out=", 6)) out = a + 6;
    else if (!strncmp(a, "--start=", 8)) start = strtoull(a + 8, NULL, 10);
    else if (!strncmp(a, "--count=", 8)) count = strtoull(a + 8, NULL, 10);
    else if (!strncmp(a, "--batch=", 8)) batch = strtoull(a + 8, NULL, 10);
    else if (!strncmp(a, "--step-limit=", 13)) t.step_limit = atoi(a + 13);
    else if (!strncmp(a, "--device=", 9)) device = atoi(a + 9);
    else if (!strncmp(a, "--tpm=", 6)) tpm = atoi(a + 6);
    else if (!strncmp(a, "--block=", 8)) block = atoi(a + 8);
    else if (!strncmp(a, "--threads=", 10)) threads = atoi(a + 10);  // CPU emulation only
    else if (!strcmp(a, "--rule=empty")) t.rule = CVC_RULE_EMPTY;
    else if (!strcmp(a, "--rule=must")) t.rule = CVC_RULE_MUST;
    else if (!strcmp(a, "--stop-lower")) t.flags |= CVC_STOP_LOWER;
    else if (!strcmp(a, "--resume")) resume = 1;
    else { fprintf(stderr, "unknown argument %s\n", a); return 1; }
  }
  if (!out) { fprintf(stderr, "--out=DIR is required\n"); return 1; }
  if (tpm < 1 || tpm > CVC_NPAT || block < 32 || block > 1024 || batch < 1 || batch > (1u << 26) ||
      threads < 1 || threads > 256) {
    fprintf(stderr, "bad --tpm/--block/--batch/--threads\n");
    return 1;
  }
  cvc_init_binom();
  if (cvc_make_patterns(patterns) != CVC_NPAT) { fprintf(stderr, "pattern bug\n"); return 1; }
  cvc_mkdirs(out);

  char path[1024];
  snprintf(path, sizeof path, "%s/records.txt", out);
  if (resume) {
    if (!cvc_load(out, &t)) { fprintf(stderr, "nothing to resume in %s\n", out); return 1; }
    cvc_truncate_records(path, t.next_rank * CVC_NPAT);
  } else {
    if (count == 0 || start + count > CVC_NMASKS) { fprintf(stderr, "bad --start/--count\n"); return 1; }
    t.first_rank = t.next_rank = start;
    t.end_rank = start + count;
  }
  FILE *recf = fopen(path, resume ? "a" : "w");
  if (!recf) { perror(path); return 1; }

  Backend be;
#ifndef __CUDACC__
  be.threads = threads;
#endif
  be.init(device, patterns, max_rec);
  DevOut *dout = (DevOut *)malloc(sizeof(DevOut));
  cvc_dev_rec *drecs = (cvc_dev_rec *)malloc(max_rec * sizeof(cvc_dev_rec));
  cvc_batch *b = (cvc_batch *)calloc(1, sizeof(cvc_batch));

  const double t0 = now();
  double kernel_s = 0;
  uint64_t games_done = 0;
  while (t.next_rank < t.end_rank) {
    Params P;
    P.rank0 = t.next_rank;
    P.nmasks = (uint32_t)(t.end_rank - t.next_rank < batch ? t.end_rank - t.next_rank : batch);
    P.tpm = (uint32_t)tpm;
    P.ppt = (uint32_t)(CVC_NPAT / tpm);
    P.rule = t.rule; P.flags = t.flags; P.step_limit = t.step_limit;
    P.max_rec = max_rec;

    const double ks = be.run(P, block, dout, drecs);
    if (dout->nrec > max_rec) {
      fprintf(stderr, "\n%u records in one launch (max %u): use a smaller --batch\n", dout->nrec, max_rec);
      return 1;
    }

    // Convert to the same batch format cvc_enum produces, then merge.
    memset(b->count, 0, sizeof b->count);
    b->count[CVC_A_WINS] = dout->counts[0];
    b->count[CVC_B_WINS] = dout->counts[1];
    b->count[CVC_STOPPED] = dout->counts[2];
    uint64_t steps_sum = 0, finished = 0;
    for (int i = 0; i < CVC_HIST_STEPS; i++) {
      b->steps[i] = dout->steps[i];
      steps_sum += (uint64_t)i * dout->steps[i];
      finished += dout->steps[i];
    }
    for (int i = 0; i < CVC_HIST_CAPS; i++) b->caps[i] = dout->caps[i];
    b->nrec = 0;
    for (unsigned i = 0; i < dout->nrec; i++) {
      const cvc_dev_rec *d = &drecs[i];
      cvc_rec x = {(P.rank0 + d->mask_off) * CVC_NPAT + d->pat, d->m, d->v, d->outcome, d->steps, d->captures};
      cvc_batch_add_rec(b, x);
    }
    cvc_merge(b, &t, recf);
    t.next_rank += P.nmasks;
    cvc_save(out, &t);

    const uint64_t games = (uint64_t)P.nmasks * CVC_NPAT;
    games_done += games;
    kernel_s += ks;
    fprintf(stderr, "masks %llu/%llu  launch: %.2f s kernel, %.1f M games/s, avg steps %.1f  |  "
            "overall %.1f M games/s  loops %llu\n",
            (unsigned long long)(t.next_rank - t.first_rank), (unsigned long long)(t.end_rank - t.first_rank),
            ks, games / ks / 1e6, finished ? (double)steps_sum / (double)finished : 0.0,
            games_done / (now() - t0) / 1e6, (unsigned long long)t.count[CVC_LOOP]);
  }
  if (games_done)
    fprintf(stderr, "done: %llu games, %.1f s, %.1f M games/s end-to-end, %.1f M games/s kernel only\n",
            (unsigned long long)games_done, now() - t0, games_done / (now() - t0) / 1e6,
            games_done / kernel_s / 1e6);
  fclose(recf);
  return 0;
}
