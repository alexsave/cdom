/* The marker walk's differential check (tests/image/prog.c and tests/image/fuzz_jpegp.c, docs/image-progressive-speed.md):
 * src/cdom_image_jpegp.c walks the file's markers before the first scan is decoded to know every scan to come, and
 * skips the scans no IDCT needs. Its list must be the scans libjpeg's own read_markers reads, in order (components,
 * Ss, Se, Ah, Al), which cdom's start_pass records as libjpeg hands them over, before any skip decision; or the walk
 * gave up, and then no scan may be skipped (the decode equals libjpeg's, which the callers check).
 * The one case the lists may differ: libjpeg stopped with an error the walk does not model, after reading only a part
 * of what the walk read (a table a scan names that is missing or bad, the MCU size, the memory budget, the scan cap),
 * so that libjpeg's list is a prefix of the walk's. Every other error libjpeg stops with after the walk (a table
 * segment it refuses, a marker it does not take, a scan header or band it refuses) is one the walk models, and the walk
 * must have given up at it, even where the lists are the same.
 * walk_reset before the decode; walk_check after it, with the scans skipped in it; 0 when it holds, else what failed. */
#include <stdint.h>
#include <stdio.h>
#include "cdom_image.h"
#include "cdom_image_int.h"
#include "jpeglib.h"           /* before jerror.h: its codes depend on JPEG_LIB_VERSION (jconfig.h) */
#include "jerror.h"

static inline void walk_reset(void) {
    cd_image_jpegp_walk_n = CD_IMAGE_JPEGP_NOT_RUN;
    cd_image_jpegp_read_n = 0;
    cd_image_jpegp_in_start = 0;
}

static inline const char *walk_check(uint32_t skipped) {
    int32_t wn = cd_image_jpegp_walk_n;
    uint32_t rn = cd_image_jpegp_read_n;
    if (rn > CD_IMAGE_SCANS_MAX + 2) return "libjpeg read more scans than the record holds";
    if (wn == CD_IMAGE_JPEGP_NOT_RUN) return rn ? "libjpeg read scans and there was no walk" : 0;
    if (wn == CD_IMAGE_JPEGP_GAVE_UP) return skipped ? "the walk gave up and a scan was skipped" : 0;
    if (wn < 0) return "the walk's count is not a count";
    uint32_t k = (uint32_t)wn < rn ? (uint32_t)wn : rn;
    for (uint32_t i = 0; i < k; i++)
        if (cd_image_jpegp_walk_list[i] != cd_image_jpegp_read_list[i]) return "a scan of the walk is not the one libjpeg read";
    if (rn > (uint32_t)wn) return "libjpeg read a scan the walk did not see";
    int f = cd_image_jpeg_fatal;
    int unmodelled = f == -1 || f == JERR_NO_BACKING_STORE || f == JERR_OUT_OF_MEMORY || f == JERR_WIDTH_OVERFLOW ||
                     f == JERR_NO_QUANT_TABLE || f == JERR_BAD_MCU_SIZE ||
                     ((f == JERR_NO_HUFF_TABLE || f == JERR_BAD_HUFF_TABLE) && cd_image_jpegp_in_start);
    if (f && !unmodelled) return "libjpeg stopped with an error the walk did not give up at";
    if ((uint32_t)wn != rn && !unmodelled) return "the walk saw scans libjpeg did not read";
    return 0;
}
