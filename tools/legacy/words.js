/* C's memory as one plain Array of 32-bit words, for a browser with no typed arrays (IE 9): tools/legacy/build.mjs
 * puts this in front of the wasm2js output when it writes app.wasm.ie9.js, and rewrites every typed-array access
 * in that output to these (or, for 32 bits, to W itself). tools/words.mjs checks each against real typed arrays.
 *
 * W[i] holds the bytes 4i to 4i + 3, little-endian as in wasm, as a signed 32-bit integer; every store keeps it
 * one. Units are those of the typed array each replaces: a byte address for 8 bits, a halfword index for 16
 * (HEAP16[a >> 1]), a word index for 32 and f32, an f64 index for f64 (words 2d and 2d + 1). Floats are packed
 * and unpacked here in software, with no Float32Array, Float64Array or DataView.
 *
 * NaN: a float load of a NaN gives JavaScript's one NaN, and a float store of a NaN writes the canonical quiet NaN
 * (0x7fc00000, or 0x7ff8000000000000), so a payload does not survive a trip through a float value. Wasm requires
 * the payload kept only through loads, stores and copies of the bits themselves, which go through the integer
 * paths here and are exact; wasm2js itself keeps floats as JavaScript numbers, which cannot carry one.
 *
 * ES5, and no name here may collide with one in the wasm2js output (the build checks). */
var W = [];          /* the memory */
var WBYTES = 0;      /* its size in bytes: a whole number of 64 KiB pages */
var WS = [0, 0, 0, 0]; /* the reinterpret scratch: i32 0 and 1 are the f64's low and high words, i32 2 the f32 */
/* 2^n for n from -1074 to 1023 at P2[n + 1074], each exact: built by doubling from 2^-1074, the smallest double,
 * so a table lookup replaces Math.pow, which old engines do not promise to get exact. */
var P2 = (function () {
  var t = [], i, v = 4.9406564584124654e-324;
  for (i = 0; i < 2098; i++, v *= 2) t.push(v);
  return t;
})();
function pow2(n) { return P2[n + 1074]; }
/* 2^128 - 2^103, halfway from the largest float to 2^128: from there up, fround rounds (to even) to infinity. */
var F32_HALF_PAST_MAX = pow2(128) - pow2(103);

/* The memory, bytes long, all zero. */
function words_init(bytes) {
  var n = bytes >>> 2, i;
  W = [];
  for (i = 0; i < n; i++) W.push(0);
  WBYTES = bytes;
}
/* pages more 64 KiB pages of zeros, at the end. */
function words_grow(pages) {
  var n = pages << 14, i;
  for (i = 0; i < n; i++) W.push(0);
  WBYTES += pages << 16;
}

function load8_u(a) { return W[a >> 2] >>> ((a & 3) << 3) & 255; }
function load8_s(a) { return W[a >> 2] << (24 - ((a & 3) << 3)) >> 24; }
function store8(a, v) {
  var i = a >> 2, s = (a & 3) << 3;
  W[i] = W[i] & ~(255 << s) | (v & 255) << s;
}
function load16_u(h) { return W[h >> 1] >>> ((h & 1) << 4) & 65535; }
function load16_s(h) { return W[h >> 1] << (16 - ((h & 1) << 4)) >> 16; }
function store16(h, v) {
  var i = h >> 1, s = (h & 1) << 4;
  W[i] = W[i] & ~(65535 << s) | (v & 65535) << s;
}

/* The exponent e of a finite, positive x: 2^e <= x < 2^(e+1), from -1074 to 1023. */
function exponent(x) {
  var e = Math.floor(Math.log(x) / Math.LN2);
  if (e > 1023) e = 1023;
  if (e < -1074) e = -1074;
  while (e > -1074 && pow2(e) > x) e--;
  while (e < 1023 && pow2(e + 1) <= x) e++;
  return e;
}

/* Math.fround: x rounded to the nearest float, ties to even, through the subnormals, to infinity past the largest
 * float's halfway point; NaN, infinities and both zeros as they are. */
function fround(x) {
  x = +x;
  if (x !== x || x === 0 || x === Infinity || x === -Infinity) return x;
  var a = x < 0 ? -x : x, e, q, r, f, d;
  if (a >= F32_HALF_PAST_MAX) return x < 0 ? -Infinity : Infinity;
  e = exponent(a);
  q = e < -126 ? -149 : e - 23; /* the exponent of the float's last bit at this magnitude */
  r = a * pow2(-q);             /* exact: a power of two, and r < 2^24 */
  f = Math.floor(r);
  d = r - f;
  if (d > 0.5 || (d === 0.5 && f % 2 === 1)) f += 1;
  r = f * pow2(q);
  return x < 0 ? -r : r;
}
/* The bits of fround(x), as a signed 32-bit integer. */
function f32_bits(x) {
  x = fround(x);
  if (x !== x) return 0x7fc00000;
  var s = x < 0 || 1 / x < 0 ? 0x80000000 : 0, a = x < 0 ? -x : x, e, m;
  if (a === Infinity) return (s | 0x7f800000) | 0;
  if (a === 0) return s | 0;
  e = exponent(a);
  if (e < -126) return (s | a * pow2(149)) | 0;            /* subnormal: the mantissa is a / 2^-149 */
  m = a * pow2(23 - e) - 8388608;                           /* the 23 bits under the implicit one */
  return (s | (e + 127) << 23 | m) | 0;
}
function f32_from_bits(b) {
  var e = b >>> 23 & 255, m = b & 0x7fffff, v;
  if (e === 255) v = m ? NaN : Infinity;
  else if (e === 0) v = m * pow2(-149);
  else v = (m + 8388608) * pow2(e - 150);
  return b < 0 ? -v : v;
}
/* The bits of x as two signed words, low into t[i], high into t[i + 1]. */
function f64_to(x, t, i) {
  x = +x;
  var hi, lo = 0, a = x < 0 ? -x : x, e, m;
  if (x !== x) hi = 0x7ff80000;
  else if (a === Infinity) hi = 0x7ff00000;
  else if (a === 0) hi = 0;
  else {
    e = exponent(a);
    if (e < -1022) m = a * pow2(1022) * pow2(52);           /* subnormal: an integer under 2^52 (2^1074 is no double) */
    else m = (a * pow2(-e) - 1) * 4503599627370496;         /* the 52 bits under the implicit one */
    lo = m % 4294967296;
    hi = (m - lo) / 4294967296;
    if (e >= -1022) hi = hi | (e + 1023) << 20;
  }
  if (x < 0 || (x === 0 && 1 / x < 0)) hi = hi | 0x80000000;
  t[i] = lo | 0;
  t[i + 1] = hi | 0;
}
function f64_from(lo, hi) {
  var e = hi >>> 20 & 2047, m = (hi & 0xfffff) * 4294967296 + (lo >>> 0), v;
  if (e === 2047) v = m ? NaN : Infinity;
  else if (e === 0) v = m * pow2(-1074);
  else v = (m + 4503599627370496) * pow2(e - 1075);
  return hi < 0 ? -v : v;
}

function load_f32(w) { return f32_from_bits(W[w]); }
function store_f32(w, v) { W[w] = f32_bits(v); }
function load_f64(d) { return f64_from(W[d << 1], W[(d << 1) + 1]); }
function store_f64(d, v) { f64_to(v, W, d << 1); }

/* wasm2js's reinterpret scratch, over WS (see above). */
function scratch_load_i32(i) { return WS[i] | 0; }
function scratch_store_i32(i, v) { WS[i] = v | 0; }
function scratch_load_f64() { return f64_from(WS[0], WS[1]); }
function scratch_store_f64(v) { f64_to(v, WS, 0); }
function scratch_load_f32() { return f32_from_bits(WS[2]); }
function scratch_store_f32(v) { WS[2] = f32_bits(v); }

/* memory.copy as memmove: overlapping ranges copy as if through a temporary. A word at a time where both ends
 * share their place in a word, a byte at a time otherwise. Clamped to the memory, as copyWithin is. */
function words_copy(dest, src, n) {
  if (dest < 0 || src < 0) return;
  if (dest + n > WBYTES) n = WBYTES - dest;
  if (src + n > WBYTES) n = WBYTES - src;
  if (n <= 0 || dest === src) return;
  if (dest < src) {
    if ((dest & 3) === (src & 3)) {
      while (n && dest & 3) { store8(dest++, load8_u(src++)); n--; }
      for (; n >= 4; n -= 4, dest += 4, src += 4) W[dest >> 2] = W[src >> 2];
    }
    for (; n; n--) store8(dest++, load8_u(src++));
  } else {
    dest += n; src += n;
    if ((dest & 3) === (src & 3)) {
      while (n && dest & 3) { store8(--dest, load8_u(--src)); n--; }
      for (; n >= 4; n -= 4) { dest -= 4; src -= 4; W[dest >> 2] = W[src >> 2]; }
    }
    for (; n; n--) store8(--dest, load8_u(--src));
  }
}
/* memory.fill: n bytes of v from dest. */
function words_fill(dest, v, n) {
  var b = v & 255, word = (b | b << 8 | b << 16 | b << 24) | 0;
  while (n && dest & 3) { store8(dest++, b); n--; }
  for (; n >= 4; n -= 4, dest += 4) W[dest >> 2] = word;
  for (; n; n--) store8(dest++, b);
}
/* A data segment: base64 decoded straight into the words, from byte address at. */
var B64 = (function () {
  var t = [], c = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", i;
  for (i = 0; i < 123; i++) t.push(0);
  for (i = 0; i < 64; i++) t[c.charCodeAt(i)] = i;
  return t;
})();
function words_base64(at, s) {
  var n = s.length, end = at + (n * 3 >> 2) - (s.charAt(n - 2) === "=") - (s.charAt(n - 1) === "="), i, x;
  for (i = 0; i < n; i += 4) {
    x = B64[s.charCodeAt(i)] << 18 | B64[s.charCodeAt(i + 1)] << 12 | B64[s.charCodeAt(i + 2)] << 6 | B64[s.charCodeAt(i + 3)];
    store8(at++, x >> 16);
    if (at < end) store8(at++, x >> 8);
    if (at < end) store8(at++, x);
  }
}
