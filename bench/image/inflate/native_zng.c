/* native.c's zlib-ng arm, apart since zlib-ng's header and zlib's cannot be in one file. */
#include <stdint.h>
#include <string.h>
#include "zlib-ng.h"

int zng_inflate_all(const uint8_t *stream, size_t n, size_t *made) {
    static uint8_t o[65536]; zng_stream z; memset(&z, 0, sizeof z);
    if (zng_inflateInit(&z) != Z_OK) return 1;
    z.next_in = stream; z.avail_in = (uint32_t)n;
    int r;
    do { z.next_out = o; z.avail_out = sizeof o; r = zng_inflate(&z, Z_NO_FLUSH); *made += sizeof o - z.avail_out; } while (r == Z_OK);
    zng_inflateEnd(&z);
    return r != Z_STREAM_END;
}
