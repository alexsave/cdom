/* The progressive decoder's differential test (tests/image/prog.sh): every file of a list decoded by this branch's
 * progressive path (src/cdom_image_jpegp.c) and by libjpeg-turbo's own (jdphuff.c and jdcoefct.c, built in the same
 * binary with every symbol renamed ref_*), at every scale from 1/8 to 8/8, and each pair compared: the status, the size,
 * and every byte of the image. The decode is cd_image_jpeg_resize with the target the size of the scaled decode, so the
 * scaler copies the decoded rows as they are (the image compared is libjpeg's output itself). Both run on
 * tests/image/host_alloc.c's counted heap; the peak of each is compared too (the new store is never larger).
 * Usage: prog DIR [LIST]    (LIST defaults to DIR/list.txt)
 * Prints a FAIL line for each difference and a summary; exits 1 on any. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include "cdom_image.h"
#include "cdom_image_int.h"
#include "host_alloc.h"
#include "walkcheck.h"

uint32_t ref_cd_image_jpeg_resize(const uint8_t *p, uint32_t len, const cd_image_info *info, uint32_t dw, uint32_t dh,
                                  uint8_t **rgba, uint32_t *w, uint32_t *h);

/* ---- a progressive file in buffered-image mode, as jpeg_read_coefficients would ask for it (jdtrans.c sets
 * buffered_image and calls jinit_d_coef_controller with need_full_buffer TRUE): src/cdom_image_jpegp.c must refuse it
 * when the decoder is set up (JERR_NOT_COMPILED), before any scan, instead of a controller without coefficient arrays */
typedef struct { struct jpeg_error_mgr pub; jmp_buf jb; } berr;
static void b_exit(j_common_ptr c) { longjmp(((berr *)c->err)->jb, 1); }
static void b_emit(j_common_ptr c, int level) { (void)c; (void)level; }
static void b_out(j_common_ptr c) { (void)c; }
static void b_fmt(j_common_ptr c, char *buf) { (void)c; buf[0] = 0; }
static void b_reset(j_common_ptr c) { c->err->msg_code = 0; }
static void b_init(j_decompress_ptr c) { (void)c; }
static boolean b_fill(j_decompress_ptr c) {
    static const JOCTET eoi[2] = {0xff, JPEG_EOI};
    c->src->next_input_byte = eoi; c->src->bytes_in_buffer = 2;
    return TRUE;
}
static void b_skip(j_decompress_ptr c, long n) {
    if (n > 0) { if ((size_t)n > c->src->bytes_in_buffer) n = (long)c->src->bytes_in_buffer; c->src->next_input_byte += n; c->src->bytes_in_buffer -= (size_t)n; }
}
static void b_term(j_decompress_ptr c) { (void)c; }

static int buffered_code;
static const char *buffered_refused(const uint8_t *p, size_t n) {
    static struct jpeg_decompress_struct c;
    static berr e;
    static struct jpeg_source_mgr src;
    static const char *res;
    buffered_code = 0;
    memset(&c, 0, sizeof c); memset(&e, 0, sizeof e); memset(&src, 0, sizeof src);
    e.pub.error_exit = b_exit; e.pub.emit_message = b_emit; e.pub.output_message = b_out;
    e.pub.format_message = b_fmt; e.pub.reset_error_mgr = b_reset;
    src.init_source = b_init; src.fill_input_buffer = b_fill; src.skip_input_data = b_skip;
    src.resync_to_restart = jpeg_resync_to_restart; src.term_source = b_term;
    src.next_input_byte = p; src.bytes_in_buffer = n;
    c.err = &e.pub;
    walk_reset();
    if (setjmp(e.jb)) {
        buffered_code = e.pub.msg_code;
        res = e.pub.msg_code != JERR_NOT_COMPILED ? "stopped with another error than JERR_NOT_COMPILED"
            : cd_image_jpegp_read_n ? "stopped only after a scan had started" : 0;
    } else {
        jpeg_create_decompress(&c);
        c.src = &src;
        jpeg_read_header(&c, TRUE);
        if (!c.progressive_mode) res = "the file is not progressive";
        else {
            c.buffered_image = TRUE;
            c.scale_num = 1; c.scale_denom = 8;
            jpeg_start_decompress(&c);
            res = "taken (a coefficient controller without coefficient arrays)";
        }
    }
    jpeg_destroy_decompress(&c);
    return res;
}

static uint8_t *read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(l ? (size_t)l : 1);
    if (fread(b, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(b); return 0; }
    fclose(f);
    *n = (size_t)l;
    return b;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: prog DIR [LIST]\n"); return 2; }
    char path[4096], line[1024];
    snprintf(path, sizeof path, "%s", argc > 2 ? argv[2] : "");
    if (argc <= 2) snprintf(path, sizeof path, "%s/list.txt", argv[1]);
    FILE *list = fopen(path, "r");
    if (!list) { printf("FAIL no list %s\n", path); return 1; }
    unsigned files = 0, pairs = 0, fails = 0, ok = 0, refused = 0, statuses[8] = {0};
    unsigned walked = 0, gave_up = 0, walk_prefix = 0, buffered_tried = 0;
    unsigned long long bytes = 0, peak_new = 0, peak_ref = 0;
    while (fgets(line, sizeof line, list)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        snprintf(path, sizeof path, "%s/%s", argv[1], line);
        size_t len;
        uint8_t *p = read_file(path, &len);
        if (!p) { printf("FAIL cannot read %s\n", path); fails++; continue; }
        files++;
        cd_image_info info;
        memset(&info, 0, sizeof info);
        uint32_t pst = cd_image_jpeg_probe(p, (uint32_t)len, &info);
        if (pst != CD_IMAGE_OK) { refused++; free(p); continue; }
        info.orientation = 1;                     /* the decoded rows as stored */
        if (info.progressive && !buffered_tried) {  /* the first progressive file */
            buffered_tried = 1;
            const char *bf = buffered_refused(p, len);
            if (bf) { fails++; printf("FAIL %s in buffered-image mode: %s (msg_code %d)\n", line, bf, buffered_code); }
            else printf("ok   %s in buffered-image mode: refused with JERR_NOT_COMPILED before any scan\n", line);
        }
        for (uint32_t n = 1; n <= 8; n++) {
            uint32_t dw = (uint32_t)(((uint64_t)info.width * n + 7) / 8), dh = (uint32_t)(((uint64_t)info.height * n + 7) / 8);
            uint8_t *a = 0, *b = 0;
            uint32_t aw = 0, ah = 0, bw = 0, bh = 0;
            size_t base = host_live;
            host_reset(SIZE_MAX);
            walk_reset();
            uint32_t skipped0 = cd_image_jpegp_skipped;
            uint32_t sa = cd_image_jpeg_resize(p, (uint32_t)len, &info, dw, dh, &a, &aw, &ah);
            size_t pa = host_peak - base;
            const char *wf = walk_check(cd_image_jpegp_skipped - skipped0);
            if (wf) {
                fails++;
                printf("FAIL %s at %u/8: walk: %s (walk %d scans, libjpeg read %u, fatal %d)\n", line, n, wf, cd_image_jpegp_walk_n,
                       cd_image_jpegp_read_n, cd_image_jpeg_fatal);
            }
            if (!wf && cd_image_jpegp_walk_n == CD_IMAGE_JPEGP_GAVE_UP && strstr(line, "-follow")) {
                fails++;                              /* tests/image/progcorpus.py: libjpeg reads all of this file's markers */
                printf("FAIL %s at %u/8: walk: gave up on markers libjpeg reads without an error\n", line, n);
            }
            if (cd_image_jpegp_walk_n == CD_IMAGE_JPEGP_GAVE_UP) gave_up++;
            else if (cd_image_jpegp_walk_n >= 0) { walked++; if (cd_image_jpegp_read_n != (uint32_t)cd_image_jpegp_walk_n) walk_prefix++; }
            host_reset(SIZE_MAX);
            uint32_t sb = ref_cd_image_jpeg_resize(p, (uint32_t)len, &info, dw, dh, &b, &bw, &bh);
            size_t pb = host_peak - base;
            pairs++;
            statuses[sa < 8 ? sa : 7]++;
            int same = sa == sb && aw == bw && ah == bh && (sa != CD_IMAGE_OK || !memcmp(a, b, (size_t)aw * ah * 4));
            /* the one intended difference: a file whose whole coefficients exceed the budget, which the smaller store holds */
            if (!same && sb == CD_IMAGE_TOO_BIG && sa == CD_IMAGE_OK) same = 1;
            if (!same) {
                fails++;
                long at = -1;
                if (sa == CD_IMAGE_OK && sb == CD_IMAGE_OK && aw == bw && ah == bh)
                    for (size_t i = 0; i < (size_t)aw * ah * 4; i++) if (a[i] != b[i]) { at = (long)(i / 4); break; }
                printf("FAIL %s at %u/8: new status %u %ux%u, libjpeg status %u %ux%u, first differing pixel %ld\n", line, n, sa, aw, ah, sb, bw, bh, at);
            }
            if (sa == CD_IMAGE_OK) { ok++; bytes += (unsigned long long)aw * ah * 4; }
            if (sa == CD_IMAGE_OK && sb == CD_IMAGE_OK && info.progressive) {
                peak_new += pa; peak_ref += pb;
                /* the store against libjpeg's: never more, beside the decoder's own tables (under 64 KB) */
                if (pa > pb + 65536) { fails++; printf("FAIL %s at %u/8: held %zu bytes, libjpeg %zu\n", line, n, pa, pb); }
            }
            if (sa == CD_IMAGE_OK) cd_free(a);
            if (sb == CD_IMAGE_OK) cd_free(b);
            if (host_live != base) { fails++; printf("FAIL %s at %u/8: %zu bytes left allocated\n", line, n, host_live - base); }
        }
        free(p);
    }
    fclose(list);
    printf("prog: %u files (%u refused at the probe), %u decodes compared, %u of them images (%llu bytes), statuses OK %u TOO_BIG %u CORRUPT %u NO_ROOM %u; scans skipped %u; walks %u equal to libjpeg's scans (%u of them a longer list where libjpeg stopped first), %u gave up; peak held, progressive, new %.1f MB against libjpeg's %.1f MB; %u differences\n",
           files, refused, pairs, ok, bytes, statuses[CD_IMAGE_OK], statuses[CD_IMAGE_TOO_BIG], statuses[CD_IMAGE_CORRUPT], statuses[CD_IMAGE_NO_ROOM],
           cd_image_jpegp_skipped, walked, walk_prefix, gave_up, peak_new / 1e6, peak_ref / 1e6, fails);
    return fails ? 1 : 0;
}
