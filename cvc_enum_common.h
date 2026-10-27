// cvc_enum_common.h - enumeration helpers, checkpoint files and result
// merging for the exhaustive enumerator (cvc_enum.c).
//
// Every deal is (M, V): M = positions of the 12 court cards among 40 (one of
// C(40,12) = 5,586,853,480 masks, ranked in colex order = numeric order), V =
// the sequence of their values (one of 12!/(4!4!4!) = 34,650 patterns).
// Deal number = rank(M) * 34650 + pattern index; there are 193,582,473,082,000.
//
// A run covers a range of mask ranks and produces, in its output directory:
//   summary.txt        counts per outcome, next mask rank (used to resume)
//   steps_hist.csv     steps,count     (exact, every finished game)
//   captures_hist.csv  captures,count
//   records.txt        deal outcome steps captures - every game that hit the
//                      step limit or overflowed the histogram ranges

#ifndef CVC_ENUM_COMMON_H
#define CVC_ENUM_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cvc_engine.h"
#include "cvc_flat.h"  // CVC_NPAT, CVC_HIST_*, cvc_unrank

#define CVC_NMASKS 5586853480ULL
#define CVC_MAX_STEPS 1000000  // host-side histogram range (records fill it)

static uint64_t cvc_binom[41][13];

static void cvc_init_binom(void) {
  for (int n = 0; n <= 40; n++)
    for (int k = 0; k <= 12; k++)
      cvc_binom[n][k] = (k == 0) ? 1 : (n == 0 ? 0 : cvc_binom[n - 1][k - 1] + cvc_binom[n - 1][k]);
}

// Mask with 12 bits set whose colex rank is r (requires cvc_init_binom).
static inline uint64_t cvc_unrank_mask(uint64_t r) { return cvc_unrank((const uint64_t(*)[13])cvc_binom, r); }

static inline uint64_t cvc_next_mask(uint64_t m) {  // Gosper's hack
  uint64_t c = m & -m, r = m + c;
  return (((r ^ m) >> 2) / c) | r;
}

static void cvc_patterns_rec(uint32_t *out, int *n, int cnt[4], int pos, uint32_t v) {
  if (pos == 12) { out[(*n)++] = v; return; }
  for (int c = 1; c <= 3; c++)
    if (cnt[c]) {
      cnt[c]--;
      cvc_patterns_rec(out, n, cnt, pos + 1, v | ((uint32_t)c << (2 * pos)));
      cnt[c]++;
    }
}

// All 34,650 value patterns in lexicographic order, packed 2 bits per card.
static int cvc_make_patterns(uint32_t *out) {
  int n = 0, cnt[4] = {0, 4, 4, 4};
  cvc_patterns_rec(out, &n, cnt, 0, 0);
  return n;
}

static void cvc_deal_string(uint64_t m, uint32_t v, char out[41]) {
  for (int i = 0; i < 40; i++) {
    if ((m >> i) & 1) { out[i] = (char)('0' + (v & 3)); v >>= 2; }
    else out[i] = '0';
  }
  out[40] = 0;
}

typedef struct {
  uint64_t first_rank, next_rank, end_rank;  // mask ranks: [first, end), done up to next
  uint64_t count[5];                         // A, B, loop, limit, stopped
  uint64_t *steps;                           // [CVC_MAX_STEPS]
  uint64_t *caps;                            // [CVC_MAX_STEPS]
  int rule, flags, step_limit;
} cvc_totals;

static const char *cvc_outcome_names[5] = {"A", "B", "loop", "limit", "stopped"};

static void cvc_totals_init(cvc_totals *t) {
  memset(t, 0, sizeof *t);
  t->steps = (uint64_t *)calloc(CVC_MAX_STEPS, sizeof(uint64_t));
  t->caps = (uint64_t *)calloc(CVC_MAX_STEPS, sizeof(uint64_t));
}

static void cvc_write_file_atomic(const char *dir, const char *name, void (*fill)(FILE *, const cvc_totals *),
                                  const cvc_totals *t) {
  char path[1024], tmp[1100];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "w");
  if (!f) { perror(tmp); exit(1); }
  fill(f, t);
  fclose(f);
  rename(tmp, path);
}

static void cvc_fill_summary(FILE *f, const cvc_totals *t) {
  uint64_t games = 0;
  for (int i = 0; i < 5; i++) games += t->count[i];
  fprintf(f, "rule %s\nflags %d\nstep_limit %d\n", t->rule ? "must" : "empty", t->flags, t->step_limit);
  fprintf(f, "first_rank %llu\nnext_rank %llu\nend_rank %llu\n", (unsigned long long)t->first_rank,
          (unsigned long long)t->next_rank, (unsigned long long)t->end_rank);
  fprintf(f, "games %llu\n", (unsigned long long)games);
  for (int i = 0; i < 5; i++) fprintf(f, "%s %llu\n", cvc_outcome_names[i], (unsigned long long)t->count[i]);
}

static void cvc_fill_steps(FILE *f, const cvc_totals *t) {
  fprintf(f, "steps,count\n");
  for (int i = 0; i < CVC_MAX_STEPS; i++)
    if (t->steps[i]) fprintf(f, "%d,%llu\n", i, (unsigned long long)t->steps[i]);
}

static void cvc_fill_caps(FILE *f, const cvc_totals *t) {
  fprintf(f, "captures,count\n");
  for (int i = 0; i < CVC_MAX_STEPS; i++)
    if (t->caps[i]) fprintf(f, "%d,%llu\n", i, (unsigned long long)t->caps[i]);
}

static void cvc_save(const char *dir, const cvc_totals *t) {
  cvc_write_file_atomic(dir, "steps_hist.csv", cvc_fill_steps, t);
  cvc_write_file_atomic(dir, "captures_hist.csv", cvc_fill_caps, t);
  cvc_write_file_atomic(dir, "summary.txt", cvc_fill_summary, t);  // last: marks the checkpoint
}

// mkdir -p
static void cvc_mkdirs(const char *dir) {
  char p[1024];
  snprintf(p, sizeof p, "%s", dir);
  for (char *c = p + 1; *c; c++)
    if (*c == '/') { *c = 0; mkdir(p, 0755); *c = '/'; }
  mkdir(p, 0755);
}

// ---- One batch of results (filled by the worker threads).

typedef struct {
  uint64_t idx;  // deal number
  uint64_t m;
  uint32_t v;
  int outcome, steps, captures;
} cvc_rec;

typedef struct {
  uint64_t count[5];  // games not in recs
  uint64_t steps[CVC_HIST_STEPS], caps[CVC_HIST_CAPS];
  cvc_rec *recs;      // loops, step-limit hits, histogram overflows
  size_t nrec, caprec;
} cvc_batch;

static void cvc_batch_add_rec(cvc_batch *b, cvc_rec x) {
  if (b->nrec == b->caprec) {
    b->caprec = b->caprec ? 2 * b->caprec : 64;
    b->recs = (cvc_rec *)realloc(b->recs, b->caprec * sizeof *b->recs);
  }
  b->recs[b->nrec++] = x;
}

static int cvc_cmp_rec(const void *a, const void *b) {
  const cvc_rec *x = (const cvc_rec *)a, *y = (const cvc_rec *)b;
  return x->idx < y->idx ? -1 : x->idx > y->idx;
}

// Merges a finished batch into the totals; step-limit games are re-played
// with exact cycle detection, so every record ends up loop / A / B.
static void cvc_merge(cvc_batch *b, cvc_totals *t, FILE *recf) {
  for (int i = 0; i < 5; i++) t->count[i] += b->count[i];
  for (int i = 0; i < CVC_HIST_STEPS; i++) t->steps[i] += b->steps[i];
  for (int i = 0; i < CVC_HIST_CAPS; i++) t->caps[i] += b->caps[i];
  qsort(b->recs, b->nrec, sizeof *b->recs, cvc_cmp_rec);
  for (size_t i = 0; i < b->nrec; i++) {
    cvc_rec *x = &b->recs[i];
    if (x->outcome == CVC_LIMIT) {
      cvc_result r = cvc_play_mv(x->m, x->v, t->rule, CVC_EXACT, 0);
      x->outcome = r.outcome; x->steps = r.steps; x->captures = r.captures;
    }
    t->count[x->outcome]++;
    if (x->outcome <= CVC_B_WINS) {
      t->steps[x->steps < CVC_MAX_STEPS ? x->steps : CVC_MAX_STEPS - 1]++;
      t->caps[x->captures < CVC_MAX_STEPS ? x->captures : CVC_MAX_STEPS - 1]++;
    }
    char s[41];
    cvc_deal_string(x->m, x->v, s);
    fprintf(recf, "%llu %s %s %d %d\n", (unsigned long long)x->idx, s, cvc_outcome_names[x->outcome],
            x->steps, x->captures);
  }
  fflush(recf);
}

// On resume: drop records written after the last checkpoint (deal number >= limit).
static void cvc_truncate_records(const char *path, uint64_t limit) {
  char tmp[1100], line[256];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *in = fopen(path, "r"), *out;
  if (!in) return;
  out = fopen(tmp, "w");
  if (!out) { perror(tmp); exit(1); }
  while (fgets(line, sizeof line, in))
    if (strtoull(line, NULL, 10) < limit) fputs(line, out);
  fclose(in);
  fclose(out);
  rename(tmp, path);
}

// Loads a previous checkpoint from dir; returns 0 if there is none.
static int cvc_load(const char *dir, cvc_totals *t) {
  char path[1024], key[64];
  unsigned long long val;
  snprintf(path, sizeof path, "%s/summary.txt", dir);
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char sval[64];
    if (sscanf(line, "%63s %63s", key, sval) != 2) continue;
    val = strtoull(sval, NULL, 10);
    if (!strcmp(key, "rule")) t->rule = !strcmp(sval, "must");
    else if (!strcmp(key, "flags")) t->flags = (int)val;
    else if (!strcmp(key, "step_limit")) t->step_limit = (int)val;
    else if (!strcmp(key, "first_rank")) t->first_rank = val;
    else if (!strcmp(key, "next_rank")) t->next_rank = val;
    else if (!strcmp(key, "end_rank")) t->end_rank = val;
    else for (int i = 0; i < 5; i++) if (!strcmp(key, cvc_outcome_names[i])) t->count[i] = val;
  }
  fclose(f);
  const char *names[2] = {"steps_hist.csv", "captures_hist.csv"};
  uint64_t *arrs[2] = {t->steps, t->caps};
  for (int h = 0; h < 2; h++) {
    snprintf(path, sizeof path, "%s/%s", dir, names[h]);
    f = fopen(path, "r");
    if (!f) { fprintf(stderr, "missing %s\n", path); exit(1); }
    if (!fgets(line, sizeof line, f)) { fprintf(stderr, "empty %s\n", path); exit(1); }
    int i;
    unsigned long long c;
    while (fscanf(f, "%d,%llu", &i, &c) == 2) arrs[h][i] = c;
    fclose(f);
  }
  return 1;
}

#endif
