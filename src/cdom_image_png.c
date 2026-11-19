/* cdom_image's PNG path: chunks walked in place, IDAT inflated by cdom's inflate (cdom_image_inflate.c) into a
 * bounded buffer, each row unfiltered where it lies in that buffer once no later match can read it, turned into RGBA
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

/* The next chunk at *at: its type and payload. 0 at the end of the data or on a chunk that runs past it. */
static int next_chunk(const uint8_t *p, uint32_t len, uint64_t *at, const uint8_t **type, const uint8_t **data, uint32_t *n) {
  if (*at + 12 > len) return 0;
  uint32_t l = be32(p + *at);
  if (*at + 12 + (uint64_t)l > len) return 0;
  *type = p + *at + 4; *data = p + *at + 8; *n = l;
  *at += 12 + (uint64_t)l;
  return 1;
}

static int iabs(int v) { return v < 0 ? -v : v; }
static uint8_t paeth(int a, int b, int c) {
  int pa = iabs(b - c), pb = iabs(a - c), pc = iabs(a + b - 2 * c);
  return (uint8_t)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

/* Undoes row filter f on cur (n bytes, bpp bytes a pixel) against prev (the row above, all zero for the first). */
static int unfilter(uint32_t f, uint8_t *cur, const uint8_t *prev, uint32_t n, uint32_t bpp) {
  switch (f) {
    case 0: break;
    case 1: for (uint32_t i = bpp; i < n; i++) cur[i] = (uint8_t)(cur[i] + cur[i - bpp]); break;
    case 2: for (uint32_t i = 0; i < n; i++) cur[i] = (uint8_t)(cur[i] + prev[i]); break;
    case 3:
      for (uint32_t i = 0; i < bpp && i < n; i++) cur[i] = (uint8_t)(cur[i] + (prev[i] >> 1));
      for (uint32_t i = bpp; i < n; i++) cur[i] = (uint8_t)(cur[i] + ((cur[i - bpp] + prev[i]) >> 1));
      break;
    case 4:
      for (uint32_t i = 0; i < bpp && i < n; i++) cur[i] = (uint8_t)(cur[i] + prev[i]);
      for (uint32_t i = bpp; i < n; i++) cur[i] = (uint8_t)(cur[i] + paeth(cur[i - bpp], prev[i], prev[i - bpp]));
      break;
    default: return 0;
  }
  return 1;
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
      for (uint32_t x = 0; x < w; x++, r += 3, out += 4) {
        if (f->key && r[0] == f->key_rgb[0] && r[1] == f->key_rgb[1] && r[2] == f->key_rgb[2]) out[0] = out[1] = out[2] = 255;
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
 * more, so that each hand-over to the rows has this much new (docs/image-png-speed.md, P3). */
#define PNG_CHUNK 262144u

typedef struct {
  cd_inflate inf;
  fmt f;
  cd_image_scaler sc;
  const uint8_t *p;           /* the file, and the next chunk to look at for the IDAT pieces */
  uint32_t len;
  uint64_t at;
  uint32_t h, stride, bpp;
  uint32_t row_at;            /* where the next row (its filter byte first) starts in the inflate's buffer */
  uint32_t done;              /* rows finished */
  uint8_t *zero;              /* the row above the first: zeros */
  uint8_t *px;                /* one RGBA row */
} pctx;

/* The next IDAT's payload: chunks of other types are passed over; the IEND, or a chunk that runs past the data,
 * ends the input. */
static int idat(void *c, const uint8_t **d, uint32_t *n) {
  pctx *x = c;
  const uint8_t *type;
  while (next_chunk(x->p, x->len, &x->at, &type, d, n)) {
    if (!memcmp(type, "IDAT", 4)) return 1;
    if (!memcmp(type, "IEND", 4)) return 0;
  }
  return 0;
}

/* The inflate's sink: each row that is whole and out of the window (no later match reads it) is unfiltered where
 * it is, in the inflate's buffer, against the row above it (kept there too), turned into RGBA and scaled; bytes
 * past the last row are dropped. Answers the bytes to drop: all before the row above the next. */
static int64_t rows(void *c, uint8_t *b, uint32_t n, int last) {
  pctx *x = c;
  uint32_t row = x->stride + 1, lim = last ? n : n > CD_INFLATE_WINDOW ? n - CD_INFLATE_WINDOW : 0;
  while (x->done < x->h && x->row_at + row <= lim) {
    uint8_t *cur = b + x->row_at;
    const uint8_t *prev = x->done ? cur - row : x->zero;
    if (!unfilter(cur[0], cur + 1, prev + 1, x->stride, x->bpp)) return -1;
    to_rgba(&x->f, cur + 1, x->px);
    cd_image_scaler_row(&x->sc, x->px);
    x->row_at += row;
    x->done++;
  }
  if (x->done == x->h) { x->row_at = 0; return lim; }
  uint32_t drop = x->done ? x->row_at - row : x->row_at;
  x->row_at -= drop;
  return drop;
}

uint32_t cd_image_png_resize(const uint8_t *p, uint32_t len, const cd_image_info *info, uint32_t dw, uint32_t dh,
                             uint8_t **rgba, uint32_t *ow, uint32_t *oh) {
  cd_image_info again;
  uint32_t depth, ctype;
  uint32_t st = read_ihdr(p, len, &again, &depth, &ctype);
  if (st != CD_IMAGE_OK) return st;
  const uint32_t w = info->width, h = info->height;
  uint32_t ch = channels(ctype);
  uint32_t stride = (uint32_t)(((uint64_t)w * ch * depth + 7) / 8);
  uint32_t bpp = ch * depth >= 8 ? ch * depth / 8 : 1;

  pctx *x = cd_alloc_try(sizeof *x);
  if (!x) return CD_IMAGE_NO_ROOM;
  memset(&x->f, 0, sizeof x->f);
  x->f.w = w; x->f.depth = depth; x->f.ctype = ctype;
  for (int i = 0; i < 256; i++) x->f.pal[4 * i + 3] = 255;
  x->p = p; x->len = len; x->h = h; x->stride = stride; x->bpp = bpp; x->row_at = 0; x->done = 0;
  /* one block: the zero row, the RGBA row, then the inflate's buffer */
  uint64_t cap = CD_INFLATE_WINDOW + 2 * ((uint64_t)stride + 1) + PNG_CHUNK;
  uint64_t room = (uint64_t)(stride + 1) + (uint64_t)w * 4 + cap;
  x->zero = room <= 0xffffffffu ? cd_alloc_try((uint32_t)room) : 0;
  if (!x->zero) { cd_free(x); return CD_IMAGE_NO_ROOM; }
  if (!cd_image_scaler_init(&x->sc, w, h, dw, dh, 1)) { cd_free(x->zero); cd_free(x); return CD_IMAGE_NO_ROOM; }
  memset(x->zero, 0, stride + 1);
  x->px = x->zero + (stride + 1);
  uint8_t *buf = x->px + (size_t)w * 4;

  /* the chunks before the first IDAT: PLTE and tRNS (after it, they are passed over) */
  int seen_plte = 0, saw_idat = 0;
  x->at = 8 + 25;
  const uint8_t *type, *data;
  uint32_t n;
  for (uint64_t at = x->at; next_chunk(p, len, &at, &type, &data, &n);) {
    if (!memcmp(type, "PLTE", 4)) {
      if (n % 3 || n > 768) { st = CD_IMAGE_CORRUPT; break; }
      for (uint32_t i = 0; i < n / 3; i++) memcpy(x->f.pal + 4 * i, data + 3 * i, 3);
      seen_plte = 1;
    } else if (!memcmp(type, "tRNS", 4)) {
      if (ctype == 3) for (uint32_t i = 0; i < n && i < 256; i++) x->f.pal[4 * i + 3] = data[i];
      else if (ctype == 0 && n == 2) { x->f.key = 1; x->f.key_g = be16(data); }
      else if (ctype == 2 && n == 6 && !data[0] && !data[2] && !data[4]) {   /* a key over 255 matches no 8-bit pixel */
        x->f.key = 1; x->f.key_rgb[0] = data[1]; x->f.key_rgb[1] = data[3]; x->f.key_rgb[2] = data[5];
      }
    } else if (!memcmp(type, "IDAT", 4)) {
      if (ctype == 3 && !seen_plte) st = CD_IMAGE_CORRUPT;
      saw_idat = 1;
      break;
    } else if (!memcmp(type, "IEND", 4)) break;
    x->at = at;                                    /* the IDAT pieces start at the first IDAT */
  }
  /* the image data: a stream cut short, refused or with a different adler32, or a row with no filter, is CORRUPT */
  if (st == CD_IMAGE_OK && (!saw_idat || cd_inflate_zlib(&x->inf, buf, (uint32_t)cap, idat, rows, x) != CD_INFLATE_OK))
    st = CD_IMAGE_CORRUPT;
  if (st == CD_IMAGE_OK) {
    uint8_t *o = cd_image_scaler_take(&x->sc);   /* 0 for a stream that ended short of the last row: CORRUPT */
    if (o) { *rgba = o; *ow = x->sc.ow; *oh = x->sc.oh; }
    else st = CD_IMAGE_CORRUPT;
  } else cd_image_scaler_free(&x->sc);
  cd_free(x->zero);
  cd_free(x);
  return st;
}
