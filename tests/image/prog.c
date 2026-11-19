/* The progressive decoder's differential test (tests/image/prog.sh): every file of a list decoded by this branch's
 * progressive path (src/cdom_image_jpegp.c) and by libjpeg-turbo's own (jdphuff.c and jdcoefct.c, built in the same
 * binary with every symbol renamed ref_*), at every scale from 1/8 to 8/8, and each pair compared: the status, the size,
 * and every byte of the image. The decode is cd_image_jpeg_resize with the target the size of the scaled decode, so the
 * scaler copies the decoded rows as they are (the image compared is libjpeg's output itself). Both run on
 * tests/image/host_alloc.c's counted heap; the peak of each is compared too (the new store is never larger).
 * Usage: prog DIR [LIST]    (LIST defaults to DIR/list.txt)
 * Prints a FAIL line for each difference and a summary; exits 1 on any. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cdom_image.h"
#include "cdom_image_int.h"
#include "host_alloc.h"

uint32_t ref_cd_image_jpeg_resize(const uint8_t *p, uint32_t len, const cd_image_info *info, uint32_t dw, uint32_t dh,
                                  uint8_t **rgba, uint32_t *w, uint32_t *h);
extern uint32_t cd_image_jpegp_skipped;     /* scans skipped (src/cdom_image_jpegp.c, CDOM_IMAGE_TESTING) */

static uint8_t *read_file(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc(l ? (size_t)l : 1);
  if (fread(b, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(b); return 0; }
  fclose(f);
  *n = (size_t)l;
  return b;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: prog DIR [LIST]\n"); return 2; }
  char path[4096], line[1024];
  snprintf(path, sizeof path, "%s", argc > 2 ? argv[2] : "");
  if (argc <= 2) snprintf(path, sizeof path, "%s/list.txt", argv[1]);
  FILE *list = fopen(path, "r");
  if (!list) { printf("FAIL no list %s\n", path); return 1; }
  unsigned files = 0, pairs = 0, fails = 0, ok = 0, refused = 0, statuses[8] = {0};
  unsigned long long bytes = 0, peak_new = 0, peak_ref = 0;
  while (fgets(line, sizeof line, list)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) continue;
    snprintf(path, sizeof path, "%s/%s", argv[1], line);
    size_t len;
    uint8_t *p = read_file(path, &len);
    if (!p) { printf("FAIL cannot read %s\n", path); fails++; continue; }
    files++;
    cd_image_info info;
    memset(&info, 0, sizeof info);
    uint32_t pst = cd_image_jpeg_probe(p, (uint32_t)len, &info);
    if (pst != CD_IMAGE_OK) { refused++; free(p); continue; }
    info.orientation = 1;                     /* the decoded rows as stored */
    for (uint32_t n = 1; n <= 8; n++) {
      uint32_t dw = (uint32_t)(((uint64_t)info.width * n + 7) / 8), dh = (uint32_t)(((uint64_t)info.height * n + 7) / 8);
      uint8_t *a = 0, *b = 0;
      uint32_t aw = 0, ah = 0, bw = 0, bh = 0;
      size_t base = host_live;
      host_reset(SIZE_MAX);
      uint32_t sa = cd_image_jpeg_resize(p, (uint32_t)len, &info, dw, dh, &a, &aw, &ah);
      size_t pa = host_peak - base;
      host_reset(SIZE_MAX);
      uint32_t sb = ref_cd_image_jpeg_resize(p, (uint32_t)len, &info, dw, dh, &b, &bw, &bh);
      size_t pb = host_peak - base;
      pairs++;
      statuses[sa < 8 ? sa : 7]++;
      int same = sa == sb && aw == bw && ah == bh && (sa != CD_IMAGE_OK || !memcmp(a, b, (size_t)aw * ah * 4));
      /* the one intended difference: a file whose whole coefficients exceed the budget, which the smaller store holds */
      if (!same && sb == CD_IMAGE_TOO_BIG && sa == CD_IMAGE_OK) same = 1;
      if (!same) {
        fails++;
        long at = -1;
        if (sa == CD_IMAGE_OK && sb == CD_IMAGE_OK && aw == bw && ah == bh)
          for (size_t i = 0; i < (size_t)aw * ah * 4; i++) if (a[i] != b[i]) { at = (long)(i / 4); break; }
        printf("FAIL %s at %u/8: new status %u %ux%u, libjpeg status %u %ux%u, first differing pixel %ld\n", line, n, sa, aw, ah, sb, bw, bh, at);
      }
      if (sa == CD_IMAGE_OK) { ok++; bytes += (unsigned long long)aw * ah * 4; }
      if (sa == CD_IMAGE_OK && sb == CD_IMAGE_OK && info.progressive) {
        peak_new += pa; peak_ref += pb;
        /* the store against libjpeg's: never more, beside the decoder's own tables (under 64 KB) */
        if (pa > pb + 65536) { fails++; printf("FAIL %s at %u/8: held %zu bytes, libjpeg %zu\n", line, n, pa, pb); }
      }
      if (sa == CD_IMAGE_OK) cd_free(a);
      if (sb == CD_IMAGE_OK) cd_free(b);
      if (host_live != base) { fails++; printf("FAIL %s at %u/8: %zu bytes left allocated\n", line, n, host_live - base); }
    }
    free(p);
  }
  fclose(list);
  printf("prog: %u files (%u refused at the probe), %u decodes compared, %u of them images (%llu bytes), statuses OK %u TOO_BIG %u CORRUPT %u NO_ROOM %u; scans skipped %u; peak held, progressive, new %.1f MB against libjpeg's %.1f MB; %u differences\n",
         files, refused, pairs, ok, bytes, statuses[CD_IMAGE_OK], statuses[CD_IMAGE_TOO_BIG], statuses[CD_IMAGE_CORRUPT], statuses[CD_IMAGE_NO_ROOM],
         cd_image_jpegp_skipped, peak_new / 1e6, peak_ref / 1e6, fails);
  return fails ? 1 : 0;
}
