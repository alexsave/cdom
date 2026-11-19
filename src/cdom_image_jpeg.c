/* cdom_image's JPEG path: libjpeg-turbo's decoder, row by row, at the smallest scale (n/8) whose output is still at
 * least the target size, into the streaming scaler. The file is read where it is (a source over the caller's bytes).
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

/* ---- one decode's state: everything the error path must release, reached through cinfo->client_data ---- */

typedef struct {
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr err;
  struct jpeg_source_mgr src;
  struct jpeg_progress_mgr prog;
  void *guard;                /* the cd_image_guard the error exit leaves through */
  const uint8_t *p;
  uint32_t len;
  const cd_image_info *info;
  uint32_t dw, dh;
  cd_image_scaler sc;
  uint8_t *row;
  uint32_t status;            /* the body's answer, when it returns */
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
#ifdef CDOM_IMAGE_SIMDE   /* the SIMD build switches merged upsampling on (cdom.mk), and jdmaster.c names this one too */
void j12init_merged_upsampler(j_decompress_ptr c) { ERREXIT1(c, JERR_BAD_PRECISION, 12); }
#endif

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

/* ---- the source: the caller's bytes ---- */

static void src_init(j_decompress_ptr c) { (void)c; }
static boolean src_fill(j_decompress_ptr c) {
  static const JOCTET eoi[2] = {0xff, JPEG_EOI};       /* what libjpeg's memory source does: end the image */
  jctx *x = c->client_data;
  x->ran_dry = 1;
  x->src.next_input_byte = eoi;
  x->src.bytes_in_buffer = 2;
  return TRUE;
}
static void src_skip(j_decompress_ptr c, long n) {
  jctx *x = c->client_data;
  if (n <= 0) return;
  if ((size_t)n > x->src.bytes_in_buffer) n = (long)x->src.bytes_in_buffer;
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

/* The decode, under the guard: every libjpeg call is here. It sets x->status when it returns. */
static void body(void *v) {
  jctx *x = v;
  struct jpeg_decompress_struct *c = &x->cinfo;
  jpeg_create_decompress(c);
  c->src = &x->src;
  c->progress = &x->prog;
  jpeg_read_header(c, TRUE);
  c->out_color_space = JCS_EXT_RGBA;                  /* grey and YCbCr both come out as RGBA with alpha 255 */
  c->do_fancy_upsampling = TRUE;
  c->do_block_smoothing = FALSE;
  c->dct_method = JDCT_ISLOW;
  c->scale_num = pick_num(x->info->width, x->info->height, x->dw, x->dh);
  c->scale_denom = 8;
  jpeg_calc_output_dimensions(c);
  if (c->output_width < x->dw || c->output_height < x->dh || c->output_width > CD_IMAGE_SIDE_MAX) {
    x->status = CD_IMAGE_CORRUPT;                     /* libjpeg read another size than the probe */
    return;
  }
  if (!cd_image_scaler_init(&x->sc, c->output_width, c->output_height, x->dw, x->dh, x->info->orientation)) {
    x->status = CD_IMAGE_NO_ROOM;
    return;
  }
  x->row = cd_alloc_try(c->output_width * 4);
  if (!x->row) { x->status = CD_IMAGE_NO_ROOM; return; }
  jpeg_start_decompress(c);
  while (c->output_scanline < c->output_height) {
    JSAMPROW r = x->row;
    if (jpeg_read_scanlines(c, &r, 1) != 1) break;
    cd_image_scaler_row(&x->sc, x->row);
  }
  x->status = x->ran_dry ? CD_IMAGE_CORRUPT : CD_IMAGE_OK;   /* the file ended before libjpeg was done with it */
}

uint32_t cd_image_jpeg_resize(const uint8_t *p, uint32_t len, const cd_image_info *info, uint32_t dw, uint32_t dh,
                              uint8_t **rgba, uint32_t *w, uint32_t *h) {
  jctx *x = cd_alloc_try(sizeof *x);
  if (!x) return CD_IMAGE_NO_ROOM;
  memset(x, 0, sizeof *x);
  x->p = p; x->len = len; x->info = info; x->dw = dw; x->dh = dh;
  x->cinfo.err = &x->err;
  x->cinfo.client_data = x;
  x->err.error_exit = err_exit; x->err.emit_message = err_emit; x->err.output_message = err_out;
  x->err.format_message = err_fmt; x->err.reset_error_mgr = err_reset;
  x->src.init_source = src_init; x->src.fill_input_buffer = src_fill; x->src.skip_input_data = src_skip;
  x->src.resync_to_restart = jpeg_resync_to_restart; x->src.term_source = src_term;
  x->src.next_input_byte = p; x->src.bytes_in_buffer = len;
  x->prog.progress_monitor = progress;
  uint32_t st;
  if (cd_image_guard(body, x, &x->guard)) {           /* a fatal libjpeg error, or too many scans */
    int code = x->err.msg_code;
    st = x->too_many_scans || code == JERR_NO_BACKING_STORE ? CD_IMAGE_TOO_BIG
       : code == JERR_OUT_OF_MEMORY ? CD_IMAGE_NO_ROOM
       : CD_IMAGE_CORRUPT;
  } else st = x->status;
  /* Always: jpeg_create_decompress sets mem to 0 before its first allocation, and destroy checks it, so this is
   * right whether create finished, failed inside, or never ran. */
  jpeg_destroy_decompress(&x->cinfo);
  cd_free(x->row);
  if (st == CD_IMAGE_OK) {
    uint8_t *o = cd_image_scaler_take(&x->sc);
    if (o) { *rgba = o; *w = x->sc.ow; *h = x->sc.oh; }
    else st = CD_IMAGE_CORRUPT;
  } else cd_image_scaler_free(&x->sc);
  cd_free(x);
  return st;
}
