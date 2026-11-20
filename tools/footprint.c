/* The Mac's memory meter for tools/shrink.mjs: a process's phys_footprint (what Activity Monitor calls its Memory:
 * dirty and compressed pages, the macOS counterpart of Linux's Private_Dirty plus Swap) and its resident size, in
 * bytes, from proc_pid_rusage.
 *   footprint <pid>            one line: "<footprint> <resident>"
 *   footprint <pid> <ms>       a line every ms milliseconds, "<us since start> <footprint> <resident>", until the
 *                              process ends or stdout closes
 * Build: cc -O2 -o tools/out/footprint tools/footprint.c (shrink.mjs builds it when it is missing). */
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

static int read_one(int pid, unsigned long long *fp, unsigned long long *rss) {
  struct rusage_info_v4 ri;
  if (proc_pid_rusage(pid, RUSAGE_INFO_V4, (rusage_info_t *)&ri)) return 0;
  *fp = ri.ri_phys_footprint;
  *rss = ri.ri_resident_size;
  return 1;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: footprint <pid> [ms]\n"); return 2; }
  int pid = atoi(argv[1]);
  unsigned long long fp, rss;
  if (argc < 3) {
    if (!read_one(pid, &fp, &rss)) { perror("proc_pid_rusage"); return 1; }
    printf("%llu %llu\n", fp, rss);
    return 0;
  }
  long ms = atol(argv[2]);
  struct timespec t0, t;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  setvbuf(stdout, 0, _IOLBF, 0);
  while (read_one(pid, &fp, &rss)) {
    clock_gettime(CLOCK_MONOTONIC, &t);
    long long us = (t.tv_sec - t0.tv_sec) * 1000000LL + (t.tv_nsec - t0.tv_nsec) / 1000;
    if (printf("%lld %llu %llu\n", us, fp, rss) < 0) return 0;
    usleep((useconds_t)(ms * 1000));
  }
  return 0;
}
