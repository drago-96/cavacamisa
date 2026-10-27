// cvc_flat.h - plays a whole range of deals in one flat event loop, for GPUs.
//
// cvc_run() plays one game; on a GPU, a warp running one game per thread
// waits for its longest game every time.  Here one loop iteration plays one
// court card (or one payment) of the current game, and a thread moves on to
// its next deal as soon as a game ends, so all 32 threads stay busy.
//
// Same semantics as cvc_run() without CVC_EXACT (step limit instead of exact
// loop detection; the host re-plays limit hits exactly).  It is plain C, so
// cvc_gpu.cu can also run it on CPU threads (make cvc_gpu_cpu) and the result
// is checked against cvc_enum.

#ifndef CVC_FLAT_H
#define CVC_FLAT_H

#include "cvc_engine.h"

#define CVC_NPAT 34650
#define CVC_HIST_STEPS 4096    // histogram bins per batch; larger go to records
#define CVC_HIST_CAPS 1024

#ifdef __CUDA_ARCH__
#define CVC_FETCH_ADD(p, x) atomicAdd((p), (x))
#else
#define CVC_FETCH_ADD(p, x) __atomic_fetch_add((p), (x), __ATOMIC_RELAXED)
#endif

// A game that does not fit the histograms: step-limit hit or overflow.
typedef struct {
  uint64_t m;         // court-card mask of the deal
  uint32_t v;         // court-card values
  uint32_t pat;       // pattern index (0..34649)
  uint32_t mask_off;  // mask rank - first mask rank of the batch
  int outcome, steps, captures;
} cvc_dev_rec;

typedef struct {
  unsigned *steps;    // [CVC_HIST_STEPS], shared memory on the GPU
  unsigned *caps;     // [CVC_HIST_CAPS]
  cvc_dev_rec *recs;  // global
  unsigned *nrec;     // global counter; entries past max_rec are dropped
  unsigned max_rec;
} cvc_flat_out;

typedef struct {
  unsigned a, b, stopped;  // games not written as records
} cvc_flat_counts;

// Mask with 12 bits set and colex rank r; binom[n][k] = C(n, k).
CVC_FN uint64_t cvc_unrank(const uint64_t (*binom)[13], uint64_t r) {
  uint64_t m = 0;
  int c = 39;
  for (int k = 12; k >= 1; k--) {
    while (binom[c][k] > r) c--;
    m |= 1ULL << c;
    r -= binom[c][k];
    c--;
  }
  return m;
}

// Plays the deals (M, pats[p]) for p in [p0, p1).
CVC_FN void cvc_flat_run(uint64_t M, uint32_t mask_off, const uint32_t *pats, int p0, int p1,
                         int rule, int flags, int step_limit,
                         cvc_flat_counts *cnt, cvc_flat_out out) {
  const int off = (rule == CVC_RULE_EMPTY) ? 1 : 0;
  const uint64_t mA = M & 0xFFFFFULL, mB = M >> 20;
  const int kA = CVC_POP64(mA);
  const uint32_t vmaskA = (1u << (2 * kA)) - 1u;

  uint64_t mY = 0, mZ = 0, pm = 0;
  uint32_t vY = 0, vZ = 0, pv = 0, V = 0;
  int nY = 0, nZ = 0, kY = 0, kZ = 0, pn = 0, pk = 0, pen = 0, steps = 0, caps = 0, lab = 0;
  int p = p0 - 1;
  int outcome = 0;  // "a game just ended": the first iteration starts game p0

  for (;;) {
    if (outcome >= 0) {
      if (p >= p0) {
        const int finished = outcome <= CVC_B_WINS;
        if (outcome == CVC_LIMIT || (finished && (steps >= CVC_HIST_STEPS || caps >= CVC_HIST_CAPS))) {
          unsigned i = CVC_FETCH_ADD(out.nrec, 1u);
          if (i < out.max_rec) {
            cvc_dev_rec r = {M, V, (uint32_t)p, mask_off, outcome, steps, caps};
            out.recs[i] = r;
          }
        } else {
          cnt->a += outcome == CVC_A_WINS;
          cnt->b += outcome == CVC_B_WINS;
          cnt->stopped += outcome == CVC_STOPPED;
          if (finished) {
            CVC_FETCH_ADD(&out.steps[steps], 1u);
            CVC_FETCH_ADD(&out.caps[caps], 1u);
          }
        }
      }
      if (++p >= p1) break;
      V = pats[p];
      mY = mA; vY = V & vmaskA; nY = 20; kY = kA;
      mZ = mB; vZ = V >> (2 * kA); nZ = 20; kZ = 12 - kA;
      pm = 0; pv = 0; pn = 0; pk = 0; pen = 0; steps = 0; caps = 0; lab = 0;
      outcome = -1;
    }

    // Y is the player to act, Z the other one.  e = own card index of the
    // next court card, or of the card where the player loses.
    const int lastY = nY - off, lastZ = nZ - off;
    const int eY = CVC_CTZ64(mY | (1ULL << lastY));
    const int eZ = CVC_CTZ64(mZ | (1ULL << lastZ));
    const int fr = pen == 0;                // free play (else Y pays pen cards)
    const int zev = fr && eZ < eY;          // Z plays a court card first
    const int cap = !fr && eY >= pen;       // Y pays in full, Z collects
    const int yev = !zev && !cap;           // Y plays a court card
    const int cY = cap ? pen : (zev ? eZ + 1 : eY + 1);
    const int cZ = fr ? (zev ? eZ + 1 : eY) : 0;

    if ((yev && eY == lastY) || (zev && eZ == lastZ)) {
      // Somebody runs out (in 'must' the losing player plays one card less).
      const int loser = zev ? lab ^ 1 : lab;
      outcome = loser ? CVC_A_WINS : CVC_B_WINS;
      caps += off && !fr && yev && !((mY >> eY) & 1) && nY == pen;
      steps += cY + cZ - (1 - off);
      continue;
    }

    const uint32_t c = (zev ? vZ : vY) & 3;
    mY >>= cY; nY -= cY;
    mZ >>= cZ; nZ -= cZ;
    if (yev) { vY >>= 2; kY--; }
    if (zev) { vZ >>= 2; kZ--; }
    pn += cY + cZ;
    steps += cY + cZ;
    if (cap) {
      mZ |= pm << nZ; vZ |= pv << (2 * kZ);
      nZ += pn; kZ += pk;
      pm = 0; pv = 0; pn = 0; pk = 0;
      caps++;
      pen = 0;
      // Trick boundary, Z leads.
      if ((flags & CVC_STOP_LOWER) && nZ == 20 &&
          cvc_deal_less(mZ | (mY << 20), vZ | (vY << (2 * kZ)), M, V)) {
        outcome = CVC_STOPPED;
        continue;
      }
      if (steps > step_limit) {
        outcome = CVC_LIMIT;
        continue;
      }
    } else {
      pm |= 1ULL << (pn - 1);
      pv |= c << (2 * pk);
      pk++;
      pen = (int)c;
    }
    if (!zev) {  // the other player acts next
      uint64_t t64 = mY; mY = mZ; mZ = t64;
      uint32_t t32 = vY; vY = vZ; vZ = t32;
      int t = nY; nY = nZ; nZ = t;
      t = kY; kY = kZ; kZ = t;
      lab ^= 1;
    }
  }
}

#endif
