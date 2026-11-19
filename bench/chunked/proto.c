/* A throwaway prototype for docs/chunked-file-read-design.md: the image decoders fed a file in pieces, natively.
 * Three ways, compared to the byte and timed:
 *   whole   today's cd_image_resize over the whole file in memory;
 *   pull N  a synchronous read callback, read(ctx, at, dst, want), pieces of N bytes (the route inside a worker);
 *   push N  a resumable decoder that suspends when it runs out and is handed the next piece (the main-thread route):
 *           libjpeg's suspending source, and for PNG a chunk walker over tinfl.
 * Never shipped: the JPEG and PNG drivers below copy the module's own (src/cdom_image_jpeg.c, src/cdom_image_png.c)
 * with the source changed, to answer the design's questions before the module is touched.
 * Usage: proto check FILE...        every mode at every piece size equal to whole, and the failure cases
 *        proto time FILE REPS       medians of whole, pull and push at 64 KiB and 256 KiB, and each one's peak */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cdom_image_int.h"
#include "cdom_image_miniz.h"
#include "host_alloc.h"
#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jerror.h"

#define UNREADABLE 7u
static long last_warnings;   /* libjpeg's warnings in the last JPEG decode: a skipped segment fed as data shows here */   /* the design's new status: a read failed or came back short (the file changed) */

/* ---- the file: bytes the browser holds; a read copies a piece of it into C memory, as the shim would ---- */

typedef struct {
  const uint8_t *b;
  uint32_t size;       /* the size the file claims (File.size) */
  uint32_t real;       /* the bytes a read can give: less than size for "a file shorter than claimed" */
  uint32_t fail_at;    /* a read at or past this offset fails (0: none) */
  uint64_t reads;
} file;

typedef uint32_t (*read_fn)(void *ctx, uint32_t at, uint8_t *dst, uint32_t want);

static uint32_t file_read(void *ctx, uint32_t at, uint8_t *dst, uint32_t want) {
  file *f = ctx;
  f->reads++;
  if (f->fail_at && at + want > f->fail_at) return 0;
  if (at >= f->real) return 0;
  uint32_t n = f->real - at < want ? f->real - at : want;
  memcpy(dst, f->b + at, n);
  return n;
}

/* The head, read and probed: 64 KiB first, doubled while the probe runs off its end (a prototype's shortcut; the
 * design walks the markers by offset instead). */
static uint32_t probe_head(read_fn rd, void *rc, uint32_t size, cd_image_info *info) {
  for (uint32_t k = 65536;; k *= 2) {
    if (k > size) k = size;
    uint8_t *h = cd_alloc_try(k ? k : 1);
    if (!h) return CD_IMAGE_NO_ROOM;
    uint32_t n = rd(rc, 0, h, k);
    if (n != k) { cd_free(h); return UNREADABLE; }
    uint32_t st = cd_image_probe(h, k, info);
    cd_free(h);
    if (st != CD_IMAGE_CORRUPT || k == size) return st;
  }
}

/* ---- JPEG ---- */

static uint32_t pick_num(uint32_t w, uint32_t h, uint32_t dw, uint32_t dh) {
  for (uint32_t n = 1; n < 8; n++) {
    uint64_t cw = ((uint64_t)w * n + 7) / 8, ch = ((uint64_t)h * n + 7) / 8;
    if (cw >= dw && ch >= dh) return n;
  }
  return 8;
}

typedef struct {
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr err;
  struct jpeg_source_mgr src;
  struct jpeg_progress_mgr prog;
  void *guard;
  cd_image_info info;
  uint32_t dw, dh;
  cd_image_scaler sc;
  uint8_t *row;
  uint32_t status;
  uint8_t ran_dry, too_many_scans, unreadable;
  read_fn rd;
  void *rc;
  uint32_t size, at, piece, cap;
  uint8_t *buf;
  int stage;           /* push: 0 nothing yet, 1 the header, 2 start, 3 rows, 4 done */
  int eof;             /* push: every byte of the file has been handed over */
  uint64_t skip;       /* push: bytes libjpeg skipped past the end of what it had */
} jx;

static void err_exit(j_common_ptr c) { cd_image_throw(((jx *)c->client_data)->guard); }
static void err_emit(j_common_ptr c, int level) { if (level < 0) c->err->num_warnings++; }
static void err_out(j_common_ptr c) { (void)c; }
static void err_fmt(j_common_ptr c, char *buf) { (void)c; buf[0] = 0; }
static void err_reset(j_common_ptr c) { c->err->msg_code = 0; c->err->num_warnings = 0; }
static void progress(j_common_ptr c) {
  jx *x = c->client_data;
  if ((uint32_t)x->cinfo.input_scan_number > CD_IMAGE_SCANS_MAX) { x->too_many_scans = 1; cd_image_throw(x->guard); }
}
static const JOCTET eoi[2] = {0xff, JPEG_EOI};
static void src_init(j_decompress_ptr c) { (void)c; }
static void src_term(j_decompress_ptr c) { (void)c; }

/* pull: each fill reads the next piece; a skip past the buffer moves the offset without reading */
static boolean pull_fill(j_decompress_ptr c) {
  jx *x = c->client_data;
  uint32_t want = x->size - x->at < x->piece ? x->size - x->at : x->piece;
  if (!want) { x->ran_dry = 1; x->src.next_input_byte = eoi; x->src.bytes_in_buffer = 2; return TRUE; }
  uint32_t n = x->rd(x->rc, x->at, x->buf, want);
  if (n != want) { x->unreadable = 1; cd_image_throw(x->guard); }
  x->at += n;
  x->src.next_input_byte = x->buf;
  x->src.bytes_in_buffer = n;
  return TRUE;
}
static void pull_skip(j_decompress_ptr c, long n) {
  jx *x = c->client_data;
  if (n <= 0) return;
  if ((size_t)n <= x->src.bytes_in_buffer) { x->src.next_input_byte += n; x->src.bytes_in_buffer -= (size_t)n; return; }
  uint64_t past = (uint64_t)n - x->src.bytes_in_buffer;
  x->src.next_input_byte += x->src.bytes_in_buffer;
  x->src.bytes_in_buffer = 0;
  x->at = x->size - x->at < past ? x->size : x->at + (uint32_t)past;
}

/* push: no bytes is a suspension, unless the file is all in (then the end, as the memory source does) */
static boolean push_fill(j_decompress_ptr c) {
  jx *x = c->client_data;
  if (!x->eof) return FALSE;
  x->ran_dry = 1; x->src.next_input_byte = eoi; x->src.bytes_in_buffer = 2;
  return TRUE;
}
static void push_skip(j_decompress_ptr c, long n) {
  jx *x = c->client_data;
  if (n <= 0) return;
  if ((size_t)n <= x->src.bytes_in_buffer) { x->src.next_input_byte += n; x->src.bytes_in_buffer -= (size_t)n; return; }
  x->skip += (uint64_t)n - x->src.bytes_in_buffer;
  x->src.next_input_byte += x->src.bytes_in_buffer;
  x->src.bytes_in_buffer = 0;
}

/* After the header: the scale, the scaler and the row (shared by both ways). 0 when the decode ends here. */
static int after_header(jx *x) {
  struct jpeg_decompress_struct *c = &x->cinfo;
  c->out_color_space = JCS_EXT_RGBA;
  c->do_fancy_upsampling = TRUE;
  c->do_block_smoothing = FALSE;
  c->dct_method = JDCT_ISLOW;
  c->scale_num = pick_num(x->info.width, x->info.height, x->dw, x->dh);
  c->scale_denom = 8;
  jpeg_calc_output_dimensions(c);
  if (c->output_width < x->dw || c->output_height < x->dh || c->output_width > CD_IMAGE_SIDE_MAX) {
    x->status = CD_IMAGE_CORRUPT; return 0;
  }
  if (!cd_image_scaler_init(&x->sc, c->output_width, c->output_height, x->dw, x->dh, x->info.orientation)) {
    x->status = CD_IMAGE_NO_ROOM; return 0;
  }
  x->row = cd_alloc_try(c->output_width * 4);
  if (!x->row) { x->status = CD_IMAGE_NO_ROOM; return 0; }
  return 1;
}

static void pull_body(void *v) {
  jx *x = v;
  struct jpeg_decompress_struct *c = &x->cinfo;
  jpeg_create_decompress(c);
  c->src = &x->src;
  c->progress = &x->prog;
  jpeg_read_header(c, TRUE);
  if (!after_header(x)) return;
  jpeg_start_decompress(c);
  while (c->output_scanline < c->output_height) {
    JSAMPROW r = x->row;
    if (jpeg_read_scanlines(c, &r, 1) != 1) break;
    cd_image_scaler_row(&x->sc, x->row);
  }
  x->status = x->ran_dry ? CD_IMAGE_CORRUPT : CD_IMAGE_OK;
}

/* One step: runs until libjpeg suspends (it wants bytes) or the decode ends. */
static void push_step(void *v) {
  jx *x = v;
  struct jpeg_decompress_struct *c = &x->cinfo;
  switch (x->stage) {
    case 0:
      jpeg_create_decompress(c);
      c->src = &x->src;
      c->progress = &x->prog;
      x->stage = 1;
      /* fall through */
    case 1:
      if (jpeg_read_header(c, TRUE) == JPEG_SUSPENDED) return;
      if (!after_header(x)) { x->stage = 4; return; }
      x->stage = 2;
      /* fall through */
    case 2:
      if (!jpeg_start_decompress(c)) return;
      x->stage = 3;
      /* fall through */
    case 3:
      while (c->output_scanline < c->output_height) {
        JSAMPROW r = x->row;
        if (jpeg_read_scanlines(c, &r, 1) != 1) return;
        cd_image_scaler_row(&x->sc, x->row);
      }
      x->status = x->ran_dry ? CD_IMAGE_CORRUPT : CD_IMAGE_OK;
      x->stage = 4;
  }
}

static jx *jx_new(read_fn rd, void *rc, uint32_t size, const cd_image_info *info, uint32_t dw, uint32_t dh,
                  uint32_t piece, uint32_t cap, int push) {
  jx *x = cd_alloc_try(sizeof *x);
  if (!x) return 0;
  memset(x, 0, sizeof *x);
  x->buf = cd_alloc_try(cap);
  if (!x->buf) { cd_free(x); return 0; }
  x->info = *info; x->dw = dw; x->dh = dh; x->rd = rd; x->rc = rc; x->size = size; x->piece = piece; x->cap = cap;
  x->cinfo.err = &x->err;
  x->cinfo.client_data = x;
  x->err.error_exit = err_exit; x->err.emit_message = err_emit; x->err.output_message = err_out;
  x->err.format_message = err_fmt; x->err.reset_error_mgr = err_reset;
  x->src.init_source = src_init; x->src.term_source = src_term;
  x->src.fill_input_buffer = push ? push_fill : pull_fill;
  x->src.skip_input_data = push ? push_skip : pull_skip;
  x->src.resync_to_restart = jpeg_resync_to_restart;
  x->src.next_input_byte = x->buf; x->src.bytes_in_buffer = 0;
  x->prog.progress_monitor = progress;
  return x;
}
static uint32_t thrown(jx *x) {
  int code = x->err.msg_code;
  return x->unreadable ? UNREADABLE
       : x->too_many_scans || code == JERR_NO_BACKING_STORE ? CD_IMAGE_TOO_BIG
       : code == JERR_OUT_OF_MEMORY ? CD_IMAGE_NO_ROOM : CD_IMAGE_CORRUPT;
}
static uint32_t jx_end(jx *x, uint32_t st, uint8_t **rgba, uint32_t *w, uint32_t *h) {
  last_warnings = x->err.num_warnings;
  jpeg_destroy_decompress(&x->cinfo);
  cd_free(x->row);
  if (st == CD_IMAGE_OK) {
    uint8_t *o = cd_image_scaler_take(&x->sc);
    if (o) { *rgba = o; *w = x->sc.ow; *h = x->sc.oh; } else st = CD_IMAGE_CORRUPT;
  } else cd_image_scaler_free(&x->sc);
  cd_free(x->buf);
  cd_free(x);
  return st;
}

static uint32_t jpeg_pull(read_fn rd, void *rc, uint32_t size, const cd_image_info *info, uint32_t dw, uint32_t dh,
                          uint32_t piece, uint8_t **rgba, uint32_t *w, uint32_t *h) {
  jx *x = jx_new(rd, rc, size, info, dw, dh, piece, piece, 0);
  if (!x) return CD_IMAGE_NO_ROOM;
  uint32_t st = cd_image_guard(pull_body, x, &x->guard) ? thrown(x) : x->status;
  return jx_end(x, st, rgba, w, h);
}

/* The room a suspending source needs beside a piece: libjpeg backs up to the start of a marker segment (at most
 * 65,535 bytes after its FF and code) or of an MCU (a few KB) when it suspends, and those bytes stay. */
#define JPEG_KEEP (65536u + 4096u)

/* cancel_after: stop after that many pieces (0: never), as an app that gives up would; the caller checks the heap. */
static uint32_t jpeg_push(read_fn rd, void *rc, uint32_t size, const cd_image_info *info, uint32_t dw, uint32_t dh,
                          uint32_t piece, uint32_t cancel_after, uint8_t **rgba, uint32_t *w, uint32_t *h,
                          uint64_t *steps) {
  jx *x = jx_new(rd, rc, size, info, dw, dh, piece, piece + JPEG_KEEP, 1);
  if (!x) return CD_IMAGE_NO_ROOM;
  uint32_t st, pieces = 0;
  for (;;) {
    (*steps)++;
    if (cd_image_guard(push_step, x, &x->guard)) { st = thrown(x); break; }
    if (x->stage == 4) { st = x->status; break; }
    if (x->eof) { st = CD_IMAGE_CORRUPT; break; }        /* suspended with nothing more to give: not reached */
    if (cancel_after && pieces == cancel_after) { st = 100; break; }
    uint32_t keep = (uint32_t)x->src.bytes_in_buffer;
    memmove(x->buf, x->src.next_input_byte, keep);
    x->at = x->size - x->at < x->skip ? x->size : x->at + (uint32_t)x->skip;
    x->skip = 0;
    uint32_t room = x->cap - keep;
    uint32_t want = room < piece ? room : piece;
    if (x->size - x->at < want) want = x->size - x->at;
    if (!room) { st = CD_IMAGE_CORRUPT; break; }         /* more held back than a marker or an MCU: not reached */
    if (!want) { x->eof = 1; x->src.next_input_byte = x->buf; x->src.bytes_in_buffer = keep; continue; }
    uint32_t n = rd(rc, x->at, x->buf + keep, want);
    if (n != want) { st = UNREADABLE; break; }
    pieces++;
    x->at += n;
    x->src.next_input_byte = x->buf;
    x->src.bytes_in_buffer = keep + n;
  }
  return jx_end(x, st, rgba, w, h);
}

/* ---- PNG: a chunk walker that takes the file in pieces of any size, over tinfl ---- */

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t channels(uint32_t ct) {
  switch (ct) { case 0: return 1; case 2: return 3; case 3: return 1; case 4: return 2; case 6: return 4; default: return 0; }
}
static uint8_t over(uint32_t c, uint32_t a) { return (uint8_t)((c * a + 255u * (255u - a) + 127u) / 255u); }
typedef struct { uint32_t w, depth, ctype; int key; uint32_t key_g; uint8_t key_rgb[3]; uint8_t pal[256 * 4]; } fmt;
static void to_rgba(const fmt *f, const uint8_t *r, uint8_t *out) {   /* src/cdom_image_png.c's, as it is */
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
    default: {
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

enum { P_HEAD, P_DATA, P_CRC, P_END };
enum { K_SKIP, K_IDAT, K_PLTE, K_TRNS };
typedef struct {
  tinfl_decompressor inf;
  uint8_t dict[TINFL_LZ_DICT_SIZE];
  fmt f;
  cd_image_scaler sc;
  uint8_t *rows, *cur, *prev, *px;
  uint32_t h, stride, bpp, have, done, dict_ofs;
  int saw_idat, seen_plte, ended;
  uint32_t st;
  /* the walk */
  uint32_t size;       /* the file's claimed size: a chunk that runs past it ends the walk, as next_chunk does */
  uint32_t at;         /* the file offset of the next byte the walker takes */
  int state, kind;
  uint8_t hb[8];
  uint32_t hn, len, left, crc_left;
  uint8_t small[768];
  uint32_t sn;
} pw;

static void png_inflate(pw *x, const uint8_t *d, uint32_t n) {
  size_t in_pos = 0;
  for (;;) {
    size_t in_n = n - in_pos, out_n = TINFL_LZ_DICT_SIZE - x->dict_ofs;
    tinfl_status ts = tinfl_decompress(&x->inf, d + in_pos, &in_n, x->dict, x->dict + x->dict_ofs, &out_n,
                                       TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT);
    in_pos += in_n;
    const uint8_t *s = x->dict + x->dict_ofs;
    size_t left = out_n;
    while (left && x->done < x->h) {
      size_t take = (x->stride + 1) - x->have;
      if (take > left) take = left;
      memcpy(x->cur + x->have, s, take);
      x->have += (uint32_t)take; s += take; left -= take;
      if (x->have == x->stride + 1) {
        if (!cd_image_unfilter(x->cur[0], x->cur + 1, x->prev + 1, x->stride, x->bpp)) { x->st = CD_IMAGE_CORRUPT; break; }
        to_rgba(&x->f, x->cur + 1, x->px);
        cd_image_scaler_row(&x->sc, x->px);
        uint8_t *t = x->cur; x->cur = x->prev; x->prev = t;
        x->have = 0;
        x->done++;
      }
    }
    x->dict_ofs = (x->dict_ofs + (uint32_t)out_n) & (TINFL_LZ_DICT_SIZE - 1);
    if (x->st != CD_IMAGE_OK) return;
    if (ts < TINFL_STATUS_DONE) { x->st = CD_IMAGE_CORRUPT; return; }
    if (ts == TINFL_STATUS_DONE) { x->ended = 1; return; }
    if (ts == TINFL_STATUS_NEEDS_MORE_INPUT && in_pos >= n) return;
  }
}

/* The end of a chunk's payload: PLTE and tRNS (before the first IDAT) take effect, as the whole walk's do. */
static void png_chunk_done(pw *x) {
  const uint8_t *data = x->small;
  uint32_t n = x->len;
  if (x->kind == K_PLTE) {
    for (uint32_t i = 0; i < n / 3; i++) memcpy(x->f.pal + 4 * i, data + 3 * i, 3);
    x->seen_plte = 1;
  } else if (x->kind == K_TRNS) {
    uint32_t ct = x->f.ctype;
    if (ct == 3) for (uint32_t i = 0; i < n && i < 256; i++) x->f.pal[4 * i + 3] = data[i];
    else if (ct == 0 && n == 2) { x->f.key = 1; x->f.key_g = be16(data); }
    else if (ct == 2 && n == 6 && !data[0] && !data[2] && !data[4]) {
      x->f.key = 1; x->f.key_rgb[0] = data[1]; x->f.key_rgb[1] = data[3]; x->f.key_rgb[2] = data[5];
    }
  }
}

/* Takes n bytes, the file's next (at x->at). Any split is fine: a chunk's length and type, its payload and its CRC
 * may each end anywhere. */
static void png_feed(pw *x, const uint8_t *p, uint32_t n) {
  while (n && x->state != P_END && x->st == CD_IMAGE_OK && !x->ended) {
    if (x->state == P_HEAD) {
      uint32_t t = 8 - x->hn < n ? 8 - x->hn : n;
      memcpy(x->hb + x->hn, p, t);
      x->hn += t; p += t; n -= t; x->at += t;
      if (x->hn < 8) break;
      x->hn = 0;
      uint32_t l = be32(x->hb);
      const uint8_t *type = x->hb + 4;
      if ((uint64_t)x->at - 8 + 12 + l > x->size) { x->state = P_END; break; }   /* runs past the file */
      x->len = x->left = l; x->sn = 0; x->kind = K_SKIP;
      if (!memcmp(type, "IEND", 4)) { x->state = P_END; break; }
      if (!memcmp(type, "IDAT", 4)) {
        if (x->f.ctype == 3 && !x->seen_plte) { x->st = CD_IMAGE_CORRUPT; break; }
        x->saw_idat = 1; x->kind = K_IDAT;
      } else if (!memcmp(type, "PLTE", 4) && !x->saw_idat) {
        if (l % 3 || l > 768) { x->st = CD_IMAGE_CORRUPT; break; }
        x->kind = K_PLTE;
      } else if (!memcmp(type, "tRNS", 4) && !x->saw_idat) x->kind = K_TRNS;
      x->state = P_DATA;
      if (!l) { png_chunk_done(x); x->state = P_CRC; x->crc_left = 4; }
    } else if (x->state == P_DATA) {
      uint32_t t = x->left < n ? x->left : n;
      if (x->kind == K_IDAT) png_inflate(x, p, t);
      else if (x->kind != K_SKIP) {
        uint32_t keep = x->sn < 768 ? 768 - x->sn : 0;
        if (keep > t) keep = t;
        memcpy(x->small + x->sn, p, keep);
        x->sn += keep;
      }
      x->left -= t; p += t; n -= t; x->at += t;
      if (!x->left && x->st == CD_IMAGE_OK && !x->ended) { png_chunk_done(x); x->state = P_CRC; x->crc_left = 4; }
    } else {
      uint32_t t = x->crc_left < n ? x->crc_left : n;
      x->crc_left -= t; p += t; n -= t; x->at += t;
      if (!x->crc_left) x->state = P_HEAD;
    }
  }
}
static int png_finished(const pw *x) { return x->state == P_END || x->st != CD_IMAGE_OK || x->ended; }

static uint32_t png_pieces(read_fn rd, void *rc, uint32_t size, const cd_image_info *info, uint32_t dw, uint32_t dh,
                           uint32_t piece, uint32_t cancel_after, uint8_t **rgba, uint32_t *ow, uint32_t *oh) {
  uint8_t ih[33];
  if (rd(rc, 0, ih, 33) != 33) return UNREADABLE;
  const uint8_t *d = ih + 16;
  uint32_t depth = d[8], ctype = d[9];
  const uint32_t w = info->width, h = info->height;
  uint32_t ch = channels(ctype);
  uint32_t stride = (uint32_t)(((uint64_t)w * ch * depth + 7) / 8);
  pw *x = cd_alloc_try(sizeof *x);
  if (!x) return CD_IMAGE_NO_ROOM;
  memset(x, 0, sizeof *x);
  x->f.w = w; x->f.depth = depth; x->f.ctype = ctype;
  for (int i = 0; i < 256; i++) x->f.pal[4 * i + 3] = 255;
  x->h = h; x->stride = stride; x->bpp = ch * depth >= 8 ? ch * depth / 8 : 1;
  x->size = size; x->at = 33; x->st = CD_IMAGE_OK;
  uint64_t room = (uint64_t)(stride + 1) * 2 + (uint64_t)w * 4;
  x->rows = room <= 0xffffffffu ? cd_alloc_try((uint32_t)room) : 0;
  uint8_t *buf = cd_alloc_try(piece);
  if (!x->rows || !buf || !cd_image_scaler_init(&x->sc, w, h, dw, dh, 1)) {
    cd_free(buf); cd_free(x->rows); cd_free(x); return CD_IMAGE_NO_ROOM;
  }
  x->cur = x->rows; x->prev = x->rows + (stride + 1); x->px = x->rows + 2 * (size_t)(stride + 1);
  memset(x->prev, 0, stride + 1);
  tinfl_init(&x->inf);
  uint32_t st = CD_IMAGE_OK, pieces = 0;
  while (!png_finished(x)) {
    if (cancel_after && pieces == cancel_after) { st = 100; break; }
    uint32_t want = size - x->at < piece ? size - x->at : piece;
    if (!want) break;
    uint32_t n = rd(rc, x->at, buf, want);
    if (n != want) { st = UNREADABLE; break; }
    pieces++;
    png_feed(x, buf, n);
  }
  if (st == CD_IMAGE_OK) st = x->st;
  if (st == CD_IMAGE_OK && !x->ended) st = CD_IMAGE_CORRUPT;
  if (st == CD_IMAGE_OK) {
    uint8_t *o = cd_image_scaler_take(&x->sc);
    if (o) { *rgba = o; *ow = x->sc.ow; *oh = x->sc.oh; } else st = CD_IMAGE_CORRUPT;
  } else cd_image_scaler_free(&x->sc);
  cd_free(buf); cd_free(x->rows); cd_free(x);
  return st;
}

/* ---- the entry: what cd_image_resize does, from a reader ---- */

enum { M_PULL, M_PUSH };
static uint32_t resize_pieces(int mode, read_fn rd, void *rc, uint32_t size, uint32_t box, uint32_t piece,
                              uint32_t cancel_after, uint8_t **rgba, uint32_t *w, uint32_t *h, uint64_t *steps) {
  *rgba = 0; *w = 0; *h = 0;
  if (!box || box > CD_IMAGE_BOX_MAX) return CD_IMAGE_BAD_BOX;
  cd_image_info info;
  uint32_t st = probe_head(rd, rc, size, &info);
  if (st != CD_IMAGE_OK) return st;
  uint32_t dw, dh;
  cd_image_target(info.width, info.height, info.orientation, box, box, &dw, &dh);
  if (info.format == CD_IMAGE_PNG) return png_pieces(rd, rc, size, &info, dw, dh, piece, cancel_after, rgba, w, h);
  return mode == M_PULL ? jpeg_pull(rd, rc, size, &info, dw, dh, piece, rgba, w, h)
                        : jpeg_push(rd, rc, size, &info, dw, dh, piece, cancel_after, rgba, w, h, steps);
}

/* ---- the harness ---- */

static uint8_t *load(const char *path, uint32_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f) { perror(path); exit(2); }
  fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *b = malloc((size_t)l ? (size_t)l : 1);
  if (fread(b, 1, (size_t)l, f) != (size_t)l) exit(2);
  fclose(f);
  *n = (uint32_t)l;
  return b;
}
static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}
static int fails, quiet;
static void expect(int ok, const char *what, const char *name, uint32_t piece, uint32_t got, uint32_t want) {
  if (!ok) { fails++; printf("FAIL %s %s piece %u: %u, want %u\n", name, what, piece, got, want); }
}

/* Whole as the reference; pull and push at every piece size equal to it, status and bytes. */
static void check_file(const char *path, const uint32_t *pieces, int npieces) {
  uint32_t len;
  uint8_t *b = load(path, &len);
  const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
  uint32_t box = 900, bw, bh;                    /* an oracle case's box is in its name ("-box256x256"), square here */
  if (strstr(name, "-box") && sscanf(strstr(name, "-box") + 4, "%ux%u", &bw, &bh) == 2) box = bw;
  uint8_t *ref; uint32_t rw, rh;
  uint32_t rst = cd_image_resize(b, len, box, box, &ref, &rw, &rh);
  long rwarn;                                    /* the warnings with one piece the size of the file: whole's */
  { file f = {b, len, len, 0, 0}; uint8_t *o; uint32_t w, h; uint64_t steps = 0; last_warnings = -1;
    if (!resize_pieces(M_PULL, file_read, &f, len, box, len, 0, &o, &w, &h, &steps)) cd_free(o);
    rwarn = last_warnings; }
  for (int i = 0; i < npieces; i++) {
    for (int mode = M_PULL; mode <= M_PUSH; mode++) {
      file f = {b, len, len, 0, 0};
      uint8_t *o; uint32_t w, h; uint64_t steps = 0;
      size_t live = host_live;
      last_warnings = -1;
      double t0 = now_ms();
      uint32_t st = resize_pieces(mode, file_read, &f, len, box, pieces[i], 0, &o, &w, &h, &steps);
      double ms = now_ms() - t0;
      int same = st == rst && (st || (w == rw && h == rh && !memcmp(o, ref, (size_t)w * h * 4)));
      expect(same, mode ? "push differs" : "pull differs", name, pieces[i], st, rst);
      /* libjpeg warns again when it re-reads, after a suspension, a marker that warned (scans500.jpg and scans501.jpg
       * warn on every scan): their pushed counts are not compared, only their bytes. */
      if (mode == M_PULL || strncmp(name, "scans50", 7))
      expect(last_warnings == rwarn, mode ? "push warnings differ" : "pull warnings differ", name, pieces[i],
             (uint32_t)last_warnings, (uint32_t)rwarn);
      if (!st) cd_free(o);
      expect(host_live == live, "leaves blocks", name, pieces[i], (uint32_t)(host_live - live), 0);
      if (!quiet) printf("%s %s piece %u: status %u, %s, %llu reads, %llu steps, %.0f ms\n", name, mode ? "push" : "pull", pieces[i], st,
             same ? "equal" : "DIFFERENT", (unsigned long long)f.reads, (unsigned long long)steps, ms);
    }
  }
  if (!rst) cd_free(ref);
  /* the failures, at 4093-byte pieces, for a file that decodes (another is refused before the failure's place): */
  for (int mode = M_PULL; mode <= M_PUSH && !rst && len > 8192; mode++) {
    uint8_t *o; uint32_t w, h; uint64_t steps = 0;
    size_t live = host_live;
    file s = {b, len, len / 2, 0, 0};            /* shorter than claimed: the read past its end comes back short */
    uint32_t st = resize_pieces(mode, file_read, &s, len, 900, 4093, 0, &o, &w, &h, &steps);
    expect(st == UNREADABLE, "short file", name, 4093, st, UNREADABLE);
    file e = {b, len, len, len / 3, 0};          /* a read error a third of the way in */
    st = resize_pieces(mode, file_read, &e, len, 900, 4093, 0, &o, &w, &h, &steps);
    expect(st == UNREADABLE, "read error", name, 4093, st, UNREADABLE);
    uint8_t *cut; uint32_t cw, ch;               /* a file cut short (its size says so): the same status as whole */
    uint32_t wst = cd_image_resize(b, len / 2, 900, 900, &cut, &cw, &ch);
    if (!wst) cd_free(cut);
    file c = {b, len / 2, len / 2, 0, 0};
    st = resize_pieces(mode, file_read, &c, len / 2, 900, 4093, 0, &o, &w, &h, &steps);
    if (!st) cd_free(o);
    expect(st == wst, "cut file", name, 4093, st, wst);
    for (uint32_t cut_at = len - 14; cut_at < len - 9; cut_at++) {   /* cut inside a PNG's last IDAT CRC, and around */
      wst = cd_image_resize(b, cut_at, 900, 900, &cut, &cw, &ch);
      if (!wst) cd_free(cut);
      file t = {b, cut_at, cut_at, 0, 0};
      st = resize_pieces(mode, file_read, &t, cut_at, 900, 4093, 0, &o, &w, &h, &steps);
      if (!st) cd_free(o);
      expect(st == wst, "cut near the end", name, cut_at, st, wst);
    }
    if (mode == M_PUSH) {                        /* cancel after 3 pieces: nothing left in the heap */
      file k = {b, len, len, 0, 0};
      st = resize_pieces(mode, file_read, &k, len, 900, 4093, 3, &o, &w, &h, &steps);
      expect(st == 100, "cancel", name, 4093, st, 100);
    }
    expect(host_live == live, "failure leaves blocks", name, 4093, (uint32_t)(host_live - live), 0);
  }
  free(b);
}

static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

/* Interleaved rounds: whole, pull 64K, push 64K, pull 256K, push 256K; the median of each, and its peak. */
static void time_file(const char *path, int reps) {
  uint32_t len;
  uint8_t *b = load(path, &len);
  const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
  enum { NA = 5 };
  const char *arms[NA] = {"whole", "pull 65536", "push 65536", "pull 262144", "push 262144"};
  const int mode[NA] = {-1, M_PULL, M_PUSH, M_PULL, M_PUSH};
  const uint32_t piece[NA] = {0, 65536, 65536, 262144, 262144};
  double t[NA][64];
  size_t peak[NA] = {0};
  for (int r = 0; r < reps; r++) {
    for (int a = 0; a < NA; a++) {
      uint8_t *o; uint32_t w, h; uint64_t steps = 0;
      uint8_t *copy = 0;
      host_reset(SIZE_MAX);
      double t0 = now_ms();
      uint32_t st;
      if (mode[a] < 0) {                         /* whole: the file in C memory first, as cd_file_read puts it */
        copy = cd_alloc_try(len);
        memcpy(copy, b, len);
        st = cd_image_resize(copy, len, 900, 900, &o, &w, &h);
      } else {
        file f = {b, len, len, 0, 0};
        st = resize_pieces(mode[a], file_read, &f, len, 900, piece[a], 0, &o, &w, &h, &steps);
      }
      t[a][r] = now_ms() - t0;
      if (host_peak > peak[a]) peak[a] = host_peak;
      if (st) { printf("%s %s: status %u\n", name, arms[a], st); exit(1); }
      cd_free(o);
      cd_free(copy);
    }
  }
  double base = 0;
  for (int a = 0; a < NA; a++) {
    qsort(t[a], (size_t)reps, sizeof(double), cmp);
    double med = t[a][reps / 2];
    if (!a) base = med;
    printf("%s\t%s\tmedian %.1f ms\t(min %.1f, max %.1f)\t%+.2f%%\tpeak %zu bytes (%.2f MB)\n", name, arms[a], med, t[a][0],
           t[a][reps - 1], (med / base - 1) * 100, peak[a], peak[a] / 1e6);
  }
  free(b);
}

int main(int argc, char **argv) {
  if (argc >= 3 && !strcmp(argv[1], "check")) {
    static const uint32_t small[] = {1, 7, 4093, 65536, 262144}, big[] = {4093, 65536, 262144};
    quiet = getenv("QUIET") != 0;
    for (int i = 2; i < argc; i++) {
      uint32_t n;
      free(load(argv[i], &n));
      if (!strstr(argv[i], "p48") && !strstr(argv[i], "p64")) check_file(argv[i], small, 5); else check_file(argv[i], big, 3);
    }
    printf(fails ? "%d FAILED\n" : "all equal\n", fails);
    return fails != 0;
  }
  if (argc == 5 && !strcmp(argv[1], "once")) {   /* one decode, for an instruction count: once FILE whole|pull|push PIECE */
    uint32_t len;
    uint8_t *b = load(argv[2], &len), *o, *copy;
    uint32_t w, h, st;
    uint64_t steps = 0;
    if (!strcmp(argv[3], "whole")) { copy = cd_alloc_try(len); memcpy(copy, b, len); st = cd_image_resize(copy, len, 900, 900, &o, &w, &h); }
    else {
      file f = {b, len, len, 0, 0};
      st = resize_pieces(strcmp(argv[3], "pull") ? M_PUSH : M_PULL, file_read, &f, len, 900, (uint32_t)atoi(argv[4]), 0, &o, &w, &h, &steps);
    }
    printf("status %u %ux%u\n", st, w, h);
    return (int)st;
  }
  if (argc == 4 && !strcmp(argv[1], "time")) { time_file(argv[2], atoi(argv[3]) > 63 ? 63 : atoi(argv[3])); return 0; }
  fprintf(stderr, "usage: proto check FILE... | proto time FILE REPS\n");
  return 2;
}
