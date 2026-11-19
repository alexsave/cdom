/* libFuzzer target: any bytes into the JPEG path (cd_image_resize, a 900 box), on the counted native heap
 * (host_alloc.c). The module's statuses are all fine; what fails is a crash, a sanitizer report, a leak, an
 * allocation over libFuzzer's -malloc_limit_mb, or the heap over -rss_limit_mb (tests/image/fuzz.sh sets both so
 * the review's item 1, 384 MB from a small file, would be caught). */
#include <stddef.h>
#include <stdint.h>
#include "cdom_image.h"
#include "host_alloc.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  uint8_t *rgba; uint32_t w, h;
  size_t live = host_live;
  uint32_t st = cd_image_resize(data, (uint32_t)size, 900, 900, &rgba, &w, &h);
  if (st == CD_IMAGE_OK) cd_free(rgba);
  else if (rgba || w || h) __builtin_trap();     /* a refusal with an image */
  if (host_live != live) __builtin_trap();       /* something left allocated */
  return 0;
}
