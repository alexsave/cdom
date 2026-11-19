/* The image bench, natively (bench/image/native.sh): cd_image_resize on one file, N times, for perf. The same code as
 * the wasm C arm (cdom's three files, libjpeg-turbo, tinfl), x86-64, on tests/image/host_alloc.c's heap. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "cdom_image.h"

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: native FILE N [BOX]\n"); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 2; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *b = malloc((size_t)n);
  if (fread(b, 1, (size_t)n, f) != (size_t)n) return 2;
  fclose(f);
  int reps = atoi(argv[2]);
  unsigned box = argc > 3 ? (unsigned)atoi(argv[3]) : 900;
  double best = 1e9, sum = 0;
  unsigned w = 0, h = 0;
  for (int i = 0; i < reps; i++) {
    struct timespec t0, t1;
    unsigned char *rgba;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    unsigned st = cd_image_resize(b, (unsigned)n, box, box, &rgba, &w, &h);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    if (st) { fprintf(stderr, "status %u\n", st); return 1; }
    if (ms < best) best = ms;
    sum += ms;
    free((char *)rgba - 16);
  }
  printf("%s: %ux%u mean %.1f ms best %.1f ms\n", argv[1], w, h, sum / reps, best);
  return 0;
}
