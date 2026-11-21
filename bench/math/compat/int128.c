/* The three 128-bit integer helpers CORE-MATH's wasm build calls (__int128 has no wasm instruction, and cdom links
 * no compiler-rt): multiply and the two shifts, from 64-bit halves only, so none can call itself. Natively the
 * compiler inlines these, so the native twins never link this file. */
#include <stdint.h>

typedef union { unsigned __int128 all; struct { uint64_t lo, hi; } s; } bm_u128;

__int128 __multi3(__int128 a, __int128 b) {
  bm_u128 x = { (unsigned __int128)a }, y = { (unsigned __int128)b }, r;
  uint64_t a0 = x.s.lo & 0xffffffffu, a1 = x.s.lo >> 32, b0 = y.s.lo & 0xffffffffu, b1 = y.s.lo >> 32;
  uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
  r.s.lo = (mid << 32) | (p00 & 0xffffffffu);
  r.s.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32) + x.s.hi * y.s.lo + x.s.lo * y.s.hi;
  return (__int128)r.all;
}

__int128 __ashlti3(__int128 a, int n) {
  bm_u128 x = { (unsigned __int128)a }, r;
  if (n == 0) return a;
  if (n >= 64) { r.s.hi = x.s.lo << (n - 64); r.s.lo = 0; }
  else { r.s.hi = (x.s.hi << n) | (x.s.lo >> (64 - n)); r.s.lo = x.s.lo << n; }
  return (__int128)r.all;
}

__int128 __lshrti3(__int128 a, int n) {
  bm_u128 x = { (unsigned __int128)a }, r;
  if (n == 0) return a;
  if (n >= 64) { r.s.lo = x.s.hi >> (n - 64); r.s.hi = 0; }
  else { r.s.lo = (x.s.lo >> n) | (x.s.hi << (64 - n)); r.s.hi = x.s.hi >> n; }
  return (__int128)r.all;
}
