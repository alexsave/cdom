/* The inflate libraries on one PNG's zlib stream, natively (docs/image-png-speed.md, P1 and the candidates).
 * Each arm inflates the stream (the PNG's IDAT payloads, joined) through its own streaming interface into a bounded
 * buffer, the adler32 checked, and the time of each run is printed; libdeflate has no streaming interface and
 * inflates into one whole-output buffer (a reference only). Built by bench/image/inflate/native.sh.
 * Usage: native <file.png> <arm> [runs=21]   arms: tinfl zlib zng libdeflate cdom cdom-pieces */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cdom_image_miniz.h"
#include "cdom_image_int.h"
#ifdef HAVE_ZLIB
#include "zlib.h"
#endif
#ifdef HAVE_ZNG
int zng_inflate_all(const uint8_t *stream, size_t n, size_t *made);   /* native_zng.c: zlib-ng's header and zlib's clash */
#endif
#ifdef HAVE_LIBDEFLATE
#include "libdeflate.h"
#endif

static uint8_t *file; static size_t file_n;
static const uint8_t *piece_p[100000]; static uint32_t piece_n[100000]; static uint32_t pieces;
static uint8_t *stream; static size_t stream_n, out_n;
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static size_t made;   /* bytes out, checked against the expected size */

static int tinfl_arm(void) {
    static tinfl_decompressor inf; static uint8_t dict[TINFL_LZ_DICT_SIZE];
    tinfl_init(&inf);
    size_t in_pos = 0, ofs = 0;
    for (;;) {
        size_t in_n = stream_n - in_pos, o_n = TINFL_LZ_DICT_SIZE - ofs;
        tinfl_status st = tinfl_decompress(&inf, stream + in_pos, &in_n, dict, dict + ofs, &o_n, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT);
        in_pos += in_n; made += o_n; ofs = (ofs + o_n) & (TINFL_LZ_DICT_SIZE - 1);
        if (st == TINFL_STATUS_DONE) return 0;
        if (st < 0) return 1;
    }
}
#ifdef HAVE_ZLIB
static int zlib_arm(void) {
    static uint8_t o[65536]; z_stream z; memset(&z, 0, sizeof z);
    if (inflateInit(&z) != Z_OK) return 1;
    z.next_in = stream; z.avail_in = (uInt)stream_n;
    int r;
    do { z.next_out = o; z.avail_out = sizeof o; r = inflate(&z, Z_NO_FLUSH); made += sizeof o - z.avail_out; } while (r == Z_OK);
    inflateEnd(&z);
    return r != Z_STREAM_END;
}
#endif
#ifdef HAVE_ZNG
static int zng_arm(void) { return zng_inflate_all(stream, stream_n, &made); }
#endif
#ifdef HAVE_LIBDEFLATE
static int libdeflate_arm(void) {
    static struct libdeflate_decompressor *d; static uint8_t *o;
    if (!d) { d = libdeflate_alloc_decompressor(); o = malloc(out_n); }
    size_t got;
    if (libdeflate_zlib_decompress(d, stream, stream_n, o, out_n, &got) != LIBDEFLATE_SUCCESS) return 1;
    made += got;
    return 0;
}
#endif
static uint32_t at;
static int more_one(void *c, const uint8_t **p, uint32_t *n) { (void)c; if (at) return 0; at = 1; *p = stream; *n = (uint32_t)stream_n; return 1; }
static int more_pieces(void *c, const uint8_t **p, uint32_t *n) { (void)c; if (at == pieces) return 0; *p = piece_p[at]; *n = piece_n[at]; at++; return 1; }
static int64_t drop_all(void *c, uint8_t *b, uint32_t n, int last) {
    (void)c; (void)b;
    uint32_t d = last ? n : n > CD_INFLATE_WINDOW ? n - CD_INFLATE_WINDOW : 0;
    made += d;
    return d;
}
static int cdom_arm(int by_pieces) {
    static cd_inflate z; static uint8_t buf[CD_INFLATE_WINDOW + 262144];
    at = 0;
    return cd_inflate_zlib(&z, buf, sizeof buf, by_pieces ? more_pieces : more_one, drop_all, 0) != CD_INFLATE_OK;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: native file.png arm [runs]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END); file_n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc(file_n);
    if (fread(file, 1, file_n, f) != file_n) return 2;
    fclose(f);
    stream = malloc(file_n);
    uint32_t w = be32(file + 16), h = be32(file + 20), bd = file[24], ct = file[25];
    uint32_t ch = ct == 2 ? 3 : ct == 6 ? 4 : ct == 4 ? 2 : 1;
    out_n = (size_t)h * (((size_t)w * ch * bd + 7) / 8 + 1);
    for (size_t p = 8; p + 12 <= file_n;) {
        uint32_t n = be32(file + p);
        if (!memcmp(file + p + 4, "IDAT", 4)) { piece_p[pieces] = file + p + 8; piece_n[pieces++] = n; memcpy(stream + stream_n, file + p + 8, n); stream_n += n; }
        p += 12 + (size_t)n;
    }
    int runs = argc > 3 ? atoi(argv[3]) : 21;
    const char *arm = argv[2];
    printf("# %s: %zu bytes of zlib stream in %u IDAT chunks, %zu out\n", argv[1], stream_n, pieces, out_n);
    for (int r = 0; r < runs; r++) {
        made = 0;
        double t0 = now();
        int bad = !strcmp(arm, "tinfl") ? tinfl_arm()
#ifdef HAVE_ZLIB
            : !strcmp(arm, "zlib") ? zlib_arm()
#endif
#ifdef HAVE_ZNG
            : !strcmp(arm, "zng") ? zng_arm()
#endif
#ifdef HAVE_LIBDEFLATE
            : !strcmp(arm, "libdeflate") ? libdeflate_arm()
#endif
            : !strcmp(arm, "cdom") ? cdom_arm(0) : !strcmp(arm, "cdom-pieces") ? cdom_arm(1) : 2;
        double ms = now() - t0;
        if (bad || made != out_n) { printf("%s FAILED (%d, %zu bytes made)\n", arm, bad, made); return 1; }
        printf("%s\t%d\t%.2f\n", arm, r, ms);
    }
    return 0;
}
