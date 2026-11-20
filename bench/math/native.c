/* The math bench's native driver (docs/math-design.md): every arm's native twin, and glibc's libm, in one program.
 *   bench time [rounds=9] [ms=5] [entries]   ns per call, as TSV on stdout: env round entry domain arm ns reps.
 *                    The same rounds as the browser's (glue.js): a warm-up that sets each cell's reps so a run takes
 *                    about ms, then each round runs every (entry, domain) once per arm, the arms in an order that
 *                    rotates each round. Each run's sum is added up per arm and printed at the end (# sum), so no
 *                    loop's calls can be dropped. entries: a comma list of names (sin,sinf), all by default.
 *   bench digest [n=100000]   the hash of each arm's results on the wasm-equals-native inputs (gen.h), one line
 *                    per arm, entry and domain, for tools/twin.mjs to compare with the wasm builds'.
 *   bench special    each arm's special-value test (special.c); exits 1 if any arm fails. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "arms.h"
#include "fns.h"

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
}
static const char *names[BM_NENTRY];
#define NAME_ROW(n, arity, doms) #n, #n "f",
static const char *const all_names[BM_NENTRY] = { BM_FNS(NAME_ROW) };
static const char *dom_name(int d) { return d == BM_SMALL ? "small" : d == BM_TYPICAL ? "typical" : "large"; }
static int wanted(const char *list, const char *name) {
  if (!list) return 1;
  size_t n = strlen(name);
  for (const char *p = list; (p = strstr(p, name)); p += n)
    if ((p == list || p[-1] == ',') && (p[n] == ',' || p[n] == 0)) return 1;
  return 0;
}

static int run_time(int rounds, double ms, const char *list) {
  static int reps[8][BM_NENTRY][5];
  double sums[8] = { 0 };
  for (int e = 0; e < BM_NENTRY; e++) names[e] = all_names[e];
  /* The warm-up: each cell (arm, entry, domain) runs until its reps take about ms. */
  for (int a = 0; a < BM_NARMS; a++)
    for (int e = 0; e < BM_NENTRY; e++)
      for (int d = 1; d <= 4; d <<= 1) {
        if (!(bm_arms[a].doms(e) & d) || !wanted(list, names[e])) continue;
        bm_arms[a].fill(e, d);
        int r = 1;
        double t;
        for (;;) {
          double t0 = now_ms();
          sums[a] += bm_arms[a].run(e, r);
          t = now_ms() - t0;
          if (t > ms / 4 || r > 1 << 20) break;
          r *= 2;
        }
        int want = (int)(r * ms / (t > 1e-6 ? t : 1e-6)) + 1;
        reps[a][e][d] = want < 1 ? 1 : want;
      }
  printf("env\tround\tentry\tdomain\tarm\tns\treps\n");
  for (int round = 0; round < rounds; round++)
    for (int e = 0; e < BM_NENTRY; e++)
      for (int d = 1; d <= 4; d <<= 1)
        for (int k = 0; k < BM_NARMS; k++) {
          int a = (k + round) % BM_NARMS;
          if (!(bm_arms[a].doms(e) & d) || !wanted(list, names[e])) continue;
          bm_arms[a].fill(e, d);
          double t0 = now_ms();
          sums[a] += bm_arms[a].run(e, reps[a][e][d]);
          double t = now_ms() - t0;
          printf("native\t%d\t%s\t%s\t%s\t%.3f\t%d\n", round, names[e], dom_name(d), bm_arms[a].name, t * 1e6 / (reps[a][e][d] * 4096.0), reps[a][e][d]);
          fflush(stdout);
        }
  for (int a = 0; a < BM_NARMS; a++) printf("# sum %s %.17g\n", bm_arms[a].name, sums[a]);
  return 0;
}

static int run_digest(int n) {
  for (int a = 0; a < BM_NARMS; a++)
    for (int e = 0; e < BM_NENTRY; e++)
      for (int d = 1; d <= 4; d <<= 1)
        if (bm_arms[a].doms(e) & d)
          printf("%s\t%s\t%s\t%016llx\n", bm_arms[a].name, all_names[e], dom_name(d), (unsigned long long)bm_arms[a].digest(e, d, n));
  return 0;
}

static int run_special(void) {
  int bad = 0;
  for (int a = 0; a < BM_NARMS; a++) {
    int f = bm_arms[a].sp_run();
    printf("%s: %d failure%s\n%s", bm_arms[a].name, f, f == 1 ? "" : "s", bm_arms[a].sp_text());
    bad |= f != 0;
  }
  return bad;
}

int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "time";
  if (!strcmp(mode, "time")) return run_time(argc > 2 ? atoi(argv[2]) : 9, argc > 3 ? atof(argv[3]) : 5, argc > 4 ? argv[4] : NULL);
  if (!strcmp(mode, "digest")) return run_digest(argc > 2 ? atoi(argv[2]) : 100000);
  if (!strcmp(mode, "special")) return run_special();
  fprintf(stderr, "usage: bench time [rounds] [ms] [entries] | digest [n] | special\n");
  return 2;
}
