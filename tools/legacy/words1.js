/* C's memory for the frames tier's app file (app.wasm.js1.js, docs/js1-transform-design.md): tools/legacy/words.js's
 * layout and names, written in the JavaScript 1.0 that Netscape 2.02 runs (docs/descent.md), so the JS 1 transform
 * (tools/legacy/js1.mjs) can put it in front of the app as it is. tools/js1.mjs checks it against real typed arrays.
 *
 * W[i] holds the bytes 4i to 4i + 3, little-endian, as a signed 32-bit integer, exactly as words.js does: the frames
 * shim reads memory.words the same way for both. Units are those of the typed array each access replaces.
 *
 * What JavaScript 1.0 lacks, and what this file does instead: no array literal and no push (W is a new Array() filled
 * by index; on Netscape 2 that is an object with number keys, which docs/descent.md measured faster there than one of
 * new Object()), no function expression (the tables are filled by top-level code), no ===, no NaN or Infinity
 * globals (INF and NAN below are made by arithmetic), no charCodeAt (base64 by a table keyed by the character), and
 * no Math.log (the exponent by a binary search of the table of powers of two). Math.floor is the only Math used.
 *
 * The transform renames every name here with the rest of the file's, so nothing here may collide with the shim's. */
var W = new Array();   /* the memory */
var WBYTES = 0;        /* its size in bytes */
var WS = new Array();  /* the reinterpret scratch: words 0 and 1 the f64's low and high, word 2 the f32 */
WS[0] = 0;
WS[1] = 0;
WS[2] = 0;
WS[3] = 0;
/* A trap: set by a trap in C, cleared by the shim (cdom_trapped); every call that can trap is followed by a check. */
var T = 0;
function trapped() {
  var t = T;
  T = 0;
  return t;
}
/* 2^n for n from -1074 to 1023 at P2[n + 1074]: 1, doubled up and halved down, each step exact. */
var P2 = new Array();
var P2k;
var P2v = 1;
for (P2k = 1074; P2k < 2098; P2k++) {
  P2[P2k] = P2v;
  P2v = P2v * 2;
}
P2v = 1;
for (P2k = 1074; P2k >= 0; P2k--) {
  P2[P2k] = P2v;
  P2v = P2v / 2;
}
var INF = P2[2097] * 2;
var NAN = INF - INF;
function pow2(n) {
  return P2[n + 1074];
}
var F32_HALF_PAST_MAX = pow2(128) - pow2(103);

function words_init(bytes) {
  var n = bytes >>> 2, i;
  W = new Array();
  for (i = 0; i < n; i++) W[i] = 0;
  WBYTES = bytes;
}
function words_grow(pages) {
  var i = WBYTES >>> 2, n = i + (pages << 14);
  for (; i < n; i++) W[i] = 0;
  WBYTES = WBYTES + pages * 65536;
}

function load8_u(a) {
  return W[a >> 2] >>> ((a & 3) << 3) & 255;
}
function load8_s(a) {
  return W[a >> 2] << (24 - ((a & 3) << 3)) >> 24;
}
function store8(a, v) {
  var i = a >> 2, s = (a & 3) << 3;
  W[i] = W[i] & ~(255 << s) | (v & 255) << s;
}
function load16_u(h) {
  return W[h >> 1] >>> ((h & 1) << 4) & 65535;
}
function load16_s(h) {
  return W[h >> 1] << (16 - ((h & 1) << 4)) >> 16;
}
function store16(h, v) {
  var i = h >> 1, s = (h & 1) << 4;
  W[i] = W[i] & ~(65535 << s) | (v & 65535) << s;
}

/* The exponent e of a finite, positive x, from -1074 to 1023: the largest with 2^e <= x. */
function exponent(x) {
  var lo = -1074, hi = 1023, mid;
  while (lo < hi) {
    mid = (lo + hi + 1) >> 1;
    if (P2[mid + 1074] <= x) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

/* Math.fround: to the nearest float, ties to even, through the subnormals, to infinity from halfway past the
 * largest float; NaN, the infinities and both zeros as they are. */
function fround(x) {
  var a, e, q, r, f, d;
  x = x - 0;
  if (x != x || x == 0 || x == INF || x == -INF) return x;
  a = x;
  if (x < 0) a = -x;
  if (a >= F32_HALF_PAST_MAX) {
    if (x < 0) return -INF;
    return INF;
  }
  e = exponent(a);
  q = e - 23;
  if (e < -126) q = -149;
  r = a * pow2(-q);
  f = Math.floor(r);
  d = r - f;
  if (d > 0.5 || (d == 0.5 && f % 2 == 1)) f = f + 1;
  r = f * pow2(q);
  if (x < 0) return -r;
  return r;
}
function f32_bits(x) {
  var s = 0, a, e, m;
  x = fround(x);
  if (x != x) return 2143289344;
  a = x;
  if (x < 0 || (x == 0 && 1 / x < 0)) s = -2147483648;
  if (x < 0) a = -x;
  if (a == INF) return s | 2139095040;
  if (a == 0) return s;
  e = exponent(a);
  if (e < -126) return s | a * pow2(149);
  m = a * pow2(23 - e) - 8388608;
  return s | (e + 127) << 23 | m;
}
function f32_from_bits(b) {
  var e = b >>> 23 & 255, m = b & 8388607, v;
  if (e == 255) {
    v = INF;
    if (m) v = NAN;
  } else if (e == 0) v = m * pow2(-149);
  else v = (m + 8388608) * pow2(e - 150);
  if (b < 0) return -v;
  return v;
}
function f64_to(x, t, i) {
  var hi, lo = 0, a, e, m;
  x = x - 0;
  a = x;
  if (x < 0) a = -x;
  if (x != x) hi = 2146959360;
  else if (a == INF) hi = 2146435072;
  else if (a == 0) hi = 0;
  else {
    e = exponent(a);
    if (e < -1022) m = a * pow2(1022) * pow2(52);
    else m = (a * pow2(-e) - 1) * 4503599627370496;
    lo = m % 4294967296;
    hi = (m - lo) / 4294967296;
    if (e >= -1022) hi = hi | (e + 1023) << 20;
  }
  if (x < 0 || (x == 0 && 1 / x < 0)) hi = hi | -2147483648;
  t[i] = lo | 0;
  t[i + 1] = hi | 0;
}
function f64_from(lo, hi) {
  var e = hi >>> 20 & 2047, m = (hi & 1048575) * 4294967296 + (lo >>> 0), v;
  if (e == 2047) {
    v = INF;
    if (m) v = NAN;
  } else if (e == 0) v = m * pow2(-1074);
  else v = (m + 4503599627370496) * pow2(e - 1075);
  if (hi < 0) return -v;
  return v;
}

function load_f32(w) {
  return f32_from_bits(W[w]);
}
function store_f32(w, v) {
  W[w] = f32_bits(v);
}
function load_f64(d) {
  return f64_from(W[d << 1], W[(d << 1) + 1]);
}
function store_f64(d, v) {
  f64_to(v, W, d << 1);
}

function scratch_load_i32(i) {
  return WS[i] | 0;
}
function scratch_store_i32(i, v) {
  WS[i] = v | 0;
}
function scratch_load_f64() {
  return f64_from(WS[0], WS[1]);
}
function scratch_store_f64(v) {
  f64_to(v, WS, 0);
}
function scratch_load_f32() {
  return f32_from_bits(WS[2]);
}
function scratch_store_f32(v) {
  WS[2] = f32_bits(v);
}

/* Math.imul, Math.clz32 and Math.trunc, which wasm2js calls and none of these engines has. imul multiplies in halves
 * so no product passes 2^53; the transform writes (a * c | 0) itself where c is a small constant. */
function imul(a, b) {
  var al = a & 65535, bl = b & 65535;
  return (al * bl + (((a >>> 16) * bl + al * (b >>> 16) & 65535) << 16)) | 0;
}
function clz32(x) {
  var n = 0;
  x = x >>> 0;
  if (x == 0) return 32;
  while (x < 2147483648) {
    x = x * 2;
    n++;
  }
  return n;
}
function trunc(x) {
  if (x < 0) return -Math.floor(-x);
  return Math.floor(x);
}

/* memory.copy as memmove, as words.js's words_copy. */
function words_copy(dest, src, n) {
  if (dest < 0 || src < 0) return;
  if (dest + n > WBYTES) n = WBYTES - dest;
  if (src + n > WBYTES) n = WBYTES - src;
  if (n <= 0 || dest == src) return;
  if (dest < src) {
    if ((dest & 3) == (src & 3)) {
      while (n && dest & 3) {
        store8(dest, load8_u(src));
        dest++;
        src++;
        n--;
      }
      for (; n >= 4; n -= 4) {
        W[dest >> 2] = W[src >> 2];
        dest += 4;
        src += 4;
      }
    }
    for (; n; n--) {
      store8(dest, load8_u(src));
      dest++;
      src++;
    }
  } else {
    dest += n;
    src += n;
    if ((dest & 3) == (src & 3)) {
      while (n && dest & 3) {
        dest--;
        src--;
        store8(dest, load8_u(src));
        n--;
      }
      for (; n >= 4; n -= 4) {
        dest -= 4;
        src -= 4;
        W[dest >> 2] = W[src >> 2];
      }
    }
    for (; n; n--) {
      dest--;
      src--;
      store8(dest, load8_u(src));
    }
  }
}
function words_fill(dest, v, n) {
  var b = v & 255, word = b | b << 8 | b << 16 | b << 24;
  while (n && dest & 3) {
    store8(dest, b);
    dest++;
    n--;
  }
  for (; n >= 4; n -= 4) {
    W[dest >> 2] = word;
    dest += 4;
  }
  for (; n; n--) {
    store8(dest, b);
    dest++;
  }
}
/* A data segment: base64 into the words from byte address at. B64 maps each character to its six bits; "=" is not
 * in it, and a missing key is 0 in a shift. */
var B64 = new Object();
var B64s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
for (P2k = 0; P2k < 64; P2k++) B64[B64s.charAt(P2k)] = P2k;
function words_base64(at, s) {
  var n = s.length, end = at + (n * 3 >> 2), i, x;
  if (s.charAt(n - 2) == "=") end--;
  if (s.charAt(n - 1) == "=") end--;
  for (i = 0; i < n; i += 4) {
    x = B64[s.charAt(i)] << 18 | B64[s.charAt(i + 1)] << 12 | B64[s.charAt(i + 2)] << 6 | B64[s.charAt(i + 3)];
    store8(at, x >> 16);
    at++;
    if (at < end) {
      store8(at, x >> 8);
      at++;
    }
    if (at < end) {
      store8(at, x);
      at++;
    }
  }
}
