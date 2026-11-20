/* The image module in wasm, for tools/image.mjs (tests/image/run.sh): the page puts a file's bytes in a block of
 * room's and calls resize, which answers the status and leaves the size and the RGBA block in out[]; drop frees it.
 * Built twice by tests/image/wasm.mk, once each way out of a libjpeg error (CDOM_IMAGE_SJLJ js and eh), and once
 * more with the heap capped (CDOM_HEAP_PAGES_MAX), for NO_ROOM. */
#include "cdom.h"
#include "cdom_cb.h"
#include "cdom_image.h"
#include "cdom_image_file.h"
#include "cdom_job.h"
#define EXPORT(n) __attribute__((export_name(n)))

static uint32_t out[3];
EXPORT("room") void *room(uint32_t n) { return cd_alloc_try(n ? n : 1); }
EXPORT("drop") void drop(void *p) { cd_free(p); }
EXPORT("out") uint32_t *out_at(void) { return out; }
EXPORT("resize") uint32_t resize(const uint8_t *p, uint32_t len, uint32_t bw, uint32_t bh) {
  uint8_t *rgba;
  uint32_t st = cd_image_resize(p, len, bw, bh, &rgba, &out[0], &out[1]);
  out[2] = (uint32_t)(uintptr_t)rgba;
  return st;
}
EXPORT("probe") uint32_t probe(const uint8_t *p, uint32_t len) {
  cd_image_info i;
  uint32_t st = cd_image_probe(p, len, &i);
  out[0] = i.shown_w; out[1] = i.shown_h; out[2] = i.orientation;
  return st;
}
/* The stack pointer's place now: the same before and after any number of errors, or the guard left it lower. */
EXPORT("sp") uint32_t sp(void) { return (uint32_t)(uintptr_t)__builtin_frame_address(0); }
/* ---- the chunked read (the design's tests 11 and 13): files picked into the input, decoded by cd_image_file_cb on
 * the page and by tests/image/job.c in a job (cd_job_pieces_cb, or cd_job_file_cb for the whole file) ---- */
static cd_id pick;
void cd_main(void) {
  cd_open("input");
  cd_attr("type", "file");
  cd_attr("id", "pick");
  cd_attr("multiple", "");
  cd_name_el(pick = cd_id_new());
  cd_close();
}
/* The last answer: status (-1 while pending), w, h, the pixels (or, from a job, its answer's bytes and length). */
static struct { int32_t status; uint32_t w, h, len; uint8_t *p; uint32_t calls, id, report, pages; } F;
EXPORT("file_rec") uint32_t file_rec(uint32_t f) {
  uint32_t v[] = {(uint32_t)F.status, F.w, F.h, (uint32_t)(uintptr_t)F.p, F.len, F.calls, F.pages};
  return v[f];
}
static void file_done(void *ctx, uint32_t st, uint8_t *rgba, uint32_t w, uint32_t h) {
  (void)ctx;
  F.status = (int32_t)st; F.w = w; F.h = h; F.p = rgba; F.len = w * h * 4; F.calls++;
}
EXPORT("file_start") void file_start(uint32_t index, uint32_t bw, uint32_t bh) {
  cd_free(F.p);
  F.p = 0; F.status = -1; F.w = F.h = F.len = 0;
  cd_image_file_cb(pick, index, bw, bh, file_done, 0, &F.id);
}
EXPORT("file_stop") void file_stop(void) { cd_image_file_stop(F.id); }
static void job_done(void *ctx, uint32_t st, const uint8_t *p, uint32_t len) {
  (void)ctx;
  F.calls++;
  F.status = (int32_t)st;
  if (st != CD_JOB_OK) return;
  if (len == 1) { F.status = 100 + (int32_t)p[0]; return; }   /* the image's status, not OK */
  if (len % 4 == 0 && F.report) {   /* report: the pixels, then the job's pages, w and h */
    const uint8_t *t = p + len - 12;
    F.pages = (uint32_t)t[0] | (uint32_t)t[1] << 8 | (uint32_t)t[2] << 16;
    F.w = (uint32_t)t[4] | (uint32_t)t[5] << 8 | (uint32_t)t[6] << 16;
    F.h = (uint32_t)t[8] | (uint32_t)t[9] << 8 | (uint32_t)t[10] << 16;
    len -= 12;
  }
  F.p = cd_alloc_try(len);
  if (!F.p) { F.status = 99; return; }
  __builtin_memcpy(F.p, p, len);
  F.len = len;
}
/* pieces 1: cd_job_pieces_cb; 0: cd_job_file_cb (the job reads the file whole, then run). report 1: the job answers
 * its memory at its end too (file_rec 6, in pages) and the size. */
EXPORT("job_start") void job_start(uint32_t index, uint32_t bw, uint32_t bh, uint32_t pieces, uint32_t report) {
  cd_free(F.p);
  F.p = 0; F.status = -1; F.w = F.h = F.len = F.pages = 0; F.report = report;
  uint8_t args[5] = {(uint8_t)bw, (uint8_t)(bw >> 8), (uint8_t)bh, (uint8_t)(bh >> 8), (uint8_t)report};
  F.id = pieces ? cd_job_pieces_cb("imgjob.job.wasm", pick, index, args, 5, job_done, 0)
                : cd_job_file_cb("imgjob.job.wasm", pick, index, args, 5, job_done, 0);
}
EXPORT("job_stop") void job_stop(void) { cd_job_stop(F.id); }
/* The highest callback slot among 8 taken now (then freed): the same after a stop as before the start, whatever
 * order the slots were freed in, or a slot was kept. */
EXPORT("slot_probe") uint32_t slot_probe(void) {
  uint32_t t[8], most = 0;
  for (int i = 0; i < 8; i++) { t[i] = cd_cb_new(0, 0); if ((t[i] & 0xffff) > most) most = t[i] & 0xffff; }
  for (int i = 0; i < 8; i++) cd_cb_drop(t[i]);
  return most;
}
