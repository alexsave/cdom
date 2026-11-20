/* libFuzzer target, differential (tests/image/prog.sh --fuzz): any bytes that probe as a JPEG of at most 4 MP decoded
 * at 1/8, 2/8, 3/8, 4/8 and 8/8 by this branch's progressive path and by libjpeg-turbo's own (the ref_* build, as
 * tests/image/prog.c), on the counted native heap. A difference in status, size or any byte, a crash, a sanitizer
 * report, a leak, or a marker walk that is not the scans libjpeg read (tests/image/walkcheck.h) is a finding (the one
 * allowed difference: libjpeg's whole buffer over the budget, TOO_BIG, where the smaller store fits). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "cdom_image.h"
#include "cdom_image_int.h"
#include "host_alloc.h"
#include "walkcheck.h"

uint32_t ref_cd_image_jpeg_resize(const uint8_t *p, uint32_t len, const cd_image_info *info, uint32_t dw, uint32_t dh,
                                  uint8_t **rgba, uint32_t *w, uint32_t *h);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  cd_image_info info;
  memset(&info, 0, sizeof info);
  if (size > 0xffffffffu || cd_image_jpeg_probe(data, (uint32_t)size, &info) != CD_IMAGE_OK) return 0;
  if ((uint64_t)info.width * info.height > 4000000) return 0;     /* a bound on the time a case takes */
  info.orientation = 1;
  static const uint32_t scales[] = {1, 2, 3, 4, 8};
  for (int i = 0; i < 5; i++) {
    uint32_t n = scales[i];
    uint32_t dw = (uint32_t)(((uint64_t)info.width * n + 7) / 8), dh = (uint32_t)(((uint64_t)info.height * n + 7) / 8);
    uint8_t *a = 0, *b = 0;
    uint32_t aw = 0, ah = 0, bw = 0, bh = 0;
    size_t live = host_live;
    walk_reset();
    uint32_t skipped0 = cd_image_jpegp_skipped;
    uint32_t sa = cd_image_jpeg_resize(data, (uint32_t)size, &info, dw, dh, &a, &aw, &ah);
    if (walk_check(cd_image_jpegp_skipped - skipped0)) __builtin_trap();   /* the walk is not libjpeg's read_markers */
    uint32_t sb = ref_cd_image_jpeg_resize(data, (uint32_t)size, &info, dw, dh, &b, &bw, &bh);
    int same = sa == sb && aw == bw && ah == bh && (sa != CD_IMAGE_OK || !memcmp(a, b, (size_t)aw * ah * 4));
    if (!same && !(sb == CD_IMAGE_TOO_BIG && sa == CD_IMAGE_OK)) __builtin_trap();
    if (sa == CD_IMAGE_OK) cd_free(a);
    if (sb == CD_IMAGE_OK) cd_free(b);
    if (host_live != live) __builtin_trap();
  }
  return 0;
}
