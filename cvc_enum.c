// cvc_enum.c - exhaustive enumeration of Cavacamisa deals on CPU threads.
//
//   ./cvc_enum --out=DIR --start=R --count=N [--threads=T] [--rule=empty|must]
//              [--stop-lower] [--step-limit=L] [--batch=B]
//   ./cvc_enum --out=DIR --resume          continue an interrupted run
//
// Plays all 34,650 value patterns for each court-card mask with rank in
// [R, R+N).  Games over the step limit are re-played with exact cycle
// detection, so the final counts do not depend on the limit.  A checkpoint is
// written after every batch of B masks; kill it any time and --resume.
//
// --stop-lower: stop a game as soon as it reaches (at a trick boundary) a
// deal that sorts lower; that deal is enumerated too and has the same fate.
// About 35% faster and still finds every cycle, but such games get no stats.

#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <time.h>
#include "cvc_engine.h"
#include "cvc_enum_common.h"

static uint32_t patterns[CVC_NPAT];

typedef struct {
  uint64_t rank0, nmasks;
  int rule, flags, step_limit;
  _Atomic uint64_t next;  // next mask offset to hand out
  pthread_mutex_t lock;
  cvc_batch out;          // merged under lock
} job_t;

#define CHUNK 16  // masks per work item

static void *worker(void *arg) {
  job_t *j = (job_t *)arg;
  cvc_batch *b = (cvc_batch *)calloc(1, sizeof *b);
  for (;;) {
    uint64_t off = atomic_fetch_add(&j->next, CHUNK);
    if (off >= j->nmasks) break;
    uint64_t end = off + CHUNK < j->nmasks ? off + CHUNK : j->nmasks;
    uint64_t m = cvc_unrank_mask(j->rank0 + off);
    for (uint64_t o = off; o < end; o++, m = cvc_next_mask(m)) {
      for (int p = 0; p < CVC_NPAT; p++) {
        cvc_result r = cvc_play_mv(m, patterns[p], j->rule, j->flags, j->step_limit);
        const int finished = r.outcome <= CVC_B_WINS;
        if (r.outcome == CVC_LOOP || r.outcome == CVC_LIMIT ||
            (finished && (r.steps >= CVC_HIST_STEPS || r.captures >= CVC_HIST_CAPS))) {
          cvc_rec x = {(j->rank0 + o) * CVC_NPAT + (uint64_t)p, m, patterns[p], r.outcome, r.steps, r.captures};
          cvc_batch_add_rec(b, x);  // counted in cvc_merge
        } else {
          b->count[r.outcome]++;
          if (finished) { b->steps[r.steps]++; b->caps[r.captures]++; }
        }
      }
    }
  }
  pthread_mutex_lock(&j->lock);
  for (int i = 0; i < 5; i++) j->out.count[i] += b->count[i];
  for (int i = 0; i < CVC_HIST_STEPS; i++) j->out.steps[i] += b->steps[i];
  for (int i = 0; i < CVC_HIST_CAPS; i++) j->out.caps[i] += b->caps[i];
  for (size_t i = 0; i < b->nrec; i++) cvc_batch_add_rec(&j->out, b->recs[i]);
  pthread_mutex_unlock(&j->lock);
  free(b->recs);
  free(b);
  return NULL;
}

int main(int argc, char **argv) {
  const char *out = NULL;
  uint64_t start = 0, count = 0, batch = 4096;
  int threads = 10, resume = 0;
  cvc_totals t;
  cvc_totals_init(&t);
  t.step_limit = 20000;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strncmp(a, "--out=", 6)) out = a + 6;
    else if (!strncmp(a, "--start=", 8)) start = strtoull(a + 8, NULL, 10);
    else if (!strncmp(a, "--count=", 8)) count = strtoull(a + 8, NULL, 10);
    else if (!strncmp(a, "--threads=", 10)) threads = atoi(a + 10);
    else if (!strncmp(a, "--batch=", 8)) batch = strtoull(a + 8, NULL, 10);
    else if (!strncmp(a, "--step-limit=", 13)) t.step_limit = atoi(a + 13);
    else if (!strcmp(a, "--rule=empty")) t.rule = CVC_RULE_EMPTY;
    else if (!strcmp(a, "--rule=must")) t.rule = CVC_RULE_MUST;
    else if (!strcmp(a, "--stop-lower")) t.flags |= CVC_STOP_LOWER;
    else if (!strcmp(a, "--resume")) resume = 1;
    else { fprintf(stderr, "unknown argument %s\n", a); return 1; }
  }
  if (!out) { fprintf(stderr, "--out=DIR is required\n"); return 1; }
  if (threads < 1 || threads > 256) { fprintf(stderr, "bad --threads\n"); return 1; }
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

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  const uint64_t done0 = t.next_rank;
  while (t.next_rank < t.end_rank) {
    job_t *j = (job_t *)calloc(1, sizeof *j);
    j->rank0 = t.next_rank;
    j->nmasks = t.end_rank - t.next_rank < batch ? t.end_rank - t.next_rank : batch;
    j->rule = t.rule; j->flags = t.flags; j->step_limit = t.step_limit;
    pthread_mutex_init(&j->lock, NULL);
    pthread_t th[256];
    for (int i = 0; i < threads; i++) pthread_create(&th[i], NULL, worker, j);
    for (int i = 0; i < threads; i++) pthread_join(th[i], NULL);
    cvc_merge(&j->out, &t, recf);
    t.next_rank += j->nmasks;
    cvc_save(out, &t);
    free(j->out.recs);
    free(j);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
    double rate = (double)(t.next_rank - done0) * CVC_NPAT / s;
    fprintf(stderr, "\rmasks %llu/%llu  %.1f M games/s  loops so far %llu   ",
            (unsigned long long)(t.next_rank - t.first_rank), (unsigned long long)(t.end_rank - t.first_rank),
            rate / 1e6, (unsigned long long)t.count[CVC_LOOP]);
  }
  fprintf(stderr, "\n");
  fclose(recf);
  return 0;
}
