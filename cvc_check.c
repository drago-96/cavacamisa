// cvc_check.c - run deals through cvc_engine.h (for cross-checking against
// reference.py) or benchmark it on random deals.
//
//   ./cvc_check [--rule=empty|must] < deals.txt     one 40-digit deal per line
//   ./cvc_check --bench=N [--rule=...] [--seed=S]  N random deals, single thread
//   options: --step-limit=N (instead of exact Brent loop detection), --stop-lower

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cvc_engine.h"

static uint64_t rng_state;
static uint64_t splitmix64(void) {
  uint64_t z = (rng_state += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

static void random_deck(uint8_t d[40]) {
  for (int i = 0; i < 40; i++) d[i] = i < 28 ? 0 : (uint8_t)(1 + (i - 28) / 4);
  for (int i = 39; i > 0; i--) {
    int j = (int)(((splitmix64() >> 32) * (uint64_t)(i + 1)) >> 32);
    uint8_t t = d[i]; d[i] = d[j]; d[j] = t;
  }
}

static const char *outcome_name(int o) {
  static const char *names[] = {"A", "B", "loop", "limit", "stopped"};
  return names[o];
}

int main(int argc, char **argv) {
  int rule = CVC_RULE_EMPTY;
  int flags = CVC_EXACT, step_limit = 20000;
  long long bench = 0;
  rng_state = 1;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--rule=empty")) rule = CVC_RULE_EMPTY;
    else if (!strcmp(argv[i], "--rule=must")) rule = CVC_RULE_MUST;
    else if (!strncmp(argv[i], "--bench=", 8)) bench = atoll(argv[i] + 8);
    else if (!strncmp(argv[i], "--step-limit=", 13)) { flags &= ~CVC_EXACT; step_limit = atoi(argv[i] + 13); }
    else if (!strcmp(argv[i], "--stop-lower")) flags |= CVC_STOP_LOWER;
    else if (!strncmp(argv[i], "--seed=", 7)) rng_state = strtoull(argv[i] + 7, NULL, 10);
    else { fprintf(stderr, "unknown argument %s\n", argv[i]); return 1; }
  }

  if (bench) {
    long long count[5] = {0}, steps = 0, caps = 0;
    uint8_t d[40];
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long long g = 0; g < bench; g++) {
      random_deck(d);
      uint64_t m; uint32_t v;
      cvc_deck_to_mv(d, &m, &v);
      cvc_result r = cvc_play_mv(m, v, rule, flags, step_limit);
      count[r.outcome]++;
      steps += r.steps;
      caps += r.captures;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
    printf("games=%lld  A=%.5f B=%.5f loop=%lld limit=%lld stopped=%.4f  avg_steps=%.3f avg_caps=%.3f\n",
           bench, (double)count[0] / bench, (double)count[1] / bench, count[2], count[3], (double)count[4] / bench,
           (double)steps / bench, (double)caps / bench);
    printf("%.3f s, %.2f M games/s (single thread, including shuffling)\n", s, bench / s / 1e6);
    return 0;
  }

  char line[256];
  while (fgets(line, sizeof line, stdin)) {
    uint8_t d[40];
    int k = 0;
    for (char *c = line; *c && k < 40; c++)
      if (*c >= '0' && *c <= '3') d[k++] = (uint8_t)(*c - '0');
    if (k != 40) continue;
    uint64_t m; uint32_t v;
    cvc_deck_to_mv(d, &m, &v);
    cvc_result r = cvc_play_mv(m, v, rule, flags, step_limit);
    for (int i = 0; i < 40; i++) putchar('0' + d[i]);
    if (r.outcome == CVC_LOOP) printf(" loop\n");
    else printf(" %s steps=%d captures=%d\n", outcome_name(r.outcome), r.steps, r.captures);
  }
  return 0;
}
