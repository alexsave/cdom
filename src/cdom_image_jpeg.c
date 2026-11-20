/* cdom_image's JPEG path: libjpeg-turbo's decoder, row by row, at the smallest scale (n/8) whose output is still at
 * least the target size, into the streaming scaler. The file's bytes come in pieces (cdom_image.h's fed decoder):
 * libjpeg's suspending source, every byte seen first by the header walker (the probe's rules, as the bytes come);
 * for cd_image_resize the source is the caller's bytes, where they are.
 * Here too, cdom's own parts of libjpeg: the memory layer (jmemsys.h, in place of jmemnobs.c) over cd_alloc_try,
 * with a budget; the error exit (cd_image_throw); a progress monitor that bounds the scans; and the 12-bit entry
 * points the decoder names, which a file never reaches. */
#include <stddef.h>
#include <string.h>
#include "cdom_image_int.h"
#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jmemsys.h"
#include "jerror.h"

/* ---- the header, read first: what is refused, and the size, before libjpeg allocates anything ---- */

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }

/* Walks the markers up to the first SOF, as libjpeg does (bytes between segments skipped, fill bytes taken). */
uint32_t cd_image_jpeg_probe(const uint8_t *p, uint32_t len, cd_image_info *info) {
  info->format = CD_IMAGE_JPEG;
  if (len < 4 || p[0] != 0xff || p[1] != 0xd8) return CD_IMAGE_NOT_IMAGE;
  uint64_t at = 2;
  for (;;) {
    while (at < len && p[at] != 0xff) at++;
    while (at < len && p[at] == 0xff) at++;
    if (at >= len) return CD_IMAGE_CORRUPT;
    uint32_t m = p[at++];
    if (m == 0 || m == 0x01 || (m >= 0xd0 && m <= 0xd8)) continue;   /* no length */
    if (m == 0xd9 || m == 0xda) return CD_IMAGE_CORRUPT;            /* the end, or a scan, before any frame */
    if (at + 2 > len) return CD_IMAGE_CORRUPT;
    uint32_t seg = be16(p + at);
    if (seg < 2 || at + seg > len) return CD_IMAGE_CORRUPT;
    if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {   /* a SOF */
      if (m != 0xc0 && m != 0xc1 && m != 0xc2) return CD_IMAGE_UNSUPPORTED; /* lossless, hierarchical, arithmetic */
      if (seg < 8) return CD_IMAGE_CORRUPT;
      const uint8_t *f = p + at + 2;
      uint32_t w = be16(f + 3), h = be16(f + 1), ncomp = f[5];
      if (f[0] != 8) return CD_IMAGE_UNSUPPORTED;                       /* 12-bit */
      if (!w || !h || seg < 8 + 3 * ncomp) return CD_IMAGE_CORRUPT;     /* a height of 0 (DNL) is not supported */
      if (ncomp != 1 && ncomp != 3) return CD_IMAGE_UNSUPPORTED;        /* CMYK and YCCK */
      uint32_t o = cd_image_orientation(p, len);
      info->width = w; info->height = h; info->progressive = m == 0xc2; info->orientation = o;
      info->shown_w = o >= 5 ? h : w; info->shown_h = o >= 5 ? w : h;
      if (w > CD_IMAGE_SIDE_MAX || h > CD_IMAGE_SIDE_MAX || (uint64_t)w * h > CD_IMAGE_PIXELS_MAX) return CD_IMAGE_TOO_BIG;
      return CD_IMAGE_OK;
    }
    at += seg;
  }
}

/* ---- the header walker (cdom_image_int.h, cd_image_walk): the probe's walk and the orientation's, a byte at a time ---- */

enum { P_SCAN, P_FILL, P_LEN, P_SOF, P_DONE };
enum { O_HEAD, O_APP1, O_EXIF, O_DONE };

static void walk_init(cd_image_walk *w, uint32_t size) {
  memset(w, 0, sizeof *w);
  w->live = 1;
  w->verdict = CD_IMAGE_MORE;
  w->pat = 2; w->ps = P_SCAN;
  if (size < 4) { w->ps = P_DONE; w->verdict = CD_IMAGE_NOT_IMAGE; }
  w->oat = 2; w->os = O_HEAD;
}

/* cd_image_jpeg_probe's walk over p[0 .. n), the file's bytes from base on: the same rules, the same verdicts. */
static void probe_walk(cd_image_walk *w, const uint8_t *p, uint32_t n, uint64_t base, uint64_t size) {
  const uint64_t end = base + n;
  while (w->ps != P_DONE) {
    if ((w->ps == P_SCAN || w->ps == P_FILL) && w->pat >= size) { w->ps = P_DONE; w->verdict = CD_IMAGE_CORRUPT; return; }
    if (w->pat >= end) return;
    const uint8_t *q = p + (w->pat - base), *e = p + n;
    if (w->ps == P_SCAN) {                         /* bytes between segments, skipped */
      while (q < e && *q != 0xff) q++;
      w->pat = base + (uint64_t)(q - p);
      if (q < e) w->ps = P_FILL;
    } else if (w->ps == P_FILL) {                  /* FF, and fill bytes; then the marker */
      while (q < e && *q == 0xff) q++;
      w->pat = base + (uint64_t)(q - p);
      if (q == e) continue;
      uint32_t m = *q;
      w->pat++;
      if (m == 0 || m == 0x01 || (m >= 0xd0 && m <= 0xd8)) { w->ps = P_SCAN; continue; }   /* no length */
      if (m == 0xd9 || m == 0xda) { w->ps = P_DONE; w->verdict = CD_IMAGE_CORRUPT; return; }   /* before any frame */
      if (w->pat + 2 > size) { w->ps = P_DONE; w->verdict = CD_IMAGE_CORRUPT; return; }
      w->marker = m; w->pn = 0; w->ps = P_LEN;
    } else {                                       /* a length (2 bytes), or a SOF's first 8, gathered */
      uint32_t want = (w->ps == P_LEN ? 2u : 8u) - w->pn, k = (uint32_t)(end - w->pat);
      if (k > want) k = want;
      memcpy(w->pg + w->pn, q, k);
      w->pn += k; w->pat += k;
      if (w->pn < (w->ps == P_LEN ? 2u : 8u)) return;
      uint64_t seg_at = w->pat - w->pn;            /* the segment's length bytes */
      if (w->ps == P_LEN) {
        uint32_t seg = be16(w->pg), m = w->marker;
        if (seg < 2 || seg_at + seg > size) { w->ps = P_DONE; w->verdict = CD_IMAGE_CORRUPT; return; }
        w->seg = seg;
        if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {   /* a SOF */
          if (m != 0xc0 && m != 0xc1 && m != 0xc2) { w->ps = P_DONE; w->verdict = CD_IMAGE_UNSUPPORTED; return; }
          if (seg < 8) { w->ps = P_DONE; w->verdict = CD_IMAGE_CORRUPT; return; }
          w->ps = P_SOF;
          continue;
        }
        w->pat = seg_at + seg;                     /* past the segment, by count */
        w->ps = P_SCAN;
        continue;
      }
      const uint8_t *f = w->pg + 2;
      uint32_t wd = be16(f + 3), ht = be16(f + 1), ncomp = f[5];
      w->ps = P_DONE;
      if (f[0] != 8) { w->verdict = CD_IMAGE_UNSUPPORTED; return; }                    /* 12-bit */
      if (!wd || !ht || w->seg < 8 + 3 * ncomp) { w->verdict = CD_IMAGE_CORRUPT; return; }
      if (ncomp != 1 && ncomp != 3) { w->verdict = CD_IMAGE_UNSUPPORTED; return; }   /* CMYK and YCCK */
      w->w = wd; w->h = ht; w->progressive = w->marker == 0xc2;
      w->verdict = wd > CD_IMAGE_SIDE_MAX || ht > CD_IMAGE_SIDE_MAX || (uint64_t)wd * ht > CD_IMAGE_PIXELS_MAX ? CD_IMAGE_TOO_BIG
                 : CD_IMAGE_OK;
    }
  }
}

/* cd_image_orientation's walk: segments by their lengths from the start, to the scan; the first APP1 that starts
 * "Exif\0\0" is read whole (in place when one piece holds it, else gathered). NO_ROOM when gathering found none. */
static uint32_t orient_walk(cd_image_walk *w, const uint8_t *p, uint32_t n, uint64_t base, uint64_t size) {
  const uint64_t end = base + n;
  while (w->os != O_DONE) {
    if (w->os == O_HEAD) {                         /* FF, the marker, the length: 4 bytes at oat */
      if (w->oat + 4 > size) { w->os = O_DONE; w->orientation = 1; break; }
      while (w->on < 4 && w->oat + w->on < end) {
        w->og[w->on] = p[w->oat + w->on - base];
        w->on++;
      }
      if (w->on < 4) return CD_IMAGE_MORE;
      uint32_t marker = w->og[1], seg = be16(w->og + 2);
      if (w->og[0] != 0xff) { w->os = O_DONE; w->orientation = 1; break; }
      if (marker == 0xff) {                        /* a fill byte: the walk moves one on */
        memmove(w->og, w->og + 1, 3); w->on = 3; w->oat++;
        continue;
      }
      if (marker == 0xda || marker == 0xd9 || seg < 2) { w->os = O_DONE; w->orientation = 1; break; }
      w->on = 0;
      if (marker == 0xe1 && seg >= 16 && w->oat + 2 + seg <= size) { w->oseg = seg; w->os = O_APP1; continue; }
      w->oat += 2 + (uint64_t)seg;
    } else if (w->os == O_APP1) {                  /* the 6 bytes after the length: Exif\0\0? */
      while (w->on < 6 && w->oat + 4 + w->on < end) {
        w->og[4 + w->on] = p[w->oat + 4 + w->on - base];
        w->on++;
      }
      if (w->on < 6) return CD_IMAGE_MORE;
      w->on = 0;
      if (memcmp(w->og + 4, "Exif\0\0", 6)) { w->oat += 2 + (uint64_t)w->oseg; w->os = O_HEAD; continue; }
      w->os = O_EXIF;
    } else {                                       /* the segment after its length, seg - 2 bytes, whole */
      const uint64_t from = w->oat + 4, len = w->oseg - 2u;
      if (!w->exif && from >= base && from + len <= end) {
        w->orientation = cd_image_exif_orientation(p + (from - base) + 6, w->oseg - 8);
        w->os = O_DONE;
        break;
      }
      if (!w->exif) {
        w->exif = cd_alloc_try((uint32_t)len);
        if (!w->exif) return CD_IMAGE_NO_ROOM;
        w->exif_n = 6;                             /* "Exif\0\0", read already (perhaps in a piece before): not needed again */
      }
      while (w->exif_n < len && from + w->exif_n < end) {
        uint64_t at = from + w->exif_n, k = end - at;
        if (k > len - w->exif_n) k = len - w->exif_n;
        memcpy(w->exif + w->exif_n, p + (at - base), (size_t)k);   /* at >= base: the bytes before were read */
        w->exif_n += (uint32_t)k;
      }
      if (w->exif_n < len) return CD_IMAGE_MORE;
      w->orientation = cd_image_exif_orientation(w->exif + 6, w->oseg - 8);
      cd_free(w->exif); w->exif = 0;
      w->os = O_DONE;
    }
  }
  return CD_IMAGE_MORE;
}

/* Both walks over the next bytes: MORE, or a final status (the probe's refusal, or NO_ROOM). */
static uint32_t walk_feed(cd_image_dec *d, const uint8_t *p, uint32_t n, uint64_t base) {
  cd_image_walk *w = &d->walk;
  probe_walk(w, p, n, base, d->size);
  if (w->verdict != CD_IMAGE_MORE && w->verdict != CD_IMAGE_OK) return w->verdict;
  if (orient_walk(w, p, n, base, d->size) == CD_IMAGE_NO_ROOM) return CD_IMAGE_NO_ROOM;
  if (w->verdict == CD_IMAGE_OK && w->os == O_DONE && !d->info.format) {
    uint32_t o = w->orientation;
    cd_image_info *i = &d->info;
    i->format = CD_IMAGE_JPEG; i->width = w->w; i->height = w->h; i->progressive = w->progressive; i->orientation = o;
    i->shown_w = o >= 5 ? w->h : w->w; i->shown_h = o >= 5 ? w->w : w->h;
  }
  return CD_IMAGE_MORE;
}
static int walk_done(const cd_image_dec *d) { return d->info.format == CD_IMAGE_JPEG; }

/* ---- one decode's state: everything the error path must release, reached through cinfo->client_data ---- */

/* Where the decode is between puts: libjpeg's calls are made again from where they suspended. */
enum { J_HEADER, J_START, J_ROWS };

typedef struct {
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr err;
  struct jpeg_source_mgr src;
  struct jpeg_progress_mgr prog;
  void *guard;                /* the cd_image_guard the error exit leaves through */
  cd_image_dec *d;
  uint32_t dw, dh;
  cd_image_scaler sc;
  uint8_t *row;
  uint32_t stage;
  uint32_t status;            /* the body's answer, when it returns */
  uint32_t failed;            /* libjpeg's error, held while the walker has not reached its verdict (0: none) */
  uint32_t skip;              /* bytes libjpeg skipped past the buffer: the next ones are dropped as they come */
  uint32_t since;             /* output rows since the last step (an output pass that reads nothing is cut every 64) */
  uint8_t gather;             /* a progressive file: the rest of it gathered before jpeg_start_decompress (below) */
  uint8_t created;            /* jpeg_create_decompress has run */
  uint8_t ran_dry;            /* libjpeg asked for bytes past the end of the file: the file is cut short */
  uint8_t too_many_scans;
} jctx;

/* ---- the memory layer (jmemsys.h): every block through cd_alloc_try, and the budget, CD_IMAGE_MEM_MAX ----
 *
 * The budget is libjpeg's own: jpeg_mem_init answers it, so it is max_memory_to_use, and realize_virt_arrays asks
 * jpeg_mem_available whether the whole-image arrays (every coefficient of a progressive or multi-scan file) fit
 * beside what is held already; when they do not, it opens a backing store, which here stops the decode (TOO_BIG)
 * before any of them is allocated. Everything else libjpeg holds is a few rows of the image, bounded by its width.
 * A block the heap has no room for is 0, and libjpeg stops with JERR_OUT_OF_MEMORY (NO_ROOM). */
void *jpeg_get_small(j_common_ptr c, size_t n) { (void)c; return n > 0xffffffffu ? 0 : cd_alloc_try((uint32_t)n); }
void jpeg_free_small(j_common_ptr c, void *o, size_t n) { (void)c; (void)n; cd_free(o); }
void *jpeg_get_large(j_common_ptr c, size_t n) { (void)c; return n > 0xffffffffu ? 0 : cd_alloc_try((uint32_t)n); }
void jpeg_free_large(j_common_ptr c, void *o, size_t n) { (void)c; (void)n; cd_free(o); }
size_t jpeg_mem_available(j_common_ptr c, size_t min_needed, size_t max_needed, size_t already) {
  (void)min_needed; (void)max_needed;
  size_t cap = (size_t)c->mem->max_memory_to_use;
  return cap > already ? cap - already : 0;
}
void jpeg_open_backing_store(j_common_ptr c, backing_store_ptr info, long total) {
  (void)info; (void)total;
  ERREXIT(c, JERR_NO_BACKING_STORE);
}
long jpeg_mem_init(j_common_ptr c) { (void)c; return (long)CD_IMAGE_MEM_MAX; }
void jpeg_mem_term(j_common_ptr c) { (void)c; }

/* ---- the 12-bit entry points jdmaster.c names: never reached (the probe refuses a 12-bit frame first) ---- */

void j12init_color_deconverter(j_decompress_ptr c) { ERREXIT1(c, JERR_BAD_PRECISION, 12); }
void j12init_upsampler(j_decompress_ptr c) { ERREXIT1(c, JERR_BAD_PRECISION, 12); }
void j12init_inverse_dct(j_decompress_ptr c) { ERREXIT1(c, JERR_BAD_PRECISION, 12); }
void j12init_d_coef_controller(j_decompress_ptr c, boolean b) { (void)b; ERREXIT1(c, JERR_BAD_PRECISION, 12); }
void j12init_d_main_controller(j_decompress_ptr c, boolean b) { (void)b; ERREXIT1(c, JERR_BAD_PRECISION, 12); }
void j12init_d_post_controller(j_decompress_ptr c, boolean b) { (void)b; ERREXIT1(c, JERR_BAD_PRECISION, 12); }

/* ---- the error manager: a fatal error leaves through the guard; a warning is counted ---- */

static void err_exit(j_common_ptr c) { cd_image_throw(((jctx *)c->client_data)->guard); }
static void err_emit(j_common_ptr c, int level) { if (level < 0) c->err->num_warnings++; }
static void err_out(j_common_ptr c) { (void)c; }
static void err_fmt(j_common_ptr c, char *buf) { (void)c; buf[0] = 0; }
static void err_reset(j_common_ptr c) { c->err->msg_code = 0; c->err->num_warnings = 0; }

/* Called between scans and rows: a file of more than CD_IMAGE_SCANS_MAX scans stops (TOO_BIG). */
static void progress(j_common_ptr c) {
  jctx *x = c->client_data;
  if ((uint32_t)x->cinfo.input_scan_number > CD_IMAGE_SCANS_MAX) { x->too_many_scans = 1; cd_image_throw(x->guard); }
}

/* ---- the source: the pieces, or the caller's bytes ---- */

/* libjpeg wants more than the buffer holds: until the file's size is in, it suspends (FALSE: it backs up to the start
 * of the marker or MCU it was reading, and the put answers MORE); once it is all in, the file ends there, as libjpeg's
 * memory source ends it: an EOI. */
static void src_init(j_decompress_ptr c) { (void)c; }
static boolean src_fill(j_decompress_ptr c) {
  static const JOCTET eoi[2] = {0xff, JPEG_EOI};
  jctx *x = c->client_data;
  if (x->d->taken < x->d->size) return FALSE;
  x->ran_dry = 1;
  x->src.next_input_byte = eoi;
  x->src.bytes_in_buffer = 2;
  return TRUE;
}
/* A skip past what is in: the rest is counted, and dropped from the next pieces as they come (past the file's end,
 * nothing more comes: the next want ends the file). */
static void src_skip(j_decompress_ptr c, long n) {
  jctx *x = c->client_data;
  if (n <= 0) return;
  if ((size_t)n > x->src.bytes_in_buffer) {
    uint64_t past = (uint64_t)n - x->src.bytes_in_buffer, left = x->d->size - x->d->taken;
    x->skip = (uint32_t)(past < left ? past : left);
    n = (long)x->src.bytes_in_buffer;
  }
  x->src.next_input_byte += n;
  x->src.bytes_in_buffer -= (size_t)n;
}
static void src_term(j_decompress_ptr c) { (void)c; }

/* The scale numerator: the smallest n in 1..8 for which both sides of the decode, ceil(side*n/8), are at least the
 * target's. */
static uint32_t pick_num(uint32_t w, uint32_t h, uint32_t dw, uint32_t dh) {
  for (uint32_t n = 1; n < 8; n++) {
    uint64_t cw = ((uint64_t)w * n + 7) / 8, ch = ((uint64_t)h * n + 7) / 8;
    if (cw >= dw && ch >= dh) return n;
  }
  return 8;
}

/* The decode as far as the bytes go, under the guard: every libjpeg call is here, each made again from where it
 * suspended (stage). It sets x->status: MORE (suspended, or a step of the output pass done), or the final status. */
static void body(void *v) {
  jctx *x = v;
  cd_image_dec *d = x->d;
  struct jpeg_decompress_struct *c = &x->cinfo;
  if (!x->created) {
    jpeg_create_decompress(c);
    x->created = 1;
    c->src = &x->src;
    c->progress = &x->prog;
  }
  x->status = CD_IMAGE_MORE;
  if (x->stage == J_HEADER) {
    if (jpeg_read_header(c, TRUE) == JPEG_SUSPENDED) return;
    /* the walker saw every byte libjpeg read, through the SOS: its walks have ended there (CORRUPT if not, which
     * the probe's walk answers for a scan before any frame) */
    if (!walk_done(d)) { x->status = CD_IMAGE_CORRUPT; return; }
    cd_image_target(d->info.width, d->info.height, d->info.orientation, d->box_w, d->box_h, &x->dw, &x->dh);
    if (d->test_w) { x->dw = d->test_w; x->dh = d->test_h; d->info.orientation = d->test_o; }
    c->out_color_space = JCS_EXT_RGBA;                  /* grey and YCbCr both come out as RGBA with alpha 255 */
    c->do_fancy_upsampling = TRUE;
    c->do_block_smoothing = FALSE;
    c->dct_method = JDCT_ISLOW;
    c->scale_num = pick_num(d->info.width, d->info.height, x->dw, x->dh);
    c->scale_denom = 8;
    jpeg_calc_output_dimensions(c);
    if (c->output_width < x->dw || c->output_height < x->dh || c->output_width > CD_IMAGE_SIDE_MAX) {
      x->status = CD_IMAGE_CORRUPT;                     /* libjpeg read another size than the walker */
      return;
    }
    if (!cd_image_scaler_init(&x->sc, c->output_width, c->output_height, x->dw, x->dh, d->info.orientation)) {
      x->status = CD_IMAGE_NO_ROOM;
      return;
    }
    x->row = cd_alloc_try(c->output_width * 4);
    if (!x->row) { x->status = CD_IMAGE_NO_ROOM; return; }
    x->stage = J_START;
  }
  if (x->stage == J_START) {
    /* A progressive file's scans are decoded by src/cdom_image_jpegp.c, which takes each scan whole from the source and
     * walks the markers of all of them once, so it needs the rest of the file in the buffer: in pieces, the rest is
     * gathered first (its coefficients are the most of what it holds, 3 to 6 bytes a pixel, against the file's 0.3) */
    if (c->progressive_mode && d->buf && d->taken < d->size) { x->gather = 1; return; }
    if (!jpeg_start_decompress(c)) return;              /* suspended while it took in a multi-scan file's scans */
    x->stage = J_ROWS;
  }
  while (c->output_scanline < c->output_height) {
    JSAMPROW r = x->row;
    if (jpeg_read_scanlines(c, &r, 1) != 1) return;     /* suspended */
    cd_image_scaler_row(&x->sc, x->row);
    /* every byte in (a multi-scan file's output pass, or the last rows): a step every 64 rows, so a caller on the main
     * thread can let the page run between them */
    if (jpeg_input_complete(c) && ++x->since == 64 && c->output_scanline < c->output_height) {
      x->since = 0;
      d->step = 1;
      return;
    }
  }
  x->status = x->ran_dry ? CD_IMAGE_CORRUPT : CD_IMAGE_OK;   /* the file ended before libjpeg was done with it */
}

#ifdef CDOM_IMAGE_TESTING
int32_t cd_image_jpeg_fatal;   /* the last decode's fatal libjpeg error (its msg_code), -1 for too many scans, 0: none */
#endif

static jctx *jctx_new(cd_image_dec *d) {
  jctx *x = cd_alloc_try(sizeof *x);
  if (!x) return 0;
  memset(x, 0, sizeof *x);
  x->d = d;
  x->cinfo.err = &x->err;
  x->cinfo.client_data = x;
  x->err.error_exit = err_exit; x->err.emit_message = err_emit; x->err.output_message = err_out;
  x->err.format_message = err_fmt; x->err.reset_error_mgr = err_reset;
  x->src.init_source = src_init; x->src.fill_input_buffer = src_fill; x->src.skip_input_data = src_skip;
  x->src.resync_to_restart = jpeg_resync_to_restart; x->src.term_source = src_term;
  x->prog.progress_monitor = progress;
#ifdef CDOM_IMAGE_TESTING
  cd_image_jpeg_fatal = 0;
#endif
  return x;
}

/* The next n bytes at p: in the room (p is d->buf + d->used, after what libjpeg kept), or, with no room, all of the
 * rest of the file where it lies (cd_image_resize; n 0 for a step). */
uint32_t cd_image_jpeg_put(cd_image_dec *d, const uint8_t *p, uint32_t n) {
  if (!d->walk.live) walk_init(&d->walk, d->size);
  uint32_t st = walk_feed(d, p, n, (uint64_t)d->taken - n);   /* every byte through the walker first */
  if (st != CD_IMAGE_MORE) return st;
  if (d->walk.verdict != CD_IMAGE_MORE && d->walk.verdict != CD_IMAGE_OK) return d->walk.verdict;
  jctx *x = d->fmt;
  if (x && x->failed) {                                  /* libjpeg failed first: the walker's verdict decides */
    d->used = 0;
    return walk_done(d) ? x->failed : CD_IMAGE_MORE;
  }
  if (!x) {
    x = jctx_new(d);
    if (!x) return CD_IMAGE_NO_ROOM;
    d->fmt = x;
  }
  if (x->gather && d->taken < d->size) { d->used += n; return CD_IMAGE_MORE; }   /* a progressive file's rest, gathered */
  /* the source: the room holds what libjpeg kept (at its front) and the new bytes; or the caller's bytes */
  if (d->buf) {
    uint32_t drop = x->skip < n ? x->skip : n;           /* a skip past the buffer pays off first (nothing kept then) */
    x->skip -= drop;
    x->src.next_input_byte = d->buf + drop;
    x->src.bytes_in_buffer = d->used + n - drop;
  } else if (n) {
    x->src.next_input_byte = p;
    x->src.bytes_in_buffer = n;
  }
  int threw = cd_image_guard(body, x, &x->guard);
  d->warnings = (uint32_t)x->err.num_warnings;
  if (threw) {                                           /* a fatal libjpeg error, or too many scans */
    int code = x->err.msg_code;
#ifdef CDOM_IMAGE_TESTING
    cd_image_jpeg_fatal = x->too_many_scans ? -1 : code;
#endif
    st = x->too_many_scans || code == JERR_NO_BACKING_STORE ? CD_IMAGE_TOO_BIG
       : code == JERR_OUT_OF_MEMORY ? CD_IMAGE_NO_ROOM
       : CD_IMAGE_CORRUPT;
    d->step = 0;
    if (!walk_done(d)) { x->failed = st; d->used = 0; return CD_IMAGE_MORE; }   /* the probe would have spoken first */
    return st;
  }
  st = x->status;
  if (st == CD_IMAGE_OK) {
    uint8_t *o = cd_image_scaler_take(&x->sc);
    if (!o) return CD_IMAGE_CORRUPT;
    d->rgba = o; d->w = x->sc.ow; d->h = x->sc.oh;
    return CD_IMAGE_OK;
  }
  if (st != CD_IMAGE_MORE || !d->buf) return st;
  /* suspended: what libjpeg has not used moves to the room's front, and the next bytes go after it */
  uint32_t keep = (uint32_t)x->src.bytes_in_buffer;
  if (keep && x->src.next_input_byte != d->buf) memmove(d->buf, x->src.next_input_byte, keep);
  d->used = keep;
  x->src.next_input_byte = d->buf;
  if (x->gather && d->cap - d->used < d->size - d->taken) {   /* room for all of the rest */
    uint32_t cap = d->used + (d->size - d->taken);
    uint8_t *b = cd_alloc_try(cap);
    if (!b) return CD_IMAGE_NO_ROOM;
    memcpy(b, d->buf, d->used);
    cd_free(d->buf);
    d->buf = b; d->cap = cap;
    x->src.next_input_byte = b;
  }
  if (!d->step && d->used == d->cap && d->taken < d->size) return CD_IMAGE_CORRUPT;   /* a unit larger than the room */
  return CD_IMAGE_MORE;
}

#ifdef CDOM_IMAGE_TESTING
/* tests/image/prog.c's and fuzz_jpegp.c's entry, the one the module had before the fed decoder: the whole file in one
 * put where it lies, to a target of dw x dh as stored, with info's orientation (the walker's verdict as the probe's). */
uint32_t cd_image_jpeg_resize(const uint8_t *p, uint32_t len, const cd_image_info *info, uint32_t dw, uint32_t dh,
                              uint8_t **rgba, uint32_t *w, uint32_t *h) {
  cd_image_dec d;
  memset(&d, 0, sizeof d);
  d.size = len; d.taken = len; d.box_w = d.box_h = 1; d.status = CD_IMAGE_MORE; d.format = CD_IMAGE_JPEG;
  d.info.orientation = 1;
  d.test_w = dw; d.test_h = dh; d.test_o = info->orientation;
  uint32_t st = cd_image_jpeg_put(&d, p, len);
  while (st == CD_IMAGE_MORE && d.step) { d.step = 0; st = cd_image_jpeg_put(&d, p + len, 0); }
  if (st == CD_IMAGE_MORE) st = CD_IMAGE_CORRUPT;
  *rgba = st == CD_IMAGE_OK ? d.rgba : 0;
  *w = st == CD_IMAGE_OK ? d.w : 0;
  *h = st == CD_IMAGE_OK ? d.h : 0;
  if (st != CD_IMAGE_OK) cd_free(d.rgba);
  cd_image_jpeg_end(&d);
  cd_free(d.walk.exif);
  return st;
}
#endif

void cd_image_jpeg_end(cd_image_dec *d) {
  jctx *x = d->fmt;
  if (!x) return;
  /* Always: jpeg_create_decompress sets mem to 0 before its first allocation, and destroy checks it, so this is
   * right whether create finished, failed inside, or never ran (x is zeroed). */
  jpeg_destroy_decompress(&x->cinfo);
  cd_free(x->row);
  cd_image_scaler_free(&x->sc);
  cd_free(x);
  d->fmt = 0;
}
