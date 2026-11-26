/* strncmp (cdom's string.h, src/libc; mini.c's), as a library calls it. run.sh builds it under cdom's flags and
 * freestanding ones, with mini.c and no other libc; run.mjs calls libc_strncmp, which returns 0, or the line of the
 * first case that is wrong. The calls go through a volatile pointer, so every one reaches mini.c's code. */
#include <string.h>
static int (*volatile cmp)(const char *, const char *, size_t) = strncmp;
__attribute__((export_name("libc_strncmp"))) int libc_strncmp(void) {
    if (cmp("abcdef", "abcxyz", 3) != 0) return __LINE__; /* equal prefixes, different tails */
    if (cmp("abcdef", "abcxyz", 4) >= 0) return __LINE__;
    if (cmp("abcxyz", "abcdef", 4) <= 0) return __LINE__;
    if (cmp("a", "b", 0) != 0 || cmp("", "x", 0) != 0) return __LINE__; /* n == 0 */
    if (cmp("abc", "abc", 100) != 0) return __LINE__; /* n past both ends */
    if (cmp("abc", "abcd", 100) >= 0 || cmp("abcd", "abc", 100) <= 0) return __LINE__;
    if (cmp("abc", "abc\0zzz", 7) != 0) return __LINE__; /* a NUL inside n ends it */
    if (cmp("ab\0x", "ab\0y", 4) != 0) return __LINE__;
    if (cmp("ab", "ab\0", 3) != 0) return __LINE__;
    if (cmp("\xff", "a", 1) <= 0 || cmp("a", "\xff", 1) >= 0) return __LINE__; /* bytes >= 0x80 are unsigned */
    if (cmp("\x80", "\x7f", 1) <= 0) return __LINE__;
    if (cmp("a\x80", "a\x01", 2) <= 0) return __LINE__;
    if (cmp("abc", "abd", 3) >= 0 || cmp("abd", "abc", 3) <= 0) return __LINE__; /* the sign */
    if (cmp("abc", "abd", 2) != 0) return __LINE__; /* n stops before the difference */
    if (cmp("", "", 5) != 0 || cmp("", "a", 5) >= 0 || cmp("a", "", 5) <= 0) return __LINE__;
    return 0;
}
