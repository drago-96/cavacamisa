// cvc_engine.h - fast Cavacamisa game engine (header only; C99 or C++, and it
// also compiles as CUDA).  Only 64/32-bit integer ops and no arrays, so the
// whole game state fits in registers.
//
// A hand is (m, v, n, k):
//   m  bit i set iff card i (0 = top) is a 1/2/3
//   v  values of those court cards in order, 2 bits each (first one in bits 0-1)
//   n  number of cards, k = popcount(m)
// "Next court card" is a single count-trailing-zeros, so the engine jumps from
// court card to court card instead of playing plain cards one at a time.
//
// Semantics are identical to reference.py (cross-checked by cvc_check.c), for
// steps/captures and for the two end-of-game rules:
//   CVC_RULE_EMPTY  a player loses as soon as their hand becomes empty
//                   (behaviour of the legacy C++ code)
//   CVC_RULE_MUST   a player loses when they have to play and have no cards
//
// Infinite games: with CVC_EXACT the engine runs Brent's cycle detection on
// trick-boundary states, so loops are detected exactly.  Without it, games
// longer than `step_limit` return CVC_LIMIT and must be re-checked exactly
// (cheap: there are very few of them).

#ifndef CVC_ENGINE_H
#define CVC_ENGINE_H

#include <stdint.h>

#ifdef __CUDACC__
#define CVC_FN static __host__ __device__ __forceinline__
#else
#define CVC_FN static inline
#endif
#ifdef __CUDA_ARCH__  // device side of a CUDA compile
#define CVC_CTZ64(x) (__ffsll((long long)(x)) - 1)
#define CVC_POP64(x) __popcll((unsigned long long)(x))
#else
#define CVC_CTZ64(x) __builtin_ctzll(x)
#define CVC_POP64(x) __builtin_popcountll(x)
#endif

enum { CVC_RULE_EMPTY = 0, CVC_RULE_MUST = 1 };
enum { CVC_A_WINS = 0, CVC_B_WINS = 1, CVC_LOOP = 2, CVC_LIMIT = 3, CVC_STOPPED = 4 };

// flags for cvc_run
#define CVC_EXACT      1  // Brent cycle detection (otherwise: step limit only)
#define CVC_STOP_LOWER 2  // stop when reaching a deal that sorts before this one

typedef struct {
  int outcome;   // CVC_A_WINS, CVC_B_WINS, CVC_LOOP, CVC_LIMIT, CVC_STOPPED
  int steps;     // cards played (for LOOP/LIMIT/STOPPED: when the run stopped)
  int captures;  // piles collected
} cvc_result;

// Total order on deals used by CVC_STOP_LOWER: compare court-card masks, then values.
CVC_FN int cvc_deal_less(uint64_t m1, uint32_t v1, uint64_t m2, uint32_t v2) {
  return m1 < m2 || (m1 == m2 && v1 < v2);
}

// Plays a game from a trick-boundary state.  Y leads (label `lab`: 0 = A,
// 1 = B), Z follows; the pile is empty.  For a deal, Y = A's 20 cards.
CVC_FN cvc_result cvc_run(uint64_t mY, uint32_t vY, int nY,
                          uint64_t mZ, uint32_t vZ, int nZ,
                          int lab, int rule, int flags, int step_limit) {
  int kY = CVC_POP64(mY), kZ = CVC_POP64(mZ);
  // A player loses when it needs card index n ('must') or plays index n-1 ('empty').
  const int off = (rule == CVC_RULE_EMPTY) ? 1 : 0;
  const uint64_t m0 = mY | (mZ << 20);  // this deal, for CVC_STOP_LOWER
  const uint32_t v0 = vY | (vZ << (2 * kY));

  int pen = 0;                // 0: free play; else Y owes `pen` cards to Z
  uint64_t pm = 0;            // pile (same encoding as a hand)
  uint32_t pv = 0;
  int pn = 0, pk = 0;
  int steps = 0, caps = 0;

  // Brent: tortoise state (leader mask | follower mask, leader size, values).
  uint64_t tm = mY | (mZ << nY) | ((uint64_t)nY << 40);
  uint32_t tv = vY | (vZ << (2 * kY));
  int power = 1, lam = 0;

  cvc_result r;
  for (;;) {
    const int lastY = nY - off, lastZ = nZ - off;
    const int eY = CVC_CTZ64(mY | (1ULL << lastY));  // min(next court, last)
    int swap;

    if (pen == 0) {
      // Free play: Y's card i at time 2i, Z's at time 2i+1.
      const int eZ = CVC_CTZ64(mZ | (1ULL << lastZ));
      if (eY <= eZ) {
        if (eY == lastY) {  // Y runs out
          r.outcome = lab ? CVC_A_WINS : CVC_B_WINS;
          r.steps = steps + 2 * eY + off;
          r.captures = caps;
          return r;
        }
        const int c = vY & 3;
        vY >>= 2; kY--;
        mY >>= eY + 1; nY -= eY + 1;
        mZ >>= eY;     nZ -= eY;
        pn = 2 * eY + 1;
        pm = 1ULL << (pn - 1); pv = (uint32_t)c; pk = 1;
        steps += pn;
        pen = c;
        swap = 1;  // Z must pay
      } else {
        if (eZ == lastZ) {  // Z runs out
          r.outcome = lab ? CVC_B_WINS : CVC_A_WINS;
          r.steps = steps + 2 * eZ + 1 + off;
          r.captures = caps;
          return r;
        }
        const int c = vZ & 3;
        vZ >>= 2; kZ--;
        mY >>= eZ + 1; nY -= eZ + 1;
        mZ >>= eZ + 1; nZ -= eZ + 1;
        pn = 2 * eZ + 2;
        pm = 1ULL << (pn - 1); pv = (uint32_t)c; pk = 1;
        steps += pn;
        pen = c;
        swap = 0;  // Y must pay
      }
    } else if (eY < pen) {
      // Y answers with a court card at index eY - or runs out trying.
      if (eY == lastY) {
        const int court_last = (int)(mY >> eY) & 1;
        r.outcome = lab ? CVC_A_WINS : CVC_B_WINS;
        r.steps = steps + nY;
        r.captures = caps + (off && !court_last && nY == pen);
        return r;
      }
      const int c = vY & 3;
      vY >>= 2; kY--;
      mY >>= eY + 1; nY -= eY + 1;
      pm |= 1ULL << (pn + eY);
      pv |= (uint32_t)c << (2 * pk); pk++;
      pn += eY + 1; steps += eY + 1;
      pen = c;
      swap = 1;
    } else {
      // Y pays `pen` plain cards, Z collects the pile and leads.
      mY >>= pen; nY -= pen;
      pn += pen; steps += pen;
      mZ |= pm << nZ; vZ |= pv << (2 * kZ);
      nZ += pn; kZ += pk;
      pm = 0; pv = 0; pn = 0; pk = 0;
      caps++;
      pen = 0;
      // Trick boundary, Z leads.  (In 'empty' Y still has cards: paying the
      // last one was handled above as a loss.)
      if ((flags & CVC_STOP_LOWER) && nZ == 20) {
        const uint64_t mm = mZ | (mY << 20);
        const uint32_t vv = vZ | (vY << (2 * kZ));
        if (cvc_deal_less(mm, vv, m0, v0)) {
          r.outcome = CVC_STOPPED; r.steps = steps; r.captures = caps;
          return r;
        }
      }
      if (flags & CVC_EXACT) {
        const uint64_t km = mZ | (mY << nZ) | ((uint64_t)nZ << 40);
        const uint32_t kv = vZ | (vY << (2 * kZ));
        if (km == tm && kv == tv) {
          r.outcome = CVC_LOOP; r.steps = steps; r.captures = caps;
          return r;
        }
        if (++lam == power) { tm = km; tv = kv; power <<= 1; lam = 0; }
      } else if (steps > step_limit) {
        r.outcome = CVC_LIMIT; r.steps = steps; r.captures = caps;
        return r;
      }
      swap = 1;
    }

    if (swap) {
      uint64_t tm_ = mY; mY = mZ; mZ = tm_;
      uint32_t tv_ = vY; vY = vZ; vZ = tv_;
      int t = nY; nY = nZ; nZ = t;
      t = kY; kY = kZ; kZ = t;
      lab ^= 1;
    }
  }
}

// Splits a deal given as (court mask over 40 positions, court values in
// order) into the two hands and plays it.  A = positions 0..19, A leads.
CVC_FN cvc_result cvc_play_mv(uint64_t m, uint32_t v, int rule, int flags, int step_limit) {
  const uint64_t mA = m & 0xFFFFFULL;
  const int kA = CVC_POP64(mA);
  const uint32_t vA = v & ((1u << (2 * kA)) - 1u);
  return cvc_run(mA, vA, 20, m >> 20, v >> (2 * kA), 20, 0, rule, flags, step_limit);
}

// Same, from an array of 40 card values (0..3).
CVC_FN void cvc_deck_to_mv(const uint8_t deck[40], uint64_t *m, uint32_t *v) {
  uint64_t mm = 0;
  uint32_t vv = 0;
  int k = 0;
  for (int i = 0; i < 40; i++)
    if (deck[i]) { mm |= 1ULL << i; vv |= (uint32_t)deck[i] << (2 * k++); }
  *m = mm; *v = vv;
}

#endif
