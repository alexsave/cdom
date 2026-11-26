/* The common benchmark of the three Netscape code paths (docs/descent.md, "The three code paths in the real browsers"):
 * one program, built through js1-transform, js1-interp and js1-compile, timed in the real browsers by
 * tools/js1bench.mjs's pages. loop and rec are tests/js1cpu.c's from js1-transform without cdom (no imports, so every
 * path can build it); mem fills and sums a 64 KB buffer. */
typedef unsigned u32;
#define EXPORT(n) __attribute__((export_name(n)))

static u32 buf[256];
static volatile u32 sink[64];

/* n rounds of a multiply, shifts, xors, a load and a store: arithmetic and memory, no calls. */
EXPORT("loop") u32 loop(u32 n) {
    u32 x = 1;
    for (u32 i = 0; i < n; i++) {
        x = x * 2654435761u + i;
        x ^= x >> 15;
        buf[i & 255] += x;
        x += buf[(i * 7) & 255] << 3;
    }
    return x;
}

/* A recursion d deep that the compiler cannot make a loop: each level stores after its call. */
__attribute__((noinline)) static u32 rec_(u32 d) {
    u32 r = 1;
    if (d) {
        r = rec_(d - 1);
        sink[d & 63] = r;
        r = r * 3 + sink[(d * 5) & 63] + d;
    }
    return r;
}
EXPORT("rec") u32 rec(u32 d) { return rec_(d); }

/* 64 KB filled a word at a time from a seed, then summed with a rotate: memory traffic. */
static u32 big[16384];
/* A data segment: the transform's memory pass (wasm2js's words build) takes only a module with one. Nine words, 36
 * bytes, a multiple of 3: js1-transform's base64 decoder (words1.js) dies on Netscape 2 at a padding "=" (docs/descent.md,
 * "The three code paths in the real browsers"), so the data here needs none. */
static const u32 mix[9] = {3, 1, 4, 1, 5, 9, 2, 6, 5};
EXPORT("mem") u32 mem(u32 seed) {
    u32 s = 0;
    for (u32 i = 0; i < 16384; i++) big[i] = i * seed + (i >> 3) + mix[i % 9u];
    for (u32 i = 0; i < 16384; i++) s = ((s << 1) | (s >> 31)) ^ big[i];
    return s;
}
