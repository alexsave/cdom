/* The image bench's C arm (bench/image/run.mjs): the page puts a file's bytes in a block of room's and times resize,
 * cd_image_resize from the bytes in C memory to the RGBA block, as an app runs it in cd_file_read_cb's callback.
 * Built as an app builds it (bench/image/Makefile, cdom.mk), once each way out of a libjpeg error (CDOM_IMAGE_SJLJ js
 * and eh) and for each CDOM_IMAGE_SIMD (none, simd128, simde). hash and wired are outside the timed call: the FNV-1a
 * of the RGBA block (run.mjs fails if two arms differ on one input) and the SIMD steps the decode wired (simde). */
#include "cdom.h"
#include "cdom_image.h"
#define EXPORT(n) __attribute__((export_name(n)))

static uint32_t out[3];
EXPORT("room") void *room(uint32_t n) { return cd_alloc_try(n ? n : 1); }
EXPORT("drop") void drop(void *p) { cd_free(p); }
EXPORT("out") uint32_t *out_at(void) { return out; }
EXPORT("resize") uint32_t resize(const uint8_t *p, uint32_t len, uint32_t bw, uint32_t bh) {
  uint8_t *rgba;
  uint32_t st = cd_image_resize(p, len, bw, bh, &rgba, &out[0], &out[1]);
  out[2] = (uint32_t)(uintptr_t)rgba;
  return st;
}
EXPORT("hash") uint32_t hash(const uint8_t *p, uint32_t n) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
#ifdef CDOM_IMAGE_SIMDE
extern unsigned int cd_image_simd_wired;
EXPORT("wired") uint32_t wired(void) { return cd_image_simd_wired; }
#else
EXPORT("wired") uint32_t wired(void) { return 0; }
#endif
void cd_main(void) {}
