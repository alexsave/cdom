/* cdom_image's PNG path: chunks walked in place, IDAT inflated by miniz's tinfl through its 32 KB wrapping window,
 * each row unfiltered and turned into RGBA (composited on white), then fed to the streaming scaler.
 * Refused: interlaced and 16-bit (UNSUPPORTED). Checked: the zlib stream's adler32 (a mismatch, or a stream that
 * stops before its checksum, is CORRUPT). Not checked: the chunk CRCs (a damaged chunk shows as damage, or fails
 * the adler32 when it is in the image data). */
#include <stddef.h>
#include <string.h>
#include "cdom_image_miniz.h"

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

typedef struct {
  tinfl_decompressor inf;
  uint8_t dict[TINFL_LZ_DICT_SIZE];
  fmt f;
  cd_image_scaler sc;
  uint8_t *rows;              /* two rows of stride+1 bytes (filter byte first), then one RGBA row */
} pctx;

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
  uint64_t room = (uint64_t)(stride + 1) * 2 + (uint64_t)w * 4;
  x->rows = room <= 0xffffffffu ? cd_alloc_try((uint32_t)room) : 0;
  if (!x->rows) { cd_free(x); return CD_IMAGE_NO_ROOM; }
  if (!cd_image_scaler_init(&x->sc, w, h, dw, dh, 1)) { cd_free(x->rows); cd_free(x); return CD_IMAGE_NO_ROOM; }
  uint8_t *cur = x->rows, *prev = x->rows + (stride + 1), *px = x->rows + 2 * (size_t)(stride + 1);
  memset(prev, 0, stride + 1);

  tinfl_init(&x->inf);
  uint32_t have = 0;          /* bytes of the current row (with its filter byte) filled so far */
  uint32_t done = 0;          /* rows finished */
  uint32_t dict_ofs = 0;
  int saw_idat = 0, seen_plte = 0, ended = 0;   /* ended: the zlib stream finished, its adler32 matched */
  uint64_t at = 8 + 25;
  const uint8_t *type, *data;
  uint32_t n;
  while (!ended && st == CD_IMAGE_OK && next_chunk(p, len, &at, &type, &data, &n)) {
    if (!memcmp(type, "PLTE", 4) && !saw_idat) {
      if (n % 3 || n > 768) { st = CD_IMAGE_CORRUPT; break; }
      for (uint32_t i = 0; i < n / 3; i++) memcpy(x->f.pal + 4 * i, data + 3 * i, 3);
      seen_plte = 1;
    } else if (!memcmp(type, "tRNS", 4) && !saw_idat) {
      if (ctype == 3) for (uint32_t i = 0; i < n && i < 256; i++) x->f.pal[4 * i + 3] = data[i];
      else if (ctype == 0 && n == 2) { x->f.key = 1; x->f.key_g = be16(data); }
      else if (ctype == 2 && n == 6 && !data[0] && !data[2] && !data[4]) {   /* a key over 255 matches no 8-bit pixel */
        x->f.key = 1; x->f.key_rgb[0] = data[1]; x->f.key_rgb[1] = data[3]; x->f.key_rgb[2] = data[5];
      }
    } else if (!memcmp(type, "IDAT", 4)) {
      if (ctype == 3 && !seen_plte) { st = CD_IMAGE_CORRUPT; break; }
      saw_idat = 1;
      size_t in_pos = 0;
      for (;;) {
        size_t in_n = n - in_pos, out_n = TINFL_LZ_DICT_SIZE - dict_ofs;
        tinfl_status ts = tinfl_decompress(&x->inf, data + in_pos, &in_n, x->dict, x->dict + dict_ofs, &out_n,
                                           TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT);
        in_pos += in_n;
        /* the bytes just made, x->dict[dict_ofs .. dict_ofs + out_n), into rows (any past the last row are dropped) */
        const uint8_t *s = x->dict + dict_ofs;
        size_t left = out_n;
        while (left && done < h) {
          size_t take = (stride + 1) - have;
          if (take > left) take = left;
          memcpy(cur + have, s, take);
          have += (uint32_t)take; s += take; left -= take;
          if (have == stride + 1) {
            if (!cd_image_unfilter(cur[0], cur + 1, prev + 1, stride, bpp)) { st = CD_IMAGE_CORRUPT; break; }
            to_rgba(&x->f, cur + 1, px);
            cd_image_scaler_row(&x->sc, px);
            uint8_t *t = cur; cur = prev; prev = t;
            have = 0;
            done++;
          }
        }
        dict_ofs = (dict_ofs + (uint32_t)out_n) & (TINFL_LZ_DICT_SIZE - 1);
        if (st != CD_IMAGE_OK) break;
        if (ts < TINFL_STATUS_DONE) { st = CD_IMAGE_CORRUPT; break; }   /* a bad stream, or its adler32 differs */
        if (ts == TINFL_STATUS_DONE) { ended = 1; break; }
        if (ts == TINFL_STATUS_NEEDS_MORE_INPUT && in_pos >= n) break;   /* this chunk is used up: on to the next */
      }
    } else if (!memcmp(type, "IEND", 4)) break;
  }
  if (st == CD_IMAGE_OK && !ended) st = CD_IMAGE_CORRUPT;   /* the data ended before the stream's checksum */
  if (st == CD_IMAGE_OK) {
    uint8_t *o = cd_image_scaler_take(&x->sc);   /* 0 for a stream that ended short of the last row: CORRUPT */
    if (o) { *rgba = o; *ow = x->sc.ow; *oh = x->sc.oh; }
    else st = CD_IMAGE_CORRUPT;
  } else cd_image_scaler_free(&x->sc);
  cd_free(x->rows);
  cd_free(x);
  return st;
}
