/* The image module's size, by format (bench/image/sizes/run.sh): one app, built with no image code (NONE), with
 * cd_image_resize (both formats), and with one format's internal entry point only (ONLY_JPEG, ONLY_PNG), so the linker
 * drops the other. */
#include "cdom.h"
#include "cdom_image.h"
#include "cdom_image_int.h"
__attribute__((unused)) static uint32_t out[3];
__attribute__((export_name("room"))) void *room(uint32_t n) { return cd_alloc_try(n ? n : 1); }
__attribute__((export_name("drop"))) void drop(void *p) { cd_free(p); }
__attribute__((export_name("resize"))) uint32_t resize(const uint8_t *p, uint32_t len, uint32_t bw, uint32_t bh) {
#if defined(NONE)
  (void)p; (void)len; (void)bw; (void)bh; return 0;
#elif defined(ONLY_JPEG) || defined(ONLY_PNG)
  cd_image_info i = {0}; uint8_t *rgba;
#ifdef ONLY_JPEG
  uint32_t st = cd_image_jpeg_probe(p, len, &i); if (st) return st;
  st = cd_image_jpeg_resize(p, len, &i, bw, bh, &rgba, &out[0], &out[1]);
#else
  uint32_t st = cd_image_png_probe(p, len, &i); if (st) return st;
  st = cd_image_png_resize(p, len, &i, bw, bh, &rgba, &out[0], &out[1]);
#endif
  out[2] = (uint32_t)(uintptr_t)rgba; return st;
#else
  uint8_t *rgba; uint32_t st = cd_image_resize(p, len, bw, bh, &rgba, &out[0], &out[1]);
  out[2] = (uint32_t)(uintptr_t)rgba; return st;
#endif
}
void cd_main(void) {}
