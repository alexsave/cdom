/* The chunked-read bench's app (bench/chunked/run.mjs): the file picked in its input, decoded to fit 900 x 900 by one
 * of four arms, the end's time (cd_now) and the answer recorded for the driver:
 *   0 whole   today's: cd_file_read_cb reads the file whole into C memory, then cd_image_resize
 *   1 file    cd_image_file_cb on the main thread, a piece per task
 *   2 pieces  cd_job_pieces_cb, the image job (tests/image/job.c) fed the file a piece at a time
 *   3 job     cd_job_file_cb, today's job: the worker reads the file whole into the job's memory, then run
 * The job arms ask the job for its memory at its end (job.c's report). */
#include "cdom.h"
#include "cdom_file.h"
#include "cdom_image.h"
#include "cdom_image_file.h"
#include "cdom_job.h"
#define EXPORT(n) __attribute__((export_name(n)))
#define BOX 900u

static cd_id pick;
void cd_main(void) {
    cd_open("input");
    cd_attr("type", "file");
    cd_attr("id", "pick");
    cd_name_el(pick = cd_id_new());
    cd_close();
}
static struct { int32_t status; uint32_t w, h, job_pages; double end; } R;
EXPORT("rec") double rec(uint32_t f) {
    double v[] = {R.status, R.w, R.h, R.job_pages, R.end};
    return v[f];
}
static void done(uint32_t st, uint32_t w, uint32_t h) { R.status = (int32_t)st; R.w = w; R.h = h; R.end = cd_now(); }
static void whole_got(void *ctx, uint32_t st, const uint8_t *p, uint32_t len) {
    (void)ctx;
    if (st != CD_FILE_OK) { done(100 + st, 0, 0); return; }
    uint8_t *rgba;
    uint32_t w, h;
    uint32_t s = cd_image_resize(p, len, BOX, BOX, &rgba, &w, &h);
    done(s, w, h);
    cd_free(rgba);
}
static void file_got(void *ctx, uint32_t st, uint8_t *rgba, uint32_t w, uint32_t h) {
    (void)ctx;
    done(st, w, h);
    cd_free(rgba);
}
static void job_got(void *ctx, uint32_t st, const uint8_t *p, uint32_t len) {
    (void)ctx;
    if (st != CD_JOB_OK) { done(200 + st, 0, 0); return; }
    if (len == 1) { done(p[0], 0, 0); return; }
    const uint8_t *t = p + len - 12;   /* the pixels, then the job's pages, w and h */
    R.job_pages = (uint32_t)t[0] | (uint32_t)t[1] << 8 | (uint32_t)t[2] << 16;
    done(CD_IMAGE_OK, (uint32_t)t[4] | (uint32_t)t[5] << 8 | (uint32_t)t[6] << 16, (uint32_t)t[8] | (uint32_t)t[9] << 8 | (uint32_t)t[10] << 16);
}
EXPORT("go") void go(uint32_t arm) {
    static const uint8_t args[5] = {BOX & 255, BOX >> 8, BOX & 255, BOX >> 8, 1};
    uint32_t id;
    R.status = -1; R.end = 0;
    if (arm == 0) cd_file_read_cb(pick, 0, 0xffffffffu, whole_got, 0);
    else if (arm == 1) cd_image_file_cb(pick, 0, BOX, BOX, file_got, 0, &id);
    else if (arm == 2) cd_job_pieces_cb("imgjob.job.wasm", pick, 0, args, 5, job_got, 0);
    else cd_job_file_cb("imgjob.job.wasm", pick, 0, args, 5, job_got, 0);
}
EXPORT("piece") uint32_t piece(void) { return CD_IMAGE_PIECE; }
