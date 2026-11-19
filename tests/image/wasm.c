/* The image module in wasm, for tools/image.mjs (tests/image/run.sh): the page puts a file's bytes in a block of
 * room's and calls resize, which answers the status and leaves the size and the RGBA block in out[]; drop frees it.
 * Built twice by tests/image/wasm.mk, once each way out of a libjpeg error (CDOM_IMAGE_SJLJ js and eh), and once
 * more with the heap capped (CDOM_HEAP_PAGES_MAX), for NO_ROOM. */
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
EXPORT("probe") uint32_t probe(const uint8_t *p, uint32_t len) {
  cd_image_info i;
  uint32_t st = cd_image_probe(p, len, &i);
  out[0] = i.shown_w; out[1] = i.shown_h; out[2] = i.orientation;
  return st;
}
/* The stack pointer's place now: the same before and after any number of errors, or the guard left it lower. */
EXPORT("sp") uint32_t sp(void) { return (uint32_t)(uintptr_t)__builtin_frame_address(0); }
void cd_main(void) {}
