/* The unfilter microbench (docs/image-png-speed.md, "Paeth as a wavefront"): p12.png's filtered rows (run.mjs hands them
 * over) unfiltered in wasm, the way the PNG sink does, by one of the loops; run.mjs times each call in node (the same V8 as
 * Chrome). Built by build.sh with cdom.mk's flag set and src/cdom_image_loops.c as the module builds it. */
#include <stdint.h>
#include <string.h>
#include "cdom_image_int.h"
#define EXPORT(n) __attribute__((export_name(n)))

static uint8_t *orig, *work, *zero;
static uint32_t w, h, bpp, pitch;
static uint8_t *grab(uint32_t n) {   /* memory past what the module had, a page at a time; never given back */
    uint32_t pages = (n + 65535) / 65536;
    int32_t at = __builtin_wasm_memory_grow(0, pages);
    return at < 0 ? 0 : (uint8_t *)((uintptr_t)at * 65536);
}
/* Room for h rows of w pixels of b bytes, each its filter byte first; answers where run.mjs writes them. */
EXPORT("setup") uint8_t *setup(uint32_t ww, uint32_t hh, uint32_t b) {
    w = ww; h = hh; bpp = b; pitch = w * b + 1;
    orig = grab(pitch * h); work = grab(pitch * h); zero = grab(pitch);
    return orig;
}
EXPORT("reset") void reset(void) { memcpy(work, orig, (size_t)pitch * h); }
/* The rows unfiltered: loop 0 cd_image_unfilter_scalar a row at a time, 1 cd_image_unfilter_simd a row at a time,
 * 2 cd_image_unfilter_run_scalar (a row at a time too), 3 cd_image_unfilter_run_simd, the runs at most most rows; most
 * 0: 2, 1, 2, 1, ... (a run of 3 as a 2-row run and one row). Answers a byte sum of the result, which run.mjs compares between loops. */
EXPORT("unfilter") uint32_t unfilter(uint32_t loop, uint32_t most) {
    uint32_t done = 0, flip = 0;
    while (done < h) {
        uint8_t *row = work + (size_t)done * pitch;
        const uint8_t *prev = done ? row - pitch + 1 : zero + 1;
        uint32_t avail = most ? most : (flip ^= 1) ? 2 : 1, k = 1;
        if (avail > h - done) avail = h - done;
        if (loop == 0) cd_image_unfilter_scalar(row[0], row + 1, prev, pitch - 1, bpp);
#if CDOM_IMAGE_SIMD
        else if (loop == 1) cd_image_unfilter_simd(row[0], row + 1, prev, pitch - 1, bpp);
        else if (loop == 3) k = cd_image_unfilter_run_simd(row, pitch, prev, pitch - 1, bpp, avail);
#endif
        else k = cd_image_unfilter_run_scalar(row, pitch, prev, pitch - 1, bpp, avail);
        if (!k) return 0;
        done += k;
    }
    uint32_t s = 0;
    for (size_t i = 0; i < (size_t)pitch * h; i++) s = s * 31 + work[i];
    return s;
}
