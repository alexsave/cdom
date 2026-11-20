/* The CPU-bound program of docs/js1-interp-design.md (P4, P5): integer ops in a loop, and a recursion that clang keeps
 * a recursion, for the interpreter (cdom.js1.js) against the IE 9 build. No cdom: tools/js1interp.mjs runs it in node
 * and tests/js1.html in a browser. */
typedef unsigned u32;
#define EXPORT(n) __attribute__((export_name(n)))

/* n iterations of a multiply, adds, xors, shifts, a rotate, a remainder, a compare and a load and a store into a small
 * table: a checksum of all of them. */
static u32 tab[64];
static const u32 digits[8] = {3, 1, 4, 1, 5, 9, 2, 6}; /* a data segment: the words build takes only a module with one */
EXPORT("loop") u32 loop(u32 n) {
  u32 x = 1, y = 0x9e3779b9u, s = 0;
  for (u32 i = 0; i < 64; i++) tab[i] = digits[i & 7u] + i;
  for (u32 i = 0; i < n; i++) {
    x = x * 1664525u + 1013904223u;
    y ^= x >> 7;
    y = (y << 3) | (y >> 29);
    s += (x & 0xffu) + (y % 17u) - (i & 3u);
    if ((int)s < 0) s ^= 0x5555u;
    tab[x >> 26] += s;
  }
  return s ^ x ^ y ^ tab[y & 63u];
}

/* A recursion n deep: the result of each call is used after it returns, in a way no accumulator can carry, so the
 * calls nest. */
static __attribute__((noinline)) u32 rec_(u32 n) {
  if (n == 0) return 7;
  u32 r = rec_(n - 1);
  return (r * 31u + n) ^ (r >> 3);
}
EXPORT("rec") u32 rec(u32 n) { return rec_(n); }
