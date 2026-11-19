/* cdom_image: the entry points, the EXIF orientation, and the streaming scaler (cdom_image.h). The formats are
 * cdom_image_jpeg.c and cdom_image_png.c; libjpeg's error exit is cdom_image_sjlj.c. */
#include <stddef.h>
#include <string.h>
#include "cdom_image_int.h"

static int is_png(const uint8_t *p, uint32_t len) { return len >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8); }
static int is_jpeg(const uint8_t *p, uint32_t len) { return len >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff; }

uint32_t cd_image_probe(const uint8_t *p, uint32_t len, cd_image_info *info) {
  memset(info, 0, sizeof *info);
  info->orientation = 1;
  if (is_jpeg(p, len)) return cd_image_jpeg_probe(p, len, info);
  if (is_png(p, len)) return cd_image_png_probe(p, len, info);
  return CD_IMAGE_NOT_IMAGE;
}

uint32_t cd_image_resize(const uint8_t *p, uint32_t len, uint32_t max_w, uint32_t max_h, uint8_t **rgba,
                         uint32_t *w, uint32_t *h) {
  *rgba = 0; *w = 0; *h = 0;
  if (!max_w || !max_h || max_w > CD_IMAGE_BOX_MAX || max_h > CD_IMAGE_BOX_MAX) return CD_IMAGE_BAD_BOX;
  cd_image_info info;
  uint32_t st = cd_image_probe(p, len, &info);
  if (st != CD_IMAGE_OK) return st;
  uint32_t dw, dh;
  cd_image_target(info.width, info.height, info.orientation, max_w, max_h, &dw, &dh);
  return info.format == CD_IMAGE_JPEG ? cd_image_jpeg_resize(p, len, &info, dw, dh, rgba, w, h)
                                      : cd_image_png_resize(p, len, &info, dw, dh, rgba, w, h);
}

/* ---- the size ---- */

/* The largest size inside bw x bh with the aspect of w x h, never larger than w x h: the side the box binds is the
 * box's, and the other is rounded to nearest (halves up), at least 1. Shown sizes: the caller swaps for 5 to 8. */
static void fit_box(uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, uint32_t *dw, uint32_t *dh) {
  if (w <= bw && h <= bh) { *dw = w; *dh = h; return; }
  if ((uint64_t)w * bh >= (uint64_t)h * bw) {         /* the width binds: w/h >= bw/bh */
    uint64_t v = ((uint64_t)h * bw * 2 + w) / ((uint64_t)w * 2);
    *dw = bw; *dh = v ? (uint32_t)v : 1;
  } else {
    uint64_t v = ((uint64_t)w * bh * 2 + h) / ((uint64_t)h * 2);
    *dh = bh; *dw = v ? (uint32_t)v : 1;
  }
}

void cd_image_target(uint32_t sw, uint32_t sh, uint32_t o, uint32_t bw, uint32_t bh, uint32_t *dw, uint32_t *dh) {
  if (o >= 5 && o <= 8) fit_box(sw, sh, bh, bw, dw, dh);   /* the shown image is sh x sw: its box, as stored */
  else fit_box(sw, sh, bw, bh, dw, dh);
}

/* ---- EXIF ---- */

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t rd16(const uint8_t *p, int le) { return le ? (uint32_t)p[1] << 8 | p[0] : be16(p); }
static uint32_t rd32(const uint8_t *p, int le) {
  return le ? (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]
            : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* The segments before the scan, each FF, marker, 2-byte length (counting itself); the first APP1 that starts
 * "Exif\0\0" (one that does not, such as XMP's, is passed over) holds a TIFF header and IFD0, whose entry 0x0112
 * (SHORT) is the orientation. Every offset is checked against the segment's room, in 64 bits. */
uint32_t cd_image_orientation(const uint8_t *p, uint32_t len) {
  if (len < 4 || p[0] != 0xff || p[1] != 0xd8) return 1;
  uint64_t at = 2;
  while (at + 4 <= len && p[at] == 0xff) {
    uint32_t marker = p[at + 1], seg = be16(p + at + 2);
    if (marker == 0xff) { at++; continue; }                 /* fill bytes */
    if (marker == 0xda || marker == 0xd9 || seg < 2) break; /* the scan, or the end: no more headers */
    if (marker == 0xe1 && seg >= 16 && at + 2 + seg <= len && !memcmp(p + at + 4, "Exif\0\0", 6)) {
      const uint8_t *t = p + at + 10;                       /* the TIFF header */
      uint64_t room = seg - 8;                              /* the bytes from t to the end of the segment */
      int le = t[0] == 'I';
      if ((t[0] != 'I' && t[0] != 'M') || t[1] != t[0] || rd16(t + 2, le) != 42) return 1;
      uint64_t ifd = rd32(t + 4, le);
      if (ifd + 2 > room) return 1;
      uint32_t count = rd16(t + ifd, le);
      for (uint32_t i = 0; i < count && ifd + 2 + 12 * (uint64_t)(i + 1) <= room; i++) {
        const uint8_t *e = t + ifd + 2 + 12 * (uint64_t)i; /* tag, type, count, value */
        if (rd16(e, le) == 0x0112 && rd16(e + 2, le) == 3) {
          uint32_t o = rd16(e + 8, le);
          return o >= 1 && o <= 8 ? o : 1;
        }
      }
      return 1;
    }
    at += 2 + (uint64_t)seg;
  }
  return 1;
}

/* ---- the scaler ---- */

int cd_image_scaler_init(cd_image_scaler *s, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh, uint32_t o) {
  memset(s, 0, sizeof *s);
  if (o < 1 || o > 8) o = 1;
  s->sw = sw; s->sh = sh; s->dw = dw; s->dh = dh; s->o = o;
  s->ow = o >= 5 ? dh : dw;
  s->oh = o >= 5 ? dw : dh;
  uint64_t out = (uint64_t)dw * dh * 4;
  if (out > 0xffffffffu) return 0;
  s->out = cd_alloc_try((uint32_t)out);
  s->acc = cd_alloc_try(dw * 3 * (uint32_t)sizeof(uint64_t) + (CDOM_IMAGE_SIMD ? dw * 3 : 0));
  s->hrow = cd_alloc_try(dw * 3 * (uint32_t)sizeof(uint32_t));
  if (!s->out || !s->acc || !s->hrow) { cd_image_scaler_free(s); return 0; }
  memset(s->acc, 0, (size_t)dw * 3 * sizeof(uint64_t));
  return 1;
}

void cd_image_scaler_free(cd_image_scaler *s) {
  cd_free(s->out); cd_free(s->acc); cd_free(s->hrow);
  s->out = 0; s->acc = 0; s->hrow = 0;
}

/* Exact area average, in integers. Scale both axes so that source pixel c covers [c*dw, (c+1)*dw) and output pixel x
 * covers [x*sw, (x+1)*sw): the overlap of the two is an integer, and the weights of one output pixel sum to sw
 * (across) and sh (down). A pixel is the weighted sum divided by sw*sh, rounded to nearest with halves up, in
 * integers: (2a + t) / 2t. The across sum is at most 65535*255 and fits 32 bits; the down sum, at most
 * 65535*65535*255 (under 2^40), fits 64 with room for the doubling. */

/* Output row y, finished: the sums in acc, divided, stored where the orientation puts them (an offset and a step
 * in bytes; the offset is only made a pointer while it is inside the image). */
static void emit(cd_image_scaler *s, uint32_t y) {
  const uint32_t dw = s->dw, dh = s->dh, ow = s->ow, oh = s->oh;
  const uint64_t t = (uint64_t)s->sw * s->sh;
#if !CDOM_IMAGE_SIMD
  const uint64_t t2 = t * 2;
#endif
  ptrdiff_t at, step;
  switch (s->o) {
    case 2: at = ((ptrdiff_t)y * ow + dw - 1) * 4; step = -4; break;
    case 3: at = ((ptrdiff_t)(dh - 1 - y) * ow + dw - 1) * 4; step = -4; break;
    case 4: at = (ptrdiff_t)(dh - 1 - y) * ow * 4; step = 4; break;
    case 5: at = (ptrdiff_t)y * 4; step = (ptrdiff_t)ow * 4; break;
    case 6: at = (ptrdiff_t)(ow - 1 - y) * 4; step = (ptrdiff_t)ow * 4; break;
    case 7: at = ((ptrdiff_t)(oh - 1) * ow + (ow - 1 - y)) * 4; step = -(ptrdiff_t)ow * 4; break;
    case 8: at = ((ptrdiff_t)(oh - 1) * ow + y) * 4; step = -(ptrdiff_t)ow * 4; break;
    default: at = (ptrdiff_t)y * ow * 4; step = 4; break;
  }
#if CDOM_IMAGE_SIMD
  const uint8_t *q = (const uint8_t *)(s->acc + (size_t)dw * 3);
  cd_image_scaler_div((uint8_t *)q, s->acc, dw * 3, t);
  for (uint32_t x = 0; x < dw; x++, at += step, q += 3) {
    uint8_t *d = s->out + at;
    d[0] = q[0]; d[1] = q[1]; d[2] = q[2]; d[3] = 255;
  }
#else
  const uint64_t *a = s->acc;
  for (uint32_t x = 0; x < dw; x++, at += step, a += 3) {
    uint8_t *d = s->out + at;
    for (int k = 0; k < 3; k++) {
      uint64_t v = (a[k] * 2 + t) / t2;
      d[k] = v > 255 ? 255 : (uint8_t)v;
    }
    d[3] = 255;
  }
#endif
  memset(s->acc, 0, (size_t)dw * 3 * sizeof(uint64_t));
}

void cd_image_scaler_row(cd_image_scaler *s, const uint8_t *px) {
  if (s->row >= s->sh) return;
  s->row++;
  const uint32_t sw = s->sw, dw = s->dw, dh = s->dh, sh = s->sh;
  uint32_t *h = s->hrow;
  cd_image_scaler_across(h, px, sw, dw);
  /* down: this source row covers [row*dh, (row+1)*dh) in units of 1/dh; output row oy ends at (oy+1)*sh */
  uint64_t rend = (uint64_t)s->row * dh;
  while (s->ypos < rend) {
    uint64_t yend = (uint64_t)(s->oy + 1) * sh;
    uint64_t b = rend < yend ? rend : yend;
    uint64_t wgt = b - s->ypos;
    cd_image_scaler_down(s->acc, h, wgt, dw * 3);
    s->ypos = b;
    if (b == yend) { emit(s, s->oy); s->oy++; }
  }
}

uint8_t *cd_image_scaler_take(cd_image_scaler *s) {
  uint8_t *o = s->oy == s->dh ? s->out : 0;
  if (o) s->out = 0;
  cd_image_scaler_free(s);
  return o;
}
