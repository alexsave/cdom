/* cdom_image's PNG path: chunks walked as the file's bytes come (any split, cdom_image.h's fed decoder), IDAT pushed
 * into cdom's inflate (cdom_image_inflate.c) wherever a piece ends, inflated into a bounded buffer, each row unfiltered where it lies in that buffer once no later match can read it, turned into RGBA
 * (composited on white), then fed to the streaming scaler.
 * Refused: interlaced and 16-bit (UNSUPPORTED). Checked: the zlib stream's adler32 (a mismatch, or a stream that
 * stops before its checksum, is CORRUPT). Not checked: the chunk CRCs (a damaged chunk shows as damage, or fails
 * the adler32 when it is in the image data). */
#include <stddef.h>
#include <string.h>
#include "cdom_image_int.h"

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }

static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

/* The channels of a colour type, or 0 for an invalid one. */
static uint32_t channels(uint32_t ct) {
  switch (ct) { case 0: return 1; case 2: return 3; case 3: return 1; case 4: return 2; case 6: return 4; default: return 0; }
}

/* IHDR, with its checks, into info; *depth and *ctype for the decoder. */
static uint32_t read_ihdr(const uint8_t *p, uint32_t len, cd_image_info *info, uint32_t *depth, uint32_t *ctype) {
  info->format = CD_IMAGE_PNG;
  if (len < 8 || memcmp(p, sig, 8)) return CD_IMAGE_NOT_IMAGE;
  if (len < 8 + 25) return CD_IMAGE_CORRUPT;                          /* cut inside its IHDR */
  if (be32(p + 8) != 13 || memcmp(p + 12, "IHDR", 4)) return CD_IMAGE_CORRUPT;
  const uint8_t *d = p + 16;
  uint32_t w = be32(d), h = be32(d + 4), bd = d[8], ct = d[9];
  if (!w || !h || w > 0x7fffffffu || h > 0x7fffffffu) return CD_IMAGE_CORRUPT;
  if (d[10] != 0 || d[11] != 0 || d[12] > 1) return CD_IMAGE_CORRUPT;  /* compression, filter method, interlace */
  if (!channels(ct)) return CD_IMAGE_CORRUPT;
  int ok = ct == 0 ? (bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16)
         : ct == 3 ? (bd == 1 || bd == 2 || bd == 4 || bd == 8)
         : (bd == 8 || bd == 16);
  if (!ok) return CD_IMAGE_CORRUPT;
  info->width = w; info->height = h; info->orientation = 1; info->shown_w = w; info->shown_h = h;
  *depth = bd; *ctype = ct;
  if (d[12] || bd == 16) return CD_IMAGE_UNSUPPORTED;                  /* interlaced (Adam7), 16-bit */
  if (w > CD_IMAGE_SIDE_MAX || h > CD_IMAGE_SIDE_MAX || (uint64_t)w * h > CD_IMAGE_PIXELS_MAX) return CD_IMAGE_TOO_BIG;
  return CD_IMAGE_OK;
}

uint32_t cd_image_png_probe(const uint8_t *p, uint32_t len, cd_image_info *info) {
  uint32_t depth, ctype;
  return read_ihdr(p, len, info, &depth, &ctype);
}

/* One channel composited on white with alpha a. */
static uint8_t over(uint32_t c, uint32_t a) { return (uint8_t)((c * a + 255u * (255u - a) + 127u) / 255u); }

typedef struct {
  uint32_t w, depth, ctype;
  int key;                    /* a tRNS colour key is set (grey and RGB): a pixel of that value is transparent */
  uint32_t key_g;             /* grey: the sample, at the image's depth */
  uint8_t key_rgb[3];         /* RGB (8-bit): the colour */
  uint8_t pal[256 * 4];       /* palette RGBA; entries past PLTE are opaque black, as libpng's */
} fmt;

/* The unfiltered row as RGBA pixels. */
static void to_rgba(const fmt *f, const uint8_t *r, uint8_t *out) {
  uint32_t w = f->w;
  switch (f->ctype) {
    case 2:
      if (!f->key) { cd_image_rgb_to_rgba(r, out, w); break; }
      for (uint32_t x = 0; x < w; x++, r += 3, out += 4) {
        if (r[0] == f->key_rgb[0] && r[1] == f->key_rgb[1] && r[2] == f->key_rgb[2]) out[0] = out[1] = out[2] = 255;
        else { out[0] = r[0]; out[1] = r[1]; out[2] = r[2]; }
        out[3] = 255;
      }
      break;
    case 6:
      for (uint32_t x = 0; x < w; x++, r += 4, out += 4) {
        uint32_t a = r[3];
        if (a == 255) { out[0] = r[0]; out[1] = r[1]; out[2] = r[2]; }
        else { out[0] = over(r[0], a); out[1] = over(r[1], a); out[2] = over(r[2], a); }
        out[3] = 255;
      }
      break;
    case 4:
      for (uint32_t x = 0; x < w; x++, r += 2, out += 4) {
        uint8_t g = r[1] == 255 ? r[0] : over(r[0], r[1]);
        out[0] = out[1] = out[2] = g; out[3] = 255;
      }
      break;
    default: {                                          /* 0 (grey) and 3 (palette), 1 to 8 bits */
      uint32_t d = f->depth, mask = (1u << d) - 1, per = 8 / d;
      for (uint32_t x = 0; x < w; x++, out += 4) {
        uint32_t v = d == 8 ? r[x] : (uint32_t)(r[x / per] >> (8 - d - d * (x % per))) & mask;
        if (f->ctype == 3) {
          const uint8_t *e = f->pal + 4 * v;
          uint32_t a = e[3];
          if (a == 255) { out[0] = e[0]; out[1] = e[1]; out[2] = e[2]; }
          else { out[0] = over(e[0], a); out[1] = over(e[1], a); out[2] = over(e[2], a); }
        } else if (f->key && v == f->key_g) {
          out[0] = out[1] = out[2] = 255;
        } else {
          uint32_t g = d == 8 ? v : v * 255 / mask;
          out[0] = out[1] = out[2] = (uint8_t)g;
        }
        out[3] = 255;
      }
    }
  }
}

/* The inflate's output buffer: the window, two rows (the one above, unfiltered, and one being made) and this much
 * more, so that each hand-over to the rows has this much new (docs/image-png-speed.md, P3), or the whole image when
 * it is smaller (a small PNG holds about 32 KB, not 290: docs/image-png-speed.md, "png-fast-2"). A bench may set
 * another (-DPNG_CHUNK=...). */
#ifndef PNG_CHUNK
#define PNG_CHUNK 262144u
#endif
/* The room the inflate keeps free for the longest match and a wide copy's overshoot (its FAST_OUT, 290): the floor
 * less the window and its 1 KB, which cdom_image_inflate.c's static assertion ties to FAST_OUT. */
#define PNG_MATCH_ROOM (CD_INFLATE_CAP_MIN - CD_INFLATE_WINDOW - 1024u)

/* The chunk walk's states: a chunk's length and type (gathered across pieces), its payload, its CRC (counted past). */
enum { W_HEAD, W_DATA, W_CRC };
/* What a payload is to the walk: image data, a palette or transparency (before the first IDAT, gathered), or passed. */
enum { K_SKIP, K_IDAT, K_PLTE, K_TRNS };

typedef struct {
  cd_inflate inf;
  fmt f;
  cd_image_scaler sc;
  uint32_t h, stride, bpp;
  uint32_t row_at;            /* where the next row (its filter byte first) starts in the inflate's buffer */
  uint32_t done;              /* rows finished */
  uint8_t *zero;              /* the row above the first: zeros */
  uint8_t *px;                /* one RGBA row */
  /* the chunk walk, at the file's position at */
  uint64_t at;
  uint32_t state, kind;
  uint32_t left;              /* the payload's bytes not yet walked */
  uint32_t got;               /* the payload's bytes gathered (PLTE, tRNS) */
  uint32_t len;               /* the payload's length */
  uint8_t head[8], hn;        /* a chunk's length and type */
  uint8_t seen_plte, saw_idat;
  uint8_t gather[768];        /* PLTE (at most 768 bytes) or the first 256 of a tRNS */
} pctx;

/* The inflate's sink: each row that is whole and out of the window (no later match reads it) is unfiltered where
 * it is, in the inflate's buffer, against the row above it (kept there too), turned into RGBA and scaled (a run of up to
 * 4 Paeth rows among those is unfiltered at once, then scaled in order: cd_image_unfilter_run); bytes
 * past the last row are dropped. Answers the bytes to drop: all before the row above the next. */
static int64_t rows(void *c, uint8_t *b, uint32_t n, int last) {
  pctx *x = c;
  uint32_t row = x->stride + 1, lim = last ? n : n > CD_INFLATE_WINDOW ? n - CD_INFLATE_WINDOW : 0;
  while (x->done < x->h && x->row_at + row <= lim) {
    uint8_t *cur = b + x->row_at;
    const uint8_t *prev = x->done ? cur - row : x->zero;
    /* the rows whole and out of the window from here (at least 1): a run of Paeth rows among them is unfiltered at once */
    uint32_t avail = (lim - x->row_at) / row, left = x->h - x->done;
    uint32_t k = cd_image_unfilter_run(cur, row, prev + 1, x->stride, x->bpp, avail < left ? avail : left);
    if (!k) return -1;
    for (uint32_t j = 0; j < k; j++, cur += row) {   /* then to the scaler in order, one at a time */
      if (x->sc.step == 3) cd_image_scaler_row(&x->sc, cur + 1);   /* RGB as it is: the scaler reads 3 bytes a pixel */
      else { to_rgba(&x->f, cur + 1, x->px); cd_image_scaler_row(&x->sc, x->px); }
    }
    x->row_at += k * row;
    x->done += k;
  }
  if (x->done == x->h) { x->row_at = 0; return lim; }
  uint32_t drop = x->done ? x->row_at - row : x->row_at;
  x->row_at -= drop;
  return drop;
}

/* The chunks before the first IDAT take effect at their end, as libpng's: PLTE and tRNS (after it, they are passed
 * over). */
static void chunk_end(pctx *x) {
  const uint8_t *data = x->gather;
  uint32_t n = x->len, ctype = x->f.ctype;
  if (x->kind == K_PLTE) {
    for (uint32_t i = 0; i < n / 3; i++) memcpy(x->f.pal + 4 * i, data + 3 * i, 3);
    x->seen_plte = 1;
  } else if (x->kind == K_TRNS) {
    if (ctype == 3) for (uint32_t i = 0; i < n && i < 256; i++) x->f.pal[4 * i + 3] = data[i];
    else if (ctype == 0 && n == 2) { x->f.key = 1; x->f.key_g = be16(data); }
    else if (ctype == 2 && n == 6 && !data[0] && !data[2] && !data[4]) {   /* a key over 255 matches no 8-bit pixel */
      x->f.key = 1; x->f.key_rgb[0] = data[1]; x->f.key_rgb[1] = data[3]; x->f.key_rgb[2] = data[5];
    }
  }
}

/* The first IDAT: the rows' block and the scaler. One block: the zero row, the RGBA row (unless the rows are read as
 * RGB), then the inflate's buffer: the window, two rows and PNG_CHUNK; or, when it is smaller, the whole image (never
 * handed over before the end) or the window and two rows (what the sink keeps once a stream makes more than the
 * image), whichever is larger, and the room the inflate keeps for the longest match (docs/image-png-speed.md, "The
 * small PNG's buffer"); never under the inflate's floor. 8-bit RGB with no colour key, in the scalar build: the rows go to the scaler as they are,
 * with no RGBA row between (none is allocated). The SIMD build keeps the RGBA row: its RGB to RGBA and across loops in
 * SIMD are faster than the scalar RGB read (docs/image-png-speed.md, "With image-simd's loops"). */
static uint32_t first_idat(cd_image_dec *d, pctx *x) {
  if (x->f.ctype == 3 && !x->seen_plte) return CD_IMAGE_CORRUPT;
  x->saw_idat = 1;
  int rgb_read = !CDOM_IMAGE_SIMD && x->f.ctype == 2 && x->f.depth == 8 && !x->f.key;
  uint64_t row = (uint64_t)x->stride + 1, all = row * x->h;
  uint64_t keep = CD_INFLATE_WINDOW + 2 * row;
  uint64_t cap = (all > keep ? all : keep) + PNG_MATCH_ROOM;
  if (cap > keep + PNG_CHUNK) cap = keep + PNG_CHUNK;
  if (cap < CD_INFLATE_CAP_MIN) cap = CD_INFLATE_CAP_MIN;
  uint64_t px_n = rgb_read ? 0 : (uint64_t)x->f.w * 4;
  uint64_t room = row + px_n + cap;
  x->zero = room <= 0xffffffffu ? cd_alloc_try((uint32_t)room) : 0;
  if (!x->zero) return CD_IMAGE_NO_ROOM;
  uint32_t dw, dh;
  cd_image_target(d->info.width, d->info.height, 1, d->box_w, d->box_h, &dw, &dh);
  if (!cd_image_scaler_init(&x->sc, d->info.width, d->info.height, dw, dh, 1)) return CD_IMAGE_NO_ROOM;
  memset(x->zero, 0, x->stride + 1);
  x->px = x->zero + row;
  if (rgb_read) x->sc.step = 3;
  cd_inflate_begin(&x->inf, x->px + px_n, (uint32_t)cap, rows, x);
  return CD_IMAGE_MORE;
}

/* The inflate's answer, once final: a stream cut short, refused or with a different adler32, or a row with no filter,
 * is CORRUPT; so is one that ended short of the last row (the scaler has no image then). */
static uint32_t finish(cd_image_dec *d, pctx *x, uint32_t st) {
  if (st != CD_INFLATE_OK) return CD_IMAGE_CORRUPT;
  uint8_t *o = cd_image_scaler_take(&x->sc);
  if (!o) return CD_IMAGE_CORRUPT;
  d->rgba = o; d->w = x->sc.ow; d->h = x->sc.oh;
  return CD_IMAGE_OK;
}

/* The end of the data: the IEND, a chunk that runs past the file, or the file's end. */
static uint32_t data_end(cd_image_dec *d, pctx *x) {
  if (!x->saw_idat) return CD_IMAGE_CORRUPT;
  return finish(d, x, cd_inflate_put(&x->inf, 0, 0, 1));
}

/* The walk over p[0 .. n), the file's bytes from base on. */
static uint32_t walk(cd_image_dec *d, pctx *x, const uint8_t *p, uint32_t n, uint64_t base) {
  const uint64_t size = d->size, end = base + n;
  for (;;) {
    if (x->state == W_HEAD) {
      if (x->hn == 0 && x->at + 12 > size) return data_end(d, x);   /* no room left for a chunk */
      if (x->at >= end) return CD_IMAGE_MORE;
      uint32_t k = (uint32_t)(end - x->at), want = 8u - x->hn;
      if (k > want) k = want;
      memcpy(x->head + x->hn, p + (x->at - base), k);
      x->hn = (uint8_t)(x->hn + k); x->at += k;
      if (x->hn < 8) return CD_IMAGE_MORE;
      x->hn = 0;
      uint32_t l = be32(x->head);
      const uint8_t *type = x->head + 4;
      if (x->at - 8 + 12 + (uint64_t)l > size) return data_end(d, x);   /* a chunk that runs past the file */
      x->len = l; x->left = l; x->got = 0; x->kind = K_SKIP;
      if (!memcmp(type, "IDAT", 4)) {
        if (!x->saw_idat) { uint32_t st = first_idat(d, x); if (st != CD_IMAGE_MORE) return st; }
        x->kind = K_IDAT;
      } else if (!memcmp(type, "IEND", 4)) {
        return data_end(d, x);
      } else if (!x->saw_idat && !memcmp(type, "PLTE", 4)) {
        if (l % 3 || l > 768) return CD_IMAGE_CORRUPT;
        x->kind = K_PLTE;
      } else if (!x->saw_idat && !memcmp(type, "tRNS", 4)) {
        x->kind = K_TRNS;
      }
      x->state = W_DATA;
    } else if (x->state == W_DATA) {
      if (x->left) {
        if (x->at >= end) return CD_IMAGE_MORE;
        uint32_t k = (uint32_t)(end - x->at);
        if (k > x->left) k = x->left;
        const uint8_t *q = p + (x->at - base);
        if (x->kind == K_IDAT) {
          uint32_t st = cd_inflate_put(&x->inf, q, k, 0);
          if (st != CD_INFLATE_MORE) return finish(d, x, st);
        } else if (x->kind != K_SKIP) {
          uint32_t lim = x->kind == K_PLTE ? 768u : 256u, c = x->got < lim ? lim - x->got : 0;
          if (c > k) c = k;
          memcpy(x->gather + x->got, q, c);
          x->got += c;
        }
        x->at += k; x->left -= k;
        if (x->left) return CD_IMAGE_MORE;
      }
      chunk_end(x);
      x->state = W_CRC;
    } else {                                     /* the CRC: counted past, not checked */
      x->at += 4;
      x->state = W_HEAD;
    }
  }
}

uint32_t cd_image_png_put(cd_image_dec *d, const uint8_t *p, uint32_t n) {
  pctx *x = d->fmt;
  uint64_t base = d->taken - n;
  if (!x) {
    /* the signature and IHDR, the first 33 bytes, gathered at the room's front (or read where they lie) */
    const uint8_t *h = p;
    uint32_t hn = n;
    if (d->buf) { d->used += n; h = d->buf; hn = d->used; }
    if (hn < (d->size < 33 ? d->size : 33)) return CD_IMAGE_MORE;
    uint32_t depth, ctype;
    uint32_t st = read_ihdr(h, hn < 33 ? hn : 33, &d->info, &depth, &ctype);
    if (st != CD_IMAGE_OK) return st;
    const uint32_t w = d->info.width, ht = d->info.height;
    uint32_t ch = channels(ctype);
    x = cd_alloc_try(sizeof *x);
    if (!x) return CD_IMAGE_NO_ROOM;
    memset(x, 0, sizeof *x);
    d->fmt = x;
    x->f.w = w; x->f.depth = depth; x->f.ctype = ctype;
    for (int i = 0; i < 256; i++) x->f.pal[4 * i + 3] = 255;
    x->h = ht;
    x->stride = (uint32_t)(((uint64_t)w * ch * depth + 7) / 8);
    x->bpp = ch * depth >= 8 ? ch * depth / 8 : 1;
    x->at = 8 + 25;
    x->state = W_HEAD;
    if (d->buf) { p = d->buf; n = d->used; d->used = 0; base = 0; }
  }
  return walk(d, x, p, n, base);
}

void cd_image_png_end(cd_image_dec *d) {
  pctx *x = d->fmt;
  if (!x) return;
  cd_image_scaler_free(&x->sc);
  cd_free(x->zero);
  cd_free(x);
  d->fmt = 0;
}
