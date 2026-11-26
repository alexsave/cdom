/* The image module's size, by format (bench/image/sizes/run.sh): one app, built with no image code (NONE), with
 * cd_image_resize (both formats), and with one format's internal entry point only (ONLY_JPEG, ONLY_PNG), so the linker
 * drops the other; and with the file module: a picked file read whole into cd_image_resize (FILE_READ), or decoded a
 * piece at a time by cd_image_file_cb (FILE_GLUE). */
#include "cdom.h"
#include "cdom_image.h"
#include "cdom_image_int.h"
#if defined(FILE_READ) || defined(FILE_GLUE)
#include "cdom_file.h"
#include "cdom_image_file.h"
static void got(void *ctx, uint32_t st, const uint8_t *p, uint32_t len) {
    (void)ctx; (void)st;
    uint8_t *rgba; uint32_t w, h;
    if (!cd_image_resize(p, len, 900, 900, &rgba, &w, &h)) cd_free(rgba);
}
static void done(void *ctx, uint32_t st, uint8_t *rgba, uint32_t w, uint32_t h) { (void)ctx; (void)st; (void)w; (void)h; cd_free(rgba); }
__attribute__((export_name("pick"))) void pick(cd_id input) {
#ifdef FILE_READ
    cd_file_read_cb(input, 0, 1u << 30, got, 0);
    (void)done;
#else
    uint32_t job;
    cd_image_file_cb(input, 0, 900, 900, done, 0, &job);
    (void)got;
#endif
}
#endif
__attribute__((unused)) static uint32_t out[3];
__attribute__((export_name("room"))) void *room(uint32_t n) { return cd_alloc_try(n ? n : 1); }
__attribute__((export_name("drop"))) void drop(void *p) { cd_free(p); }
__attribute__((export_name("resize"))) uint32_t resize(const uint8_t *p, uint32_t len, uint32_t bw, uint32_t bh) {
#if defined(NONE)
    (void)p; (void)len; (void)bw; (void)bh; return 0;
#elif defined(ONLY_JPEG) || defined(ONLY_PNG)
    cd_image_info i = {0}; uint8_t *rgba;
    /* the fed decoder's block on the stack, the format set, the whole file in one put (as cd_image_resize does) */
    cd_image_dec d = {0};
    (void)i; (void)rgba;
    d.size = len; d.taken = len; d.box_w = bw; d.box_h = bh; d.status = CD_IMAGE_MORE; d.info.orientation = 1;
#ifdef ONLY_JPEG
    d.format = CD_IMAGE_JPEG;
    uint32_t st = cd_image_jpeg_put(&d, p, len);
    while (st == CD_IMAGE_MORE && d.step) { d.step = 0; st = cd_image_jpeg_put(&d, 0, 0); }
    cd_image_jpeg_end(&d);
#else
    d.format = CD_IMAGE_PNG;
    uint32_t st = cd_image_png_put(&d, p, len);
    cd_image_png_end(&d);
#endif
    out[0] = d.w; out[1] = d.h; out[2] = (uint32_t)(uintptr_t)d.rgba; return st;
#else
    uint8_t *rgba; uint32_t st = cd_image_resize(p, len, bw, bh, &rgba, &out[0], &out[1]);
    out[2] = (uint32_t)(uintptr_t)rgba; return st;
#endif
}
void cd_main(void) {}
