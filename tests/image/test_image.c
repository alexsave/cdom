/* Native tests of the image module (src/cdom_image*.c), run by tests/image/run.sh from its output directory, after
 * tests/image/oracle.py made cases.tsv, scaler.tsv, inputs/ and expect/. Every output must equal the oracle's exact
 * image to the byte. The heap is tests/image/host_alloc.c's: every test checks that nothing is left allocated, and
 * the peak cases that the most held at once (with the file) stays under the case's limit.
 * Prints one line per failure, "FAIL <what>", and the counts at the end; exits non-zero on any failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h> /* ssize_t: glibc does not declare it through the C headers alone */
#include "cdom_image.h"
#include "cdom_image_int.h"
#include "host_alloc.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *read_file(const char *path, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) { printf("FAIL cannot open %s\n", path); failures++; *n = 0; return 0; }
  fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc(l ? (size_t)l : 1);
  if (fread(b, 1, (size_t)l, f) != (size_t)l) { printf("FAIL short read %s\n", path); failures++; }
  fclose(f);
  *n = (size_t)l;
  return b;
}

static const char *names[] = {"OK", "NOT_IMAGE", "TOO_BIG", "UNSUPPORTED", "CORRUPT", "NO_ROOM", "BAD_BOX"};
static const char *sname(uint32_t s) { return s < 7 ? names[s] : "?"; }

/* The first pixel that differs, or -1. */
static long first_diff(const uint8_t *a, const uint8_t *b, size_t px) {
  for (size_t i = 0; i < px * 4; i++) if (a[i] != b[i]) return (long)(i / 4);
  return -1;
}

/* ---------- whole files, from cases.tsv ---------- */
static void run_cases(void) {
  FILE *m = fopen("cases.tsv", "r");
  if (!m) { printf("FAIL no cases.tsv: run oracle.py\n"); failures++; return; }
  char line[4096];
  int n = 0;
  while (fgets(line, sizeof line, m)) {
    char *f[8]; int k = 0;
    for (char *t = strtok(line, "\t\n"); t && k < 8; t = strtok(0, "\t\n")) f[k++] = t;
    if (k < 8) continue;
    const char *name = f[0];
    uint32_t bw = (uint32_t)atoi(f[2]), bh = (uint32_t)atoi(f[3]);
    size_t len; uint8_t *in = read_file(f[1], &len);
    if (!in) continue;
    size_t base = host_live;
    host_reset(SIZE_MAX);
    uint8_t *out = (uint8_t *)1; uint32_t w = 7, h = 7;
    uint32_t st = cd_image_resize(in, (uint32_t)len, bw, bh, &out, &w, &h);
    size_t peak = host_peak - base;
    if (!strncmp(f[4], "STATUS:", 7)) {
      CHECK(!strcmp(sname(st), f[4] + 7), "%s: status %s, wanted %s", name, sname(st), f[4] + 7);
      CHECK(!out && !w && !h, "%s: a refusal answered an image (%p, %u x %u)", name, (void *)out, w, h);
    } else if (!strncmp(f[4], "STATUS-ONLY:", 12)) {
      CHECK(!strcmp(sname(st), f[4] + 12), "%s: status %s, wanted %s", name, sname(st), f[4] + 12);
      cd_free(st == CD_IMAGE_OK ? out : 0);
    } else {
      uint32_t ew = (uint32_t)atoi(f[5]), eh = (uint32_t)atoi(f[6]);
      long maxkb = atol(f[7]);
      CHECK(st == CD_IMAGE_OK, "%s: status %s", name, sname(st));
      if (st == CD_IMAGE_OK) {
        CHECK(w == ew && h == eh, "%s: size %ux%u, wanted %ux%u", name, w, h, ew, eh);
        if (w == ew && h == eh) {
          size_t en; uint8_t *exp = read_file(f[4], &en);
          long d = exp && en == (size_t)w * h * 4 ? first_diff(out, exp, (size_t)w * h) : -2;
          CHECK(d == -1, "%s: differs from the exact image at pixel %ld (x %ld, y %ld): %u %u %u %u, wanted %u %u %u %u", name, d,
                d >= 0 ? d % w : -1, d >= 0 ? d / w : -1, d >= 0 ? out[4 * d] : 0, d >= 0 ? out[4 * d + 1] : 0, d >= 0 ? out[4 * d + 2] : 0,
                d >= 0 ? out[4 * d + 3] : 0, d >= 0 ? exp[4 * d] : 0, d >= 0 ? exp[4 * d + 1] : 0, d >= 0 ? exp[4 * d + 2] : 0, d >= 0 ? exp[4 * d + 3] : 0);
          free(exp);
        }
        if (maxkb) {
          size_t total = len + peak;
          if (getenv("IMAGE_TEST_VERBOSE")) printf("  %-34s file %6zu KB, held at most %6zu KB\n", name, len / 1024, peak / 1024);
          CHECK(total <= (size_t)maxkb * 1024, "%s: peak %zu KB (file %zu + held %zu), limit %ld KB", name, total / 1024, len / 1024, peak / 1024, maxkb);
        }
        cd_free(out);
      }
    }
    CHECK(host_live == base, "%s: %zd bytes left allocated", name, (ssize_t)(host_live - base));
    free(in);
    n++;
  }
  fclose(m);
  printf("cases.tsv: %d cases\n", n);
}

/* ---------- the scaler alone, on raw rows ---------- */
static void run_scaler(void) {
  FILE *m = fopen("scaler.tsv", "r");
  if (!m) { printf("FAIL no scaler.tsv\n"); failures++; return; }
  char line[4096]; int n = 0;
  while (fgets(line, sizeof line, m)) {
    char *f[10]; int k = 0;
    for (char *t = strtok(line, "\t\n"); t && k < 10; t = strtok(0, "\t\n")) f[k++] = t;
    if (k < 10) continue;
    uint32_t sw = atoi(f[2]), sh = atoi(f[3]), dw = atoi(f[4]), dh = atoi(f[5]), o = atoi(f[6]);
    size_t sn, en;
    uint8_t *src = read_file(f[1], &sn), *exp = read_file(f[7], &en);
    cd_image_scaler sc;
    size_t base = host_live;
    CHECK(cd_image_scaler_init(&sc, sw, sh, dw, dh, o), "%s: init", f[0]);
    for (uint32_t y = 0; y < sh; y++) {
      cd_image_scaler_row(&sc, src + (size_t)y * sw * 4);
      if (y + 1 < sh && sc.oy == dh) CHECK(0, "%s: every output row finished before the last source row", f[0]);
    }
    CHECK(sc.ow == (uint32_t)atoi(f[8]) && sc.oh == (uint32_t)atoi(f[9]), "%s: output size %ux%u", f[0], sc.ow, sc.oh);
    uint32_t ow = sc.ow, oh = sc.oh;
    uint8_t *out = cd_image_scaler_take(&sc);
    CHECK(out != 0, "%s: no image after the last row", f[0]);
    if (out && en == (size_t)ow * oh * 4) {
      long d = first_diff(out, exp, (size_t)ow * oh);
      CHECK(d == -1, "%s: differs from the exact average at pixel %ld: %u, wanted %u", f[0], d, d >= 0 ? out[4 * d] : 0, d >= 0 ? exp[4 * d] : 0);
    }
    cd_free(out);
    CHECK(host_live == base, "%s: %zd bytes left allocated", f[0], (ssize_t)(host_live - base));
    free(src); free(exp);
    n++;
  }
  fclose(m);
  printf("scaler.tsv: %d cases\n", n);
}

/* ---------- the scaler taken early gives nothing; rows past the end are ignored; its memory ---------- */
static void test_scaler_edges(void) {
  cd_image_scaler sc;
  uint8_t row[10 * 4];
  memset(row, 200, sizeof row);
  size_t base = host_live;
  CHECK(cd_image_scaler_init(&sc, 10, 4, 5, 2, 1), "init");
  cd_image_scaler_row(&sc, row);
  cd_image_scaler_row(&sc, row);
  cd_image_scaler_row(&sc, row);
  cd_image_scaler_row(&sc, row);
  uint32_t oy = sc.oy;
  memset(row, 0, sizeof row);
  cd_image_scaler_row(&sc, row);   /* a fifth row of four: ignored */
  CHECK(sc.oy == oy && sc.row == 4, "a row past the end moved the scaler");
  uint8_t *o = cd_image_scaler_take(&sc);
  CHECK(o && o[0] == 200 && o[3] == 255 && o[4 * 9] == 200, "flat 200 in, flat 200 out");
  cd_free(o);
  CHECK(cd_image_scaler_init(&sc, 10, 4, 5, 2, 1), "init");
  cd_image_scaler_row(&sc, row);
  CHECK(cd_image_scaler_take(&sc) == 0, "an image before the last source row");
  CHECK(host_live == base, "a take before the end left %zd bytes", (ssize_t)(host_live - base));
  host_reset(SIZE_MAX);
  CHECK(cd_image_scaler_init(&sc, 4000, 3000, 900, 675, 1), "init");
  size_t want = (size_t)900 * 675 * 4 + 900 * 3 * 8 + 900 * 3 * 4;
  CHECK(host_peak - base == want, "the scaler holds %zu bytes, wanted %zu (the output, one 64-bit row, one 32-bit row)", host_peak - base, want);
  cd_image_scaler_free(&sc);
  cd_image_scaler_free(&sc);
  CHECK(host_live == base, "leak");
}

/* ---------- the box ---------- */
static void test_target(void) {
  uint32_t w, h;
  cd_image_target(4032, 3024, 1, 900, 900, &w, &h); CHECK(w == 900 && h == 675, "4032x3024 in 900: %ux%u", w, h);
  cd_image_target(4032, 3024, 6, 900, 900, &w, &h); CHECK(w == 900 && h == 675, "the same, orientation 6 (as stored): %ux%u", w, h);
  cd_image_target(4032, 3024, 6, 900, 300, &w, &h); CHECK(w == 300 && h == 225, "orientation 6 in a 900x300 box: shown 225 x 300, so 300 x 225 as stored: %ux%u", w, h);
  cd_image_target(4032, 3024, 1, 900, 300, &w, &h); CHECK(w == 400 && h == 300, "the height binds: %ux%u", w, h);
  cd_image_target(100, 50, 1, 900, 900, &w, &h); CHECK(w == 100 && h == 50, "never enlarged: %ux%u", w, h);
  cd_image_target(3, 2, 1, 2, 2, &w, &h); CHECK(w == 2 && h == 1, "1.33 rounds to 1: %ux%u", w, h);
  cd_image_target(4, 3, 1, 2, 2, &w, &h); CHECK(w == 2 && h == 2, "1.5 rounds half up to 2: %ux%u", w, h);
  cd_image_target(65500, 1, 1, 8192, 8192, &w, &h); CHECK(w == 8192 && h == 1, "a thin image keeps one row: %ux%u", w, h);
}

/* ---------- EXIF ---------- */
static size_t make_exif(uint8_t *b, int le, uint32_t ifd_off, uint16_t count, uint16_t tag, uint16_t type, uint16_t val) {
  size_t n = 0;
  b[n++] = 0xff; b[n++] = 0xd8;
  b[n++] = 0xff; b[n++] = 0xe1;
  size_t seg_at = n; n += 2;
  memcpy(b + n, "Exif\0\0", 6); n += 6;
  size_t t = n;
  b[n++] = le ? 'I' : 'M'; b[n++] = le ? 'I' : 'M';
#define P16(v) do { if (le) { b[n++] = (v) & 255; b[n++] = (v) >> 8; } else { b[n++] = (v) >> 8; b[n++] = (v) & 255; } } while (0)
  P16(42);
  if (le) { b[n++] = ifd_off; b[n++] = ifd_off >> 8; b[n++] = ifd_off >> 16; b[n++] = ifd_off >> 24; }
  else { b[n++] = ifd_off >> 24; b[n++] = ifd_off >> 16; b[n++] = ifd_off >> 8; b[n++] = ifd_off; }
  while (n - t < ifd_off && ifd_off < 100) b[n++] = 0;
  P16(count);
  P16(tag); P16(type); P16(0); P16(1); P16(val); P16(0);
  size_t seg = n - seg_at;
  b[seg_at] = seg >> 8; b[seg_at + 1] = seg & 255;
  b[n++] = 0xff; b[n++] = 0xd9;
  return n;
}
static void test_exif(void) {
  uint8_t b[256];
  for (uint32_t o = 1; o <= 8; o++) {
    size_t n = make_exif(b, 1, 8, 1, 0x0112, 3, o);
    CHECK(cd_image_orientation(b, (uint32_t)n) == o, "little-endian orientation %u", o);
    n = make_exif(b, 0, 8, 1, 0x0112, 3, o);
    CHECK(cd_image_orientation(b, (uint32_t)n) == o, "big-endian orientation %u", o);
  }
  size_t n = make_exif(b, 1, 8, 1, 0x0112, 3, 0);
  CHECK(cd_image_orientation(b, (uint32_t)n) == 1, "orientation 0 reads as 1");
  n = make_exif(b, 1, 8, 1, 0x0112, 3, 9);
  CHECK(cd_image_orientation(b, (uint32_t)n) == 1, "orientation 9 reads as 1");
  n = make_exif(b, 1, 8, 1, 0x0110, 3, 6);
  CHECK(cd_image_orientation(b, (uint32_t)n) == 1, "another tag is not the orientation");
  n = make_exif(b, 1, 8, 1, 0x0112, 4, 6);
  CHECK(cd_image_orientation(b, (uint32_t)n) == 1, "a LONG is not an orientation");
  n = make_exif(b, 1, 0xfffffffeu, 1, 0x0112, 3, 6);
  CHECK(cd_image_orientation(b, (uint32_t)n) == 1, "an IFD offset of 2^32 - 2 is refused, not wrapped");
  n = make_exif(b, 1, 8, 0xffff, 0x0112, 3, 6);
  CHECK(cd_image_orientation(b, (uint32_t)n) == 6, "a huge entry count is bounded by the segment");
  n = make_exif(b, 1, 8, 0xffff, 0x0110, 3, 6);
  CHECK(cd_image_orientation(b, (uint32_t)n) == 1, "an entry count past the segment ends at the segment");
  {   /* an IFD at the segment's last byte, in a block that ends with the segment: the count would be read past
       * the end (ASan says so), so the offset is refused */
    n = make_exif(b, 1, 8, 1, 0x0112, 3, 6);
    size_t seg_end = n - 2;                      /* the EOI after it dropped */
    uint32_t room = (uint32_t)(seg_end - 12);    /* the bytes from the TIFF header (at 12) to the segment's end */
    b[16] = (uint8_t)(room - 1); b[17] = 0; b[18] = 0; b[19] = 0;   /* the IFD offset, little-endian */
    uint8_t *h = malloc(seg_end);
    memcpy(h, b, seg_end);
    CHECK(cd_image_orientation(h, (uint32_t)seg_end) == 1, "an IFD offset at the segment's last byte reads as 1");
    free(h);
  }
  n = make_exif(b, 1, 8, 1, 0x0112, 3, 6);
  for (size_t cut = 0; cut < n; cut++) {
    uint32_t o = cd_image_orientation(b, (uint32_t)cut);
    CHECK(o >= 1 && o <= 8, "cut at %zu: %u", cut, o);
  }
}

/* ---------- refusals, decided from the header with no allocation ---------- */
static uint8_t *patched(const uint8_t *src, size_t n, size_t at, const uint8_t *v, size_t vn) {
  uint8_t *c = malloc(n);
  memcpy(c, src, n);
  memcpy(c + at, v, vn);
  return c;
}
static size_t find(const uint8_t *p, size_t n, const uint8_t *pat, size_t pn) {
  for (size_t i = 0; i + pn <= n; i++) if (!memcmp(p + i, pat, pn)) return i;
  return (size_t)-1;
}
static uint32_t resize_status(const uint8_t *p, size_t n, uint32_t box) {
  uint8_t *o; uint32_t w, h;
  uint32_t s = cd_image_resize(p, (uint32_t)n, box, box, &o, &w, &h);
  if (s != CD_IMAGE_OK) CHECK(!o, "an image came back with status %s", sname(s));
  cd_free(o);
  return s;
}
/* The status, and that no allocation was made for it. */
static uint32_t refused_free(const uint8_t *p, size_t n, uint32_t box, const char *what) {
  host_reset(SIZE_MAX);
  uint32_t s = resize_status(p, n, box);
  CHECK(host_calls == 0, "%s: refused after %zu allocations", what, host_calls);
  return s;
}
static void test_refusals(void) {
  size_t n, pn, qn;
  uint8_t *jpg = read_file("inputs/s420.jpg", &n), *png = read_file("inputs/rgb.png", &pn), *pjpg = read_file("inputs/prog.jpg", &qn);
  size_t base = host_live;
  cd_image_info info;
  CHECK(resize_status((const uint8_t *)"GIF89a......", 12, 900) == CD_IMAGE_NOT_IMAGE, "GIF");
  CHECK(resize_status((const uint8_t *)"", 0, 900) == CD_IMAGE_NOT_IMAGE, "empty");
  CHECK(resize_status(jpg, 2, 900) == CD_IMAGE_NOT_IMAGE, "two bytes");
  size_t sof = find(jpg, n, (const uint8_t[]){0xff, 0xc0, 0x00, 0x11, 0x08}, 5);
  CHECK(sof != (size_t)-1, "no SOF0 in s420.jpg");
  uint8_t *p = patched(jpg, n, sof + 5, (const uint8_t[]){0x1f, 0x41, 0x1f, 0x40}, 4);   /* 8001 x 8000: over 64 MP */
  CHECK(refused_free(p, n, 900, "64 MP + 1 row") == CD_IMAGE_TOO_BIG, "8001x8000 over CD_IMAGE_PIXELS_MAX");
  CHECK(cd_image_probe(p, (uint32_t)n, &info) == CD_IMAGE_TOO_BIG && info.width == 8000 && info.height == 8001, "the probe says TOO_BIG, with the size");
  free(p);
  p = patched(jpg, n, sof + 5, (const uint8_t[]){0x1f, 0x40, 0x1f, 0x40}, 4);
  CHECK(cd_image_probe(p, (uint32_t)n, &info) == CD_IMAGE_OK && info.width == 8000 && info.height == 8000, "8000x8000 is allowed");
  free(p);
  p = patched(jpg, n, sof + 5, (const uint8_t[]){0x00, 0x10, 0xff, 0xdd}, 4);   /* 65501 x 16 */
  CHECK(refused_free(p, n, 900, "a side of 65501") == CD_IMAGE_TOO_BIG, "a side of 65501");
  CHECK(cd_image_probe(p, (uint32_t)n, &info) == CD_IMAGE_TOO_BIG, "the probe applies the side limit too (review item 10)");
  free(p);
  p = patched(jpg, n, sof + 5, (const uint8_t[]){0x00, 0x10, 0xff, 0xdc}, 4);   /* 65500 x 16 */
  CHECK(cd_image_probe(p, (uint32_t)n, &info) == CD_IMAGE_OK, "a side of 65500 probes OK");
  free(p);
  p = patched(jpg, n, sof + 1, (const uint8_t[]){0xc9}, 1);
  CHECK(refused_free(p, n, 900, "SOF9") == CD_IMAGE_UNSUPPORTED, "arithmetic coding (SOF9)");
  free(p);
  p = patched(jpg, n, sof + 1, (const uint8_t[]){0xc3}, 1);
  CHECK(refused_free(p, n, 900, "SOF3") == CD_IMAGE_UNSUPPORTED, "lossless (SOF3)");
  free(p);
  p = patched(jpg, n, sof + 4, (const uint8_t[]){12}, 1);
  CHECK(refused_free(p, n, 900, "12-bit") == CD_IMAGE_UNSUPPORTED, "12-bit precision");
  free(p);
  p = patched(jpg, n, sof + 5, (const uint8_t[]){0, 0}, 2);
  CHECK(refused_free(p, n, 900, "height 0") == CD_IMAGE_CORRUPT, "height 0");
  free(p);
  /* a fatal libjpeg error raised deep inside it (a quantization table index of 15): out through the guard, all freed */
  size_t dqt = find(jpg, n, (const uint8_t[]){0xff, 0xdb, 0x00, 0x43, 0x00}, 5);
  CHECK(dqt != (size_t)-1, "no DQT in s420.jpg");
  p = patched(jpg, n, dqt + 4, (const uint8_t[]){0x0f}, 1);
  CHECK(resize_status(p, n, 300) == CD_IMAGE_CORRUPT, "a bad DQT index: a fatal libjpeg error");
  free(p);
  size_t pdqt = find(pjpg, qn, (const uint8_t[]){0xff, 0xdb, 0x00, 0x43, 0x00}, 5);
  p = patched(pjpg, qn, pdqt + 4, (const uint8_t[]){0x0f}, 1);
  CHECK(resize_status(p, qn, 300) == CD_IMAGE_CORRUPT, "a bad DQT index in a progressive JPEG");
  free(p);
  CHECK(resize_status(jpg, n / 2, 300) == CD_IMAGE_CORRUPT, "baseline JPEG cut in half");
  CHECK(resize_status(pjpg, qn / 2, 300) == CD_IMAGE_CORRUPT, "progressive JPEG cut in half");
  CHECK(resize_status(jpg, 20, 300) == CD_IMAGE_CORRUPT, "JPEG cut in its headers");
  /* PNG */
  CHECK(cd_image_probe(png, (uint32_t)pn, &info) == CD_IMAGE_OK && info.format == CD_IMAGE_PNG && info.width == 1203 && info.height == 802
        && info.shown_w == 1203 && info.orientation == 1, "PNG probe");
  p = patched(png, pn, 28, (const uint8_t[]){1}, 1);
  CHECK(refused_free(p, pn, 300, "interlaced") == CD_IMAGE_UNSUPPORTED, "interlaced PNG");
  free(p);
  p = patched(png, pn, 24, (const uint8_t[]){16}, 1);
  CHECK(refused_free(p, pn, 300, "16-bit") == CD_IMAGE_UNSUPPORTED, "16-bit PNG");
  free(p);
  p = patched(png, pn, 24, (const uint8_t[]){7}, 1);
  CHECK(refused_free(p, pn, 300, "depth 7") == CD_IMAGE_CORRUPT, "bit depth 7");
  free(p);
  p = patched(png, pn, 25, (const uint8_t[]){5}, 1);
  CHECK(refused_free(p, pn, 300, "type 5") == CD_IMAGE_CORRUPT, "colour type 5");
  free(p);
  p = patched(png, pn, 16, (const uint8_t[]){0, 0, 0, 0}, 4);
  CHECK(refused_free(p, pn, 300, "width 0") == CD_IMAGE_CORRUPT, "width 0");
  free(p);
  p = patched(png, pn, 16, (const uint8_t[]){0, 0, 0xff, 0xdd, 0, 0, 0, 1}, 8);
  CHECK(refused_free(p, pn, 300, "PNG side 65501") == CD_IMAGE_TOO_BIG, "PNG side of 65501");
  free(p);
  p = patched(png, pn, 16, (const uint8_t[]){0, 0, 0x7d, 0, 0, 0, 0x7d, 1}, 8);       /* 32000 x 32001 */
  CHECK(refused_free(p, pn, 300, "PNG over 64 MP") == CD_IMAGE_TOO_BIG, "PNG over CD_IMAGE_PIXELS_MAX");
  free(p);
  CHECK(resize_status(png, pn / 2, 300) == CD_IMAGE_CORRUPT, "PNG cut in half");
  CHECK(resize_status(png, 33, 300) == CD_IMAGE_CORRUPT, "PNG cut after IHDR");
  CHECK(resize_status(png, 30, 300) == CD_IMAGE_CORRUPT, "PNG cut inside IHDR");
  CHECK(resize_status(png, 7, 300) == CD_IMAGE_NOT_IMAGE, "seven bytes");
  size_t idat = find(png, pn, (const uint8_t *)"IDAT", 4);
  p = patched(png, pn, idat + 4, (const uint8_t[]){0, 0, 0, 0}, 4);
  CHECK(resize_status(p, pn, 300) == CD_IMAGE_CORRUPT, "a bad zlib header");
  free(p);
  /* the box: a status before anything is read or allocated */
  uint8_t *o = (uint8_t *)1; uint32_t w = 1, h = 1;
  host_reset(SIZE_MAX);
  CHECK(cd_image_resize(png, (uint32_t)pn, 0, 0, &o, &w, &h) == CD_IMAGE_BAD_BOX && !o && !w && !h && host_calls == 0, "a box of 0");
  CHECK(cd_image_resize(png, (uint32_t)pn, CD_IMAGE_BOX_MAX + 1, 10, &o, &w, &h) == CD_IMAGE_BAD_BOX, "a box over CD_IMAGE_BOX_MAX");
  CHECK(cd_image_resize(png, (uint32_t)pn, 0xffffffffu, 0xffffffffu, &o, &w, &h) == CD_IMAGE_BAD_BOX, "a huge box");
  CHECK(cd_image_resize(png, (uint32_t)pn, CD_IMAGE_BOX_MAX, CD_IMAGE_BOX_MAX, &o, &w, &h) == CD_IMAGE_OK && w == 1203 && h == 802, "the largest box");
  cd_free(o);
  CHECK(host_live == base, "the refusals left %zd bytes", (ssize_t)(host_live - base));
  free(jpg); free(png); free(pjpg);
}

/* ---------- a 1x1 palette PNG with a PLTE of n bytes (none when n < 0) ---------- */
static size_t make_pal_png(uint8_t *d, int n) {
  size_t k = 8;
  memcpy(d, "\x89PNG\r\n\x1a\n", 8);
  const uint8_t ihdr[] = {0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 1, 0, 0, 0, 1, 8, 3, 0, 0, 0, 0, 0, 0, 0};
  memcpy(d + k, ihdr, sizeof ihdr); k += sizeof ihdr;
  if (n >= 0) {
    d[k++] = 0; d[k++] = 0; d[k++] = (uint8_t)(n >> 8); d[k++] = (uint8_t)n;
    memcpy(d + k, "PLTE", 4); k += 4;
    for (int i = 0; i < n; i++) d[k++] = (uint8_t)(10 + i);
    k += 4;
  }
  /* zlib, a stored block of filter 0 and index 0, then its adler32 (of the two zero bytes: 0x00020001) */
  const uint8_t z[] = {0x78, 0x01, 0x01, 0x02, 0x00, 0xfd, 0xff, 0, 0, 0x00, 0x02, 0x00, 0x01};
  d[k++] = 0; d[k++] = 0; d[k++] = 0; d[k++] = sizeof z;
  memcpy(d + k, "IDAT", 4); k += 4;
  memcpy(d + k, z, sizeof z); k += sizeof z + 4;
  return k;
}
static void test_palette_sizes(void) {
  uint8_t d[1024];
  size_t base = host_live;
  uint8_t *o; uint32_t w, h;
  size_t k = make_pal_png(d, 3);
  CHECK(cd_image_resize(d, (uint32_t)k, 8, 8, &o, &w, &h) == CD_IMAGE_OK && o && o[0] == 10 && o[1] == 11 && o[2] == 12, "a one-entry palette");
  cd_free(o);
  k = make_pal_png(d, 768);
  CHECK(cd_image_resize(d, (uint32_t)k, 8, 8, &o, &w, &h) == CD_IMAGE_OK, "a 256-entry palette");
  cd_free(o);
  k = make_pal_png(d, 771);
  CHECK(cd_image_resize(d, (uint32_t)k, 8, 8, &o, &w, &h) == CD_IMAGE_CORRUPT, "257 palette entries");
  k = make_pal_png(d, 4);
  CHECK(cd_image_resize(d, (uint32_t)k, 8, 8, &o, &w, &h) == CD_IMAGE_CORRUPT, "a PLTE not a multiple of 3");
  k = make_pal_png(d, -1);
  CHECK(cd_image_resize(d, (uint32_t)k, 8, 8, &o, &w, &h) == CD_IMAGE_CORRUPT, "a palette image with no PLTE");
  CHECK(host_live == base, "leak of %zd bytes", (ssize_t)(host_live - base));
}

/* ---------- a heap that runs out and stays out (review item 4): at every allocation, NO_ROOM and nothing left ---------- */
static void test_no_room(const char *file, const char *what, uint32_t box) {
  size_t n; uint8_t *in = read_file(file, &n);
  size_t base = host_live;
  host_reset(SIZE_MAX);
  uint8_t *o; uint32_t w, h;
  CHECK(cd_image_resize(in, (uint32_t)n, box, box, &o, &w, &h) == CD_IMAGE_OK, "%s: no image with room", what);
  cd_free(o);
  size_t calls = host_calls, no_room = 0, steps = 0;
  /* the room grows a quarter at a time from 0, so the first block to find none moves through the whole run */
  for (size_t room = 0; room < (size_t)64 << 20; room = room ? room * 5 / 4 + 1 : 1) {
    host_reset(room);
    o = (uint8_t *)1;
    uint32_t s = cd_image_resize(in, (uint32_t)n, box, box, &o, &w, &h);
    steps++;
    if (s == CD_IMAGE_OK) { cd_free(o); CHECK(!host_full, "%s: OK although a block found no room (%zu bytes)", what, room); break; }
    CHECK(s == CD_IMAGE_NO_ROOM, "%s: with %zu bytes of room, status %s", what, room, sname(s));
    CHECK(!o, "%s: an image with NO_ROOM", what);
    if (s == CD_IMAGE_NO_ROOM) no_room++;
    CHECK(host_live == base, "%s: with %zu bytes of room, %zd bytes left allocated", what, room, (ssize_t)(host_live - base));
  }
  host_reset(SIZE_MAX);
  CHECK(no_room >= 10, "%s: only %zu rooms ended NO_ROOM", what, no_room);
  if (getenv("IMAGE_TEST_VERBOSE")) printf("  %s: %zu rooms, %zu NO_ROOM, %zu allocations in a run\n", what, steps, no_room, calls);
  free(in);
}

/* ---------- the first allocations of all: jpeg_create_decompress's, with room for none of them, or only some ---------- */
static void test_no_room_at_create(void) {
  size_t n; uint8_t *in = read_file("inputs/s420.jpg", &n);
  size_t base = host_live;
  uint8_t *o; uint32_t w, h;
  for (size_t room = 0; room < 64 * 1024; room += 16) {
    host_reset(room);
    uint32_t s = cd_image_resize(in, (uint32_t)n, 300, 300, &o, &w, &h);
    CHECK(s == CD_IMAGE_NO_ROOM, "with %zu bytes of room, %s", room, sname(s));
    CHECK(host_live == base, "jpeg_create_decompress out of room (%zu bytes) left %zd bytes (review item 4)", room, (ssize_t)(host_live - base));
    if (host_live != base) break;
  }
  host_reset(SIZE_MAX);
  free(in);
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  run_scaler();
  test_scaler_edges();
  test_target();
  test_exif();
  run_cases();
  test_refusals();
  test_palette_sizes();
  test_no_room_at_create();
  test_no_room("inputs/s420.jpg", "no room: baseline JPEG", 300);
  test_no_room("inputs/prog.jpg", "no room: progressive JPEG", 300);
  test_no_room("inputs/rgba.png", "no room: PNG", 300);
  printf("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
