/* The differential test of js1c (docs/js1-compile-design.md): tools/js1.mjs runs each export with the same arguments
 * as real wasm in node and as js1c's JavaScript 1.0, and the results, every out() call and the memory must be the
 * same. Integer ops, i64 math (lowered by binaryen to i32 pairs), doubles and their bits, loops, a 1000-deep recursion,
 * bytes, halves and unaligned words, memset and memmove, a switch (br_table), calls through a table, and traps.
 * No cdom: no string table, one import (env.out). loop and rec are also the measurements' CPU-bound tests. */
typedef unsigned u32;
typedef unsigned long long u64;
typedef long long i64;
typedef unsigned short u16;
#define EXPORT(n) __attribute__((export_name(n)))
#define NOINLINE __attribute__((noinline))
__attribute__((import_module("env"), import_name("out"))) void out(int v);

static void out64(u64 v) {
    out((int)(u32)v);
    out((int)(u32)(v >> 32));
}

EXPORT("ints") int ints(int a, int b) {
    u32 ua = (u32)a, ub = (u32)b;
    out(a + b); out(a - b); out(a * b); out(a * 7); out(a * 3000001);
    out(a & b); out(a | b); out(a ^ b); out(~a);
    out(a << (b & 31)); out(a >> (b & 31)); out((int)(ua >> (b & 31)));
    out(a << 5); out(a >> 7); out((int)(ua >> 9));
    out((int)((ua << (b & 31)) | (ua >> ((32 - b) & 31)))); /* rotl */
    out((int)((ua >> (b & 31)) | (ua << ((32 - b) & 31)))); /* rotr */
    out(a < b); out(a <= b); out(a > b); out(a >= b); out(a == b); out(a != b);
    out(ua < ub); out(ua <= ub); out(ua > ub); out(ua >= ub); out(!a);
    out(__builtin_clz(ua | 1)); out(__builtin_ctz(ua | 0x80000000u)); out(__builtin_popcount(ua));
    out((signed char)a); out((short)a); out((unsigned char)a); out((u16)a);
    if (b != 0 && !(a == (int)0x80000000 && b == -1)) { out(a / b); out(a % b); }
    if (b != 0) { out((int)(ua / ub)); out((int)(ua % ub)); }
    out(a / 7); out(a % 7); out((int)(ua / 10)); out((int)(ua % 10)); out(a / -3); out(a % -3);
    return a > b ? a - b : b - a;
}

EXPORT("i64s") int i64s(int a, int b, int c, int d) {
    i64 x = (i64)((u64)(u32)a << 32 | (u32)b), y = (i64)((u64)(u32)c << 32 | (u32)d);
    u64 ux = (u64)x, uy = (u64)y;
    out64((u64)(x + y)); out64((u64)(x - y)); out64((u64)(x * y)); out64(ux & uy); out64(ux | uy); out64(ux ^ uy);
    out64((u64)(x << (d & 63))); out64((u64)(x >> (d & 63))); out64(ux >> (d & 63));
    out(x < y); out(ux < uy); out(x == y); out(x >= y); out(ux > uy);
    if (y != 0 && !(x == (i64)0x8000000000000000ull && y == -1)) { out64((u64)(x / y)); out64((u64)(x % y)); }
    if (uy != 0) { out64(ux / uy); out64(ux % uy); }
    out(__builtin_clzll(ux | 1)); out(__builtin_ctzll(ux | (1ull << 63))); out(__builtin_popcountll(ux));
    out64((u64)(i64)a); out64((u64)(u32)a);
    return (int)(x >> 17) ^ (int)y;
}

EXPORT("dbl") int dbl(int a, int b) {
    double x = a / 3.0, y = b * 1.5 + 0.25, z;
    volatile double m[2];
    u32 w[2];
    out((int)(x * 1000)); out((int)(y * 1000)); out((int)((x + y) * 1000)); out((int)((x - y) * 1000));
    out((int)(x * y)); out(b ? (int)(x / y) : 0); out(x < y); out(x == y); out(x != y);
    out((int)__builtin_sqrt(y < 0 ? -y : y)); out((int)__builtin_floor(x)); out((int)__builtin_ceil(x));
    out((int)__builtin_trunc(y)); out((int)__builtin_nearbyint(x)); out((int)__builtin_nearbyint(2.5)); out((int)__builtin_nearbyint(-3.5));
    out((int)(__builtin_fmin(x, y) * 100)); out((int)(__builtin_fmax(x, y) * 100)); out((int)__builtin_copysign(7.0, y));
    out((int)(u32)(double)(u32)a); out((int)(double)a);
    z = x * 1e300 * 1e10; /* infinity */
    m[0] = z; m[1] = -0.0;
    __builtin_memcpy(w, (const void *)&m[0], 8); out((int)w[0]); out((int)w[1]);
    __builtin_memcpy(w, (const void *)&m[1], 8); out((int)w[0]); out((int)w[1]);
    m[0] = x; __builtin_memcpy(w, (const void *)&m[0], 8); out((int)w[0]); out((int)w[1]);
    m[0] = 5e-320; __builtin_memcpy(w, (const void *)&m[0], 8); out((int)w[0]); out((int)w[1]);
    out((int)(m[0] * 1e300 * 1e20));
    m[0] = x * 1e-315; __builtin_memcpy(w, (const void *)&m[0], 8); out((int)w[0]); out((int)w[1]); /* a subnormal at run time */
    out((int)(m[0] * 1e300 * 1e25));
    u64 bits = __builtin_bit_cast(u64, y); out64(bits);
    out((int)(__builtin_bit_cast(double, bits ^ (1ull << 63)) * 4));
    out(__builtin_isnan(z - z)); out(z > 1e308);
    return (int)(x + y);
}

EXPORT("loop") int loop(int n) {
    u32 x = 1, i;
    for (i = 0; i < (u32)n; i++) {
        x = (x + i * 2654435761u) ^ (x >> 7);
        x = x * 33 + (i & 255);
    }
    return (int)x;
}

static volatile int depth, deepest;
NOINLINE static int rec_in(int n) {
    int r;
    depth++;
    if (depth > deepest) deepest = depth;
    if (n == 0) r = 1;
    else r = rec_in(n - 1) * 3 + n;
    depth--;
    return r;
}
EXPORT("rec") int rec(int n) {
    deepest = 0;
    int r = rec_in(n);
    out(deepest);
    return r;
}

static unsigned char bytes[64];
struct __attribute__((packed)) P { char c; int i; short s; double d; };
static struct P packed[3];
EXPORT("mem") int mem(int a) {
    int i, sum = 0;
    for (i = 0; i < 64; i++) bytes[i] = (unsigned char)(a * i + i);
    for (i = 0; i < 64; i++) sum = sum * 31 + (signed char)bytes[i];
    out(sum);
    u16 *h = (u16 *)(void *)bytes;
    for (i = 0; i < 32; i++) h[i] = (u16)(h[i] * 3 + i);
    for (i = 0; i < 32; i++) sum = sum * 7 + (short)h[i];
    out(sum);
    packed[1].c = (char)a; packed[1].i = a * 77; packed[1].s = (short)(a - 9); packed[1].d = a / 7.0;
    packed[2].i = packed[1].i + 1;
    out(packed[1].c); out(packed[1].i); out(packed[1].s); out((int)(packed[1].d * 1e6)); out(packed[2].i);
    /* Sizes from the argument, so clang leaves memory.fill and memory.copy (a constant size it expands into loads). */
    int k = (a & 3) + 1;
    __builtin_memset(bytes + 3, a, (unsigned)(20 + k));
    __builtin_memmove(bytes + 10, bytes, (unsigned)(29 + k));
    __builtin_memmove(bytes, bytes + 5, (unsigned)(29 + k));
    __builtin_memcpy(bytes + 40, bytes + 8, (unsigned)(12 + 4 * k));
    __builtin_memmove(bytes + 8, bytes + 4, (unsigned)(16 * k));
    for (i = 0; i < 64; i++) sum = sum * 3 + bytes[i];
    out(sum);
    return sum;
}
EXPORT("buf") unsigned char *buf(void) { return bytes; }

NOINLINE static int f0(int x) { return x + 1; }
NOINLINE static int f1(int x) { return x * 2; }
NOINLINE static int f2(int x) { return x - 3; }
NOINLINE static int f3(int x) { return rec_in(x & 7); }
static int (*const table[4])(int) = { f0, f1, f2, f3 };
EXPORT("ind") int ind(int n) {
    int x = 5, i;
    for (i = 0; i < n; i++) x = table[(x + i) & 3](x) & 0xffff;
    return x;
}
static int (*volatile wrong)(int);
EXPORT("ind_bad") int ind_bad(int x) {
    wrong = (int (*)(int))(void *)ind_bad;
    return ((int (*)(int, int))(void *)table[x & 3])(x, x);
}

EXPORT("sw") int sw(int n) {
    int r = 0, i;
    for (i = 0; i < n; i++) {
        switch ((i * 7 + n) % 11) {
        case 0: r += 3; break;
        case 1: r ^= 0x55; break;
        case 2: r = r * 5 + 1; break;
        case 3: case 4: r -= i; break;
        case 5: r = r << 1; break;
        case 7: r = r >> 1; break;
        case 8: r += i * i; break;
        case 10: r = ~r; break;
        default: r += 1;
        }
    }
    return r;
}

EXPORT("trap") int trap(int x) {
    bytes[0] = 99;
    if (x) __builtin_trap();
    return 1;
}
NOINLINE static int divide(int a, int b) { return a / b; }
EXPORT("divz") int divz(int a, int b) { return divide(a, b) + 1; }
EXPORT("deeptrap") int deeptrap(int n) {
    if (n == 0) __builtin_trap();
    return deeptrap(n - 1) + 1;
}
/* memory.grow: js1c writes the new pages' words (Netscape 2 dies reading a word nothing wrote). */
EXPORT("grow") int grow(int n) { return (int)__builtin_wasm_memory_grow(0, (unsigned)n); }
/* A word, a byte and a half of the heap that nothing wrote (wasm reads 0): js1c writes only the words below __heap_base on
 * the engines that read a missing word as 0, so these read one that does not exist (tools/js1.mjs). k counts from __heap_base. */
/* Data words at and above 2^31: Netscape 3.04's parseInt keeps a value only up to 2147483647, so js1c writes a data word as a signed one. */
static const unsigned bigwords[] = {0xdeadbeefu, 0x80000000u, 0xffffffffu, 0x7fffffffu, 0xcafe1234u, 3396446046u};
EXPORT("bigword") int bigword(int k) { return (int)bigwords[k]; }
EXPORT("bigbyte") int bigbyte(int k) { return ((const unsigned char *)bigwords)[k]; }
extern unsigned char __heap_base;
EXPORT("peek") int peek(int k) { return ((volatile int *)(&__heap_base + 64))[k]; }
EXPORT("peekb") int peekb(int k) { return ((volatile unsigned char *)(&__heap_base + 64))[k]; }
EXPORT("peekh") int peekh(int k) { return ((volatile short *)(&__heap_base + 64))[k]; }
