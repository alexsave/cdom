/* stdlib.h and stdio.h's FILE (cdom's, src/libc), as a library written for any C target uses them. run.sh
 * builds it under cdom's flags and freestanding ones and links it with cdom.c (the heap) and mini.c; run.mjs calls
 * libc_stdlib, which returns 0, or the line of the first check that is wrong. The values go through
 * volatiles, so the compiler keeps every call. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A prototype that names FILE, as libjpeg's jpeglib.h has (jpeg_stdio_src): it compiles, and no FILE is made. */
void lib_stdio_src(void *cinfo, FILE *infile);
static FILE *volatile no_file = NULL;

static volatile int vi = -7, vmin = INT_MIN + 1;
static volatile long vl = -70000L;
static int (*volatile absp)(int) = abs;
static long (*volatile labsp)(long) = labs;
static volatile size_t big = 1000, small = 3;

void cd_main(void) {}

#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
__attribute__((export_name("libc_stdlib"))) int libc_stdlib(void) {
    CHECK(abs(vi) == 7 && abs(-vi) == 7 && abs(0) == 0);
    CHECK(abs(vmin) == INT_MAX);
    CHECK(labs(vl) == 70000L && labs(-vl) == 70000L);
    CHECK(absp(vi) == 7 && labsp(vl) == 70000L);
    CHECK(no_file == NULL);
    unsigned char *a = malloc(big);
    CHECK(a != NULL);
    memset(a, 0xab, big);
    unsigned char *z = calloc(small, big);
    CHECK(z != NULL && z != a);
    for (size_t i = 0; i < small * big; i++) CHECK(z[i] == 0);
    a = realloc(a, big * 8);
    CHECK(a != NULL && a[0] == 0xab && a[big - 1] == 0xab);
    free(a);
    free(z);
    free(NULL);
    return 0;
}
