/* cdom's WebAssembly interpreter for the frames tier (docs/js1-interp-design.md): it runs the register code that
 * tools/legacy/js1lower.mjs makes from an app's .wasm at build time (the module file, app.wasm.js1.js), for Netscape 2
 * to 4 and IE 3 and 4, which cannot parse the IE 9 build.
 * JavaScript 1.0, as Netscape 2.02 was measured to have it (docs/descent.md; tools/js10.mjs refuses the rest): every
 * function declared at the top level, all state in globals named wi_, every table a plain object with number keys,
 * no typeof, no literal but strings and numbers, no ===, no switch, no try or throw, no Array, no charCodeAt.
 * C's call stack is the interpreter's own (wi_S), never the engine's: a wasm call is no JavaScript call, so the depth of
 * C's recursion is bounded by wi_LIMIT registers, not by Netscape 2's 100 to 300 nested calls.
 * The handlers are the sections after the loop, one an op, from a line //@ <op> <operand kinds> to the next; the build
 * (js1lower.mjs, interpreter) numbers the ops in js1ops.txt's order and writes them into the loop at its dispatch. */

/* The register files: a frame of a function is wi_R[fp] to wi_R[fp + its size - 1], its locals first, then the slots of
 * its value stack. An i64 keeps its low word in wi_R and its high word in wi_H, at the same index. */
var wi_R = new Object();
var wi_H = new Object();
/* Return positions and frame bases, two entries a call; a position of -1 ends a run (wi_call). */
var wi_S = new Object();
var wi_sp = 0;
/* The first register above every live frame, where a call back into C from an import starts its frames. */
var wi_top = 0;
/* How many runs are live (an import that calls back into C nests one). */
var wi_depth = 0;
/* The most pages the memory may grow to: 64 MiB, 16 million words, far past what these browsers can hold (a store into
 * an object costs Netscape 2 some 35 us); a grow past it fails, as when a memory is full. */
var wi_PAGECAP = 1024;
/* The registers C's frames may take: past it, a call traps (the stack is exhausted). */
var wi_LIMIT = 1048576;
/* A trap: 0, or 1 + its index in wi_TRAPS. It ends the run it happens in and every run under it; the next call from
 * outside starts clean. */
var wi_trap = 0;
var wi_TRAPS = new Object();
wi_TRAPS[0] = "unreachable";
wi_TRAPS[1] = "integer divide by zero";
wi_TRAPS[2] = "integer overflow";
wi_TRAPS[3] = "invalid conversion to integer";
wi_TRAPS[4] = "indirect call to a null or out of range entry";
wi_TRAPS[5] = "indirect call signature mismatch";
wi_TRAPS[6] = "call stack exhausted";
/* The module, as wi_load leaves it. */
var wi_C = null;          /* the code */
var wi_M = new Object();  /* the memory: 32-bit words, signed, little-endian within each (tools/legacy/words.js's) */
var wi_pages = 0;
var wi_maxPages = -1;
var wi_G = new Object();  /* the globals; i64 ones' high words in wi_GH */
var wi_GH = new Object();
var wi_I = new Object();  /* the imports, by function index */
var wi_IN = new Object(); /* an import's parameter count */
var wi_IR = new Object(); /* its result kind: 0 none, 1 i32, 2 f64, 3 i64, 4 f32 */
var wi_NIMP = 0;
var wi_FE = new Object(); /* a defined function's entry, by its index among the defined */
var wi_FN = new Object(); /* its parameter count */
var wi_FK = new Object(); /* its parameter kinds, at 64 times its index plus the parameter's */
var wi_FR = new Object(); /* its result kind */
var wi_SIG = new Object(); /* every function's signature, by function index (imports first) */
var wi_T = new Object();  /* the table: function indexes, -1 for none */
var wi_TN = 0;
var wi_missing = "";      /* the imports the page did not give */
/* What the frames shim reads of the memory (tools/legacy/frames.js: words and load_f64, as the IE 9 build's). */
var wi_mem = new Object();
wi_mem.words = wi_M;
wi_mem.load_f64 = wi_memF64;
/* Scratch: the two words of a split f64 or i64. */
var wi_lo = 0;
var wi_hi = 0;

/* Powers of two, 2^n at wi_P2[n + 1074] for n from -1074 to 1023, each exact: doubled up from 1 and halved down from
 * it, since a literal of the smallest double is not something an old parser is promised to read. */
var wi_P2 = new Object();
var wi_k;
wi_P2[1074] = 1;
for (wi_k = 1075; wi_k < 2098; wi_k++) wi_P2[wi_k] = wi_P2[wi_k - 1] * 2;
for (wi_k = 1073; wi_k >= 0; wi_k--) wi_P2[wi_k] = wi_P2[wi_k + 1] / 2;
var wi_INF = wi_P2[2097] * 2;
var wi_NAN = wi_INF - wi_INF;
/* 2^128 - 2^103, halfway from the largest float to 2^128: from there up, fround rounds to infinity. */
var wi_F32_PAST = wi_P2[1202] - wi_P2[1177];
/* The digit string's alphabet (js1lower.mjs, digits), each character's value by the character. */
var wi_ALPHA = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
var wi_A = new Object();
for (wi_k = 0; wi_k < 64; wi_k++) wi_A[wi_ALPHA.charAt(wi_k)] = wi_k;
var wi_n = 0;

/* The run: from pc, in the frame at fp, until a return pops the position -1 that wi_call pushed, or a trap. */
function wi_loop(pc, fp, sp) {
  var C = wi_C, R = wi_R, H = wi_H, M = wi_M, S = wi_S, G = wi_G, op, a, b, d, n, x, y, lo, hi, s;
  while (pc >= 0) {
    op = C[pc];
    /*@dispatch*/
  }
}

/* Calls defined function f (its index among the defined) with up to eight arguments from outside: an export, the
 * start, or an import calling back into C. 0 after a trap. */
function wi_call(f, a, b, c, d, e, g, h, i) {
  var R = wi_R, fp, sp, n = wi_FN[f], k = f * 64, r;
  if (wi_depth == 0) {
    wi_trap = 0;
    wi_top = 0;
    wi_sp = 0;
  }
  if (wi_trap) return 0;
  fp = wi_top;
  sp = wi_sp;
  if (fp + n > wi_LIMIT) {
    wi_trap = 7;
    return 0;
  }
  if (n > 0) wi_arg(fp, a, wi_FK[k]);
  if (n > 1) wi_arg(fp + 1, b, wi_FK[k + 1]);
  if (n > 2) wi_arg(fp + 2, c, wi_FK[k + 2]);
  if (n > 3) wi_arg(fp + 3, d, wi_FK[k + 3]);
  if (n > 4) wi_arg(fp + 4, e, wi_FK[k + 4]);
  if (n > 5) wi_arg(fp + 5, g, wi_FK[k + 5]);
  if (n > 6) wi_arg(fp + 6, h, wi_FK[k + 6]);
  if (n > 7) wi_arg(fp + 7, i, wi_FK[k + 7]);
  wi_S[sp] = -1;
  wi_S[sp + 1] = 0;
  wi_depth++;
  wi_loop(wi_FE[f], fp, sp + 2);
  wi_depth--;
  if (wi_trap) return 0;
  r = R[fp];
  wi_hi = wi_H[fp];
  if (wi_FR[f] == 0) return;
  return r;
}
/* An argument from outside, as its kind wants it: 1 i32, 2 f64, 3 i64 (a whole number, so exact within 2^53), 4 f32.
 * An i64 result's high word is left in wi_hi. */
function wi_arg(at, v, kind) {
  wi_H[at] = 0;
  if (kind == 2) wi_R[at] = v - 0;
  else if (kind == 4) wi_R[at] = wi_fround(v - 0);
  else if (kind == 3) {
    wi_split64(wi_trunc(v - 0));
    wi_R[at] = wi_lo;
    wi_H[at] = wi_hi;
  } else wi_R[at] = v | 0;
}
/* The trap that ended the last call from outside, by name, or "". */
function wi_trapped() {
  if (wi_trap) return wi_TRAPS[wi_trap - 1];
  return "";
}
/* Import k, with its arguments at register a, its result stored there. */
function wi_imp(k, a) {
  var R = wi_R, n = wi_IN[k], x, kind;
  if (n == 0) x = wi_I[k]();
  else if (n == 1) x = wi_I[k](R[a]);
  else if (n == 2) x = wi_I[k](R[a], R[a + 1]);
  else if (n == 3) x = wi_I[k](R[a], R[a + 1], R[a + 2]);
  else if (n == 4) x = wi_I[k](R[a], R[a + 1], R[a + 2], R[a + 3]);
  else if (n == 5) x = wi_I[k](R[a], R[a + 1], R[a + 2], R[a + 3], R[a + 4]);
  else x = wi_I[k](R[a], R[a + 1], R[a + 2], R[a + 3], R[a + 4], R[a + 5], R[a + 6], R[a + 7], R[a + 8], R[a + 9]);
  kind = wi_IR[k];
  if (kind == 1) R[a] = x | 0;
  else if (kind == 2) R[a] = x - 0;
  else if (kind == 4) R[a] = wi_fround(x - 0);
  else if (kind == 3) {
    R[a] = x | 0;
    wi_H[a] = 0;
  }
}
/* What an import the page did not give answers. */
function wi_absent() {
  return 0;
}

/* The module: the numbers of the digit string s (js1lower.mjs, moduleNumbers), the import names ("module.name ", each
 * ended by a space), and the page's imports object. */
function wi_load(s, names, imports) {
  wi_loadD(wi_decode(s), names, imports);
}
/* The same, with the numbers already in D (the statements form) and wi_n set to their count. */
function wi_loadN(D, names, imports) {
  var n = 0;
  while (D[n] == 0 || D[n]) n++;
  wi_n = n;
  wi_loadD(D, names, imports);
}
/* The digit string's numbers, 5 bits a character, the low first; a character of 32 or more is followed by more of
 * the same number. Zigzagged: an even v is v / 2, an odd one -(v + 1) / 2. */
function wi_decode(s) {
  var D = new Object(), A = wi_A, n = s.length, i = 0, k = 0, v, m, d;
  while (i < n) {
    d = A[s.charAt(i)];
    i++;
    v = 0;
    m = 1;
    while (d >= 32) {
      v = v + (d - 32) * m;
      m = m * 32;
      d = A[s.charAt(i)];
      i++;
    }
    v = v + d * m;
    if (v % 2) D[k] = -(v + 1) / 2;
    else D[k] = v / 2;
    k++;
  }
  wi_n = k;
  return D;
}
function wi_loadD(D, names, imports) {
  var p = D[wi_n - 1], nimp, nf, f, i, k, n, z, at, j, full, dot, mod, nm, imp, t, kind, lo, hi, total, M = wi_M;
  if (D[p] != 1) return;
  nimp = D[p + 1];
  nf = D[p + 2];
  p += 3;
  wi_NIMP = nimp;
  for (f = 0; f < nimp; f++) {
    wi_IN[f] = D[p];
    wi_IR[f] = D[p + 1];
    p += 2;
  }
  for (f = 0; f < nimp + nf; f++) {
    wi_SIG[f] = D[p];
    p++;
  }
  for (f = 0; f < nf; f++) {
    wi_FE[f] = D[p];
    n = D[p + 1];
    wi_FN[f] = n;
    p += 2;
    for (k = 0; k < n; k++) {
      wi_FK[f * 64 + k] = D[p];
      p++;
    }
    wi_FR[f] = D[p];
    p++;
  }
  /* The table: every entry -1, then the runs of the element segments. */
  wi_TN = D[p];
  p++;
  for (i = 0; i < wi_TN; i++) wi_T[i] = -1;
  p = wi_runs(D, p, wi_T);
  /* The globals: kind, low word, high word. */
  n = D[p];
  p++;
  for (i = 0; i < n; i++) {
    kind = D[p];
    lo = D[p + 1];
    hi = D[p + 2];
    p += 3;
    if (kind == 4) wi_G[i] = wi_f32from(lo);
    else if (kind == 2) wi_G[i] = wi_f64from(lo, hi);
    else wi_G[i] = lo;
    wi_GH[i] = hi;
  }
  /* The memory: every word written, then the data segments' words. */
  wi_pages = D[p];
  wi_maxPages = D[p + 1];
  p += 2;
  total = wi_pages * 16384;
  for (i = 0; i < total; i++) M[i] = 0;
  p++;
  p = wi_runs(D, p, M);
  /* The float constants in the code, as their bits. */
  n = D[p];
  p++;
  for (i = 0; i < n; i++) {
    D[D[p]] = wi_f64from(D[p + 1], D[p + 2]);
    p += 3;
  }
  wi_C = D;
  /* The imports, by name. */
  at = 0;
  wi_missing = "";
  for (f = 0; f < nimp; f++) {
    j = names.indexOf(" ", at);
    full = names.substring(at, j);
    at = j + 1;
    dot = full.indexOf(".");
    mod = full.substring(0, dot);
    nm = full.substring(dot + 1, full.length);
    imp = null;
    if (imports[mod]) imp = imports[mod][nm];
    if (!imp) {
      imp = wi_absent;
      wi_missing = wi_missing + full + " ";
    }
    wi_I[f] = imp;
  }
  /* The start function. */
  t = D[p];
  if (t >= 0) wi_call(t - nimp);
}
/* Runs of values into T: a count of runs, then for each a count of entries to skip and a count of values that follow.
 * Returns the position after them. */
function wi_runs(D, p, T) {
  var runs = D[p], r, i = 0, n, k;
  p++;
  for (r = 0; r < runs; r++) {
    i += D[p];
    n = D[p + 1];
    p += 2;
    for (k = 0; k < n; k++) {
      T[i] = D[p];
      i++;
      p++;
    }
  }
  return p;
}

/* Memory: the slow paths. a is a byte address. */
function wi_ld8(a) {
  return wi_M[a >> 2] >>> ((a & 3) << 3) & 255;
}
function wi_st8(a, v) {
  var i = a >> 2, s = (a & 3) << 3;
  wi_M[i] = wi_M[i] & ~(255 << s) | (v & 255) << s;
}
/* A word at an address that is not a multiple of 4. */
function wi_ld32u(a) {
  var i = a >> 2, s = (a & 3) << 3;
  if (s == 0) return wi_M[i];
  return wi_M[i] >>> s | wi_M[i + 1] << (32 - s);
}
function wi_st32u(a, v) {
  if ((a & 3) == 0) {
    wi_M[a >> 2] = v | 0;
    return;
  }
  wi_st8(a, v);
  wi_st8(a + 1, v >> 8);
  wi_st8(a + 2, v >> 16);
  wi_st8(a + 3, v >> 24);
}
function wi_ld16(a) {
  if ((a & 3) == 3) return wi_ld8(a) | wi_ld8(a + 1) << 8;
  return wi_M[a >> 2] >>> ((a & 3) << 3) & 65535;
}
function wi_st16(a, v) {
  var i, s;
  if ((a & 3) == 3) {
    wi_st8(a, v);
    wi_st8(a + 1, v >> 8);
    return;
  }
  i = a >> 2;
  s = (a & 3) << 3;
  wi_M[i] = wi_M[i] & ~(65535 << s) | (v & 65535) << s;
}
/* memory.grow: the old size in pages, or -1. */
function wi_grow(n) {
  var old = wi_pages, i, end;
  n = n >>> 0;
  if (old + n > wi_PAGECAP || (wi_maxPages >= 0 && old + n > wi_maxPages)) return -1;
  end = (old + n) * 16384;
  for (i = old * 16384; i < end; i++) wi_M[i] = 0;
  wi_pages = old + n;
  return old;
}
/* memory.copy as memmove, and memory.fill: a byte at a time to a word boundary, then words where both ends share their
 * place in a word. */
function wi_copy(d, s, n) {
  var M = wi_M;
  d = d >>> 0;
  s = s >>> 0;
  n = n >>> 0;
  if (n == 0 || d == s) return;
  if (d < s) {
    if ((d & 3) == (s & 3)) {
      while (n > 0 && (d & 3)) {
        wi_st8(d, wi_ld8(s));
        d++;
        s++;
        n--;
      }
      while (n >= 4) {
        M[d >> 2] = M[s >> 2];
        d += 4;
        s += 4;
        n -= 4;
      }
    }
    while (n > 0) {
      wi_st8(d, wi_ld8(s));
      d++;
      s++;
      n--;
    }
    return;
  }
  d += n;
  s += n;
  if ((d & 3) == (s & 3)) {
    while (n > 0 && (d & 3)) {
      d--;
      s--;
      wi_st8(d, wi_ld8(s));
      n--;
    }
    while (n >= 4) {
      d -= 4;
      s -= 4;
      M[d >> 2] = M[s >> 2];
      n -= 4;
    }
  }
  while (n > 0) {
    d--;
    s--;
    wi_st8(d, wi_ld8(s));
    n--;
  }
}
function wi_fill(d, v, n) {
  var b = v & 255, w = b | b << 8 | b << 16 | b << 24;
  d = d >>> 0;
  n = n >>> 0;
  while (n > 0 && (d & 3)) {
    wi_st8(d, b);
    d++;
    n--;
  }
  while (n >= 4) {
    wi_M[d >> 2] = w;
    d += 4;
    n -= 4;
  }
  while (n > 0) {
    wi_st8(d, b);
    d++;
    n--;
  }
}

/* Bits. */
function wi_clz(x) {
  var n = 0;
  if (x == 0) return 32;
  while (x > 0) {
    x = x << 1;
    n++;
  }
  return n;
}
function wi_ctz(x) {
  var n = 0;
  if (x == 0) return 32;
  while ((x & 1) == 0) {
    x = x >> 1;
    n++;
  }
  return n;
}
function wi_popcnt(x) {
  var n = 0;
  while (x != 0) {
    x = x & (x - 1);
    n++;
  }
  return n;
}

/* i64 on pairs: each answer in wi_lo and wi_hi. */
function wi_mul64(alo, ahi, blo, bhi) {
  var a0 = alo & 65535, a1 = alo >>> 16, b0 = blo & 65535, b1 = blo >>> 16, mid, lo, carry, hi, x, y;
  mid = a0 * b1 + a1 * b0;
  lo = a0 * b0 + (mid % 65536) * 65536;
  carry = Math.floor(lo / 4294967296);
  lo = lo - carry * 4294967296;
  hi = a1 * b1 + Math.floor(mid / 65536) + carry;
  /* The cross terms' low words: alo * bhi + ahi * blo, as i32 multiplies. */
  x = alo & 65535;
  y = bhi & 65535;
  hi = hi + (x * y + ((alo >>> 16) * y + x * (bhi >>> 16) << 16) | 0);
  x = ahi & 65535;
  y = blo & 65535;
  hi = hi + (x * y + ((ahi >>> 16) * y + x * (blo >>> 16) << 16) | 0);
  wi_lo = lo | 0;
  wi_hi = hi | 0;
}
/* Unsigned division of the pairs, by shift and subtract when either has a high word: the quotient in wi_lo and
 * wi_hi, the remainder in wi_rlo and wi_rhi. b is not 0. */
var wi_rlo = 0;
var wi_rhi = 0;
function wi_divu64(alo, ahi, blo, bhi) {
  var qlo = 0, qhi = 0, rlo = 0, rhi = 0, i, bit, ge, x;
  if (ahi == 0 && bhi == 0) {
    wi_lo = (alo >>> 0) / (blo >>> 0) | 0;
    wi_hi = 0;
    wi_rlo = (alo >>> 0) % (blo >>> 0) | 0;
    wi_rhi = 0;
    return;
  }
  for (i = 63; i >= 0; i--) {
    /* r = r << 1 | bit i of a */
    rhi = rhi << 1 | rlo >>> 31;
    if (i >= 32) bit = ahi >>> (i - 32) & 1;
    else bit = alo >>> i & 1;
    rlo = rlo << 1 | bit;
    /* r >= b, unsigned */
    ge = 0;
    if ((rhi >>> 0) > (bhi >>> 0)) ge = 1;
    else if (rhi == bhi && (rlo >>> 0) >= (blo >>> 0)) ge = 1;
    if (ge) {
      x = (rlo >>> 0) - (blo >>> 0);
      rhi = rhi - bhi - (x < 0 ? 1 : 0) | 0;
      rlo = x | 0;
      if (i >= 32) qhi = qhi | 1 << (i - 32);
      else qlo = qlo | 1 << i;
    }
  }
  wi_lo = qlo;
  wi_hi = qhi;
  wi_rlo = rlo;
  wi_rhi = rhi;
}
/* Signed and unsigned division and remainder: what is 0 for no trap, or the trap's code (wi_trap's). rem: the
 * remainder is wanted. */
function wi_div64(alo, ahi, blo, bhi, signed, rem) {
  var neg = 0, rneg = 0, lo, hi;
  if (blo == 0 && bhi == 0) return 2;
  if (signed) {
    if (!rem && ahi == -2147483648 && alo == 0 && blo == -1 && bhi == -1) return 3;
    if (ahi < 0) {
      wi_neg64(alo, ahi);
      alo = wi_lo;
      ahi = wi_hi;
      neg = 1;
      rneg = 1;
    }
    if (bhi < 0) {
      wi_neg64(blo, bhi);
      blo = wi_lo;
      bhi = wi_hi;
      neg = 1 - neg;
    }
  }
  wi_divu64(alo, ahi, blo, bhi);
  if (rem) {
    lo = wi_rlo;
    hi = wi_rhi;
    neg = rneg;
  } else {
    lo = wi_lo;
    hi = wi_hi;
  }
  if (neg) wi_neg64(lo, hi);
  else {
    wi_lo = lo;
    wi_hi = hi;
  }
  return 0;
}
function wi_neg64(lo, hi) {
  wi_lo = -lo | 0;
  wi_hi = ~hi + (lo == 0 ? 1 : 0) | 0;
}
function wi_rot64(lo, hi, s, left) {
  var l, h;
  s = s & 63;
  if (!left) s = (64 - s) & 63;
  if (s >= 32) {
    l = lo;
    lo = hi;
    hi = l;
    s = s - 32;
  }
  if (s == 0) {
    wi_lo = lo;
    wi_hi = hi;
    return;
  }
  h = hi << s | lo >>> (32 - s);
  l = lo << s | hi >>> (32 - s);
  wi_lo = l;
  wi_hi = h;
}
/* A whole number t, in range, as a pair. */
function wi_split64(t) {
  var hi = Math.floor(t / 4294967296);
  wi_lo = t - hi * 4294967296 | 0;
  wi_hi = hi | 0;
}

/* Floats. */
function wi_trunc(x) {
  if (x < 0) return -Math.floor(-x);
  return Math.floor(x);
}
/* The exponent e of a finite x > 0: 2^e <= x < 2^(e + 1), by a binary search of wi_P2 (no Math.log). */
function wi_exponent(x) {
  var lo = 0, hi = 2097, mid;
  while (lo < hi) {
    mid = (lo + hi + 1) >> 1;
    if (wi_P2[mid] <= x) lo = mid;
    else hi = mid - 1;
  }
  return lo - 1074;
}
/* x rounded to the nearest float, ties to even (Math.fround). */
function wi_fround(x) {
  var a, e, q, r, f, d;
  if (x != x || x == 0 || x == wi_INF || x == -wi_INF) return x;
  a = x;
  if (x < 0) a = -x;
  if (a >= wi_F32_PAST) {
    if (x < 0) return -wi_INF;
    return wi_INF;
  }
  e = wi_exponent(a);
  q = e - 23;
  if (e < -126) q = -149;
  r = a * wi_P2[1074 - q];
  f = Math.floor(r);
  d = r - f;
  if (d > 0.5 || (d == 0.5 && f % 2 == 1)) f = f + 1;
  r = f * wi_P2[q + 1074];
  if (x < 0) return -r;
  return r;
}
function wi_f32bits(x) {
  var s = 0, a, e, m;
  x = wi_fround(x);
  if (x != x) return 2143289344;
  if (x < 0 || (x == 0 && 1 / x < 0)) s = -2147483648;
  a = x;
  if (x < 0) a = -x;
  if (a == wi_INF) return s | 2139095040;
  if (a == 0) return s;
  e = wi_exponent(a);
  if (e < -126) return s | a * wi_P2[1074 + 149];
  m = a * wi_P2[1074 + 23 - e] - 8388608;
  return s | (e + 127) << 23 | m;
}
function wi_f32from(b) {
  var e = b >>> 23 & 255, m = b & 8388607, v;
  if (e == 255) {
    if (m) return wi_NAN;
    v = wi_INF;
  } else if (e == 0) v = m * wi_P2[1074 - 149];
  else v = (m + 8388608) * wi_P2[1074 + e - 150];
  if (b < 0) return -v;
  return v;
}
/* The bits of x: low word in wi_lo, high in wi_hi. */
function wi_f64to(x) {
  var hi, lo = 0, a = x, e, m;
  if (x < 0) a = -x;
  if (x != x) hi = 2146959360;
  else if (a == wi_INF) hi = 2146435072;
  else if (a == 0) hi = 0;
  else {
    e = wi_exponent(a);
    if (e < -1022) m = a * wi_P2[1074 + 1022] * wi_P2[1074 + 52];
    else m = (a * wi_P2[1074 - e] - 1) * 4503599627370496;
    lo = m % 4294967296;
    hi = (m - lo) / 4294967296;
    if (e >= -1022) hi = hi | (e + 1023) << 20;
  }
  if (x < 0 || (x == 0 && 1 / x < 0)) hi = hi | -2147483648;
  wi_lo = lo | 0;
  wi_hi = hi | 0;
}
function wi_f64from(lo, hi) {
  var e = hi >>> 20 & 2047, m = (hi & 1048575) * 4294967296 + (lo >>> 0), v;
  if (e == 2047) {
    if (m) return wi_NAN;
    v = wi_INF;
  } else if (e == 0) v = m * wi_P2[0];
  else v = (m + 4503599627370496) * wi_P2[e - 1];
  if (hi < 0) return -v;
  return v;
}
function wi_memF64(d) {
  return wi_f64from(wi_M[d * 2], wi_M[d * 2 + 1]);
}
/* min and max as wasm has them: NaN if either is, and -0 below 0. */
function wi_fmin(x, y) {
  if (x != x || y != y) return wi_NAN;
  if (x == 0 && y == 0) {
    if (1 / x < 0) return x;
    return y;
  }
  if (x < y) return x;
  return y;
}
function wi_fmax(x, y) {
  if (x != x || y != y) return wi_NAN;
  if (x == 0 && y == 0) {
    if (1 / x > 0) return x;
    return y;
  }
  if (x > y) return x;
  return y;
}
function wi_copysign(x, y) {
  var neg = 0;
  if (y < 0 || (y == 0 && 1 / y < 0)) neg = 1;
  if (y != y) {
    wi_f64to(y);
    if (wi_hi < 0) neg = 1;
  }
  if (x != x) return x;
  if (x < 0 || (x == 0 && 1 / x < 0)) x = -x;
  if (neg) return -x;
  return x;
}
/* Round half to even. */
function wi_nearest(x) {
  var f, d;
  if (x != x || x == wi_INF || x == -wi_INF || x == 0) return x;
  f = Math.floor(x);
  d = x - f;
  if (d > 0.5 || (d == 0.5 && f % 2 != 0)) f = f + 1;
  if (f == 0 && x < 0) return -f;
  return f;
}
/* Float to i32 or i64, wasm's way: the trapping ones answer the trap's code in wi_tc (0 for none). */
var wi_tc = 0;
function wi_truncs32(x) {
  var t;
  wi_tc = 0;
  if (x != x) {
    wi_tc = 4;
    return 0;
  }
  t = wi_trunc(x);
  if (t < -2147483648 || t > 2147483647) {
    wi_tc = 3;
    return 0;
  }
  return t | 0;
}
function wi_truncu32(x) {
  var t;
  wi_tc = 0;
  if (x != x) {
    wi_tc = 4;
    return 0;
  }
  t = wi_trunc(x);
  if (t <= -1 || t > 4294967295) {
    wi_tc = 3;
    return 0;
  }
  return t | 0;
}
function wi_truncsats32(x) {
  if (x != x) return 0;
  if (x <= -2147483648) return -2147483648;
  if (x >= 2147483647) return 2147483647;
  return wi_trunc(x) | 0;
}
function wi_truncsatu32(x) {
  if (x != x || x <= 0) return 0;
  if (x >= 4294967295) return -1;
  return wi_trunc(x) | 0;
}
/* Into wi_lo and wi_hi. sat: saturate instead of trapping. */
function wi_truncs64(x, sat) {
  var t;
  wi_tc = 0;
  if (x != x) {
    if (!sat) wi_tc = 4;
    wi_lo = 0;
    wi_hi = 0;
    return;
  }
  t = wi_trunc(x);
  if (t < -wi_P2[1137] || t >= wi_P2[1137]) {
    if (!sat) wi_tc = 3;
    if (t < 0) {
      wi_lo = 0;
      wi_hi = -2147483648;
    } else {
      wi_lo = -1;
      wi_hi = 2147483647;
    }
    return;
  }
  wi_split64(t);
}
function wi_truncu64(x, sat) {
  var t;
  wi_tc = 0;
  if (x != x) {
    if (!sat) wi_tc = 4;
    wi_lo = 0;
    wi_hi = 0;
    return;
  }
  t = wi_trunc(x);
  if (t <= -1 || t >= wi_P2[1138]) {
    if (!sat) wi_tc = 3;
    if (t < 0) {
      wi_lo = 0;
      wi_hi = 0;
    } else {
      wi_lo = -1;
      wi_hi = -1;
    }
    return;
  }
  if (t < 0) t = 0;
  wi_split64(t);
}
/* The handlers. C is the code, pc this instruction's position, R and H the register files, fp the frame, sp the call
 * stack's top, M the memory, G the globals; a, b, d, n, x, y, lo, hi and s are scratch. Each reads every operand
 * before it writes its destination, which may be one of them. */
//@ mov rr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]];
pc += 3;
//@ mov64 rr
a = fp + C[pc + 2];
d = fp + C[pc + 1];
x = R[a];
H[d] = H[a];
R[d] = x;
pc += 3;
//@ const rk
R[fp + C[pc + 1]] = C[pc + 2];
pc += 3;
//@ constf rf
R[fp + C[pc + 1]] = C[pc + 2];
pc += 3;
//@ const64 rkk
d = fp + C[pc + 1];
R[d] = C[pc + 2];
H[d] = C[pc + 3];
pc += 4;
//@ gget rk
R[fp + C[pc + 1]] = G[C[pc + 2]];
pc += 3;
//@ gget64 rk
d = fp + C[pc + 1];
R[d] = G[C[pc + 2]];
H[d] = wi_GH[C[pc + 2]];
pc += 3;
//@ gset kr
G[C[pc + 1]] = R[fp + C[pc + 2]];
pc += 3;
//@ gset64 kr
a = fp + C[pc + 2];
G[C[pc + 1]] = R[a];
wi_GH[C[pc + 1]] = H[a];
pc += 3;
//@ select rrrr
if (R[fp + C[pc + 4]]) x = R[fp + C[pc + 2]];
else x = R[fp + C[pc + 3]];
R[fp + C[pc + 1]] = x;
pc += 5;
//@ select64 rrrr
if (R[fp + C[pc + 4]]) a = fp + C[pc + 2];
else a = fp + C[pc + 3];
x = R[a];
y = H[a];
d = fp + C[pc + 1];
R[d] = x;
H[d] = y;
pc += 5;
//@ jmp t
pc = C[pc + 1];
//@ jnz rt
if (R[fp + C[pc + 1]]) pc = C[pc + 2];
else pc += 3;
//@ jz rt
if (R[fp + C[pc + 1]]) pc += 3;
else pc = C[pc + 2];
//@ jeq rrt
if (R[fp + C[pc + 1]] == R[fp + C[pc + 2]]) pc = C[pc + 3];
else pc += 4;
//@ jne rrt
if (R[fp + C[pc + 1]] != R[fp + C[pc + 2]]) pc = C[pc + 3];
else pc += 4;
//@ jlts rrt
if (R[fp + C[pc + 1]] < R[fp + C[pc + 2]]) pc = C[pc + 3];
else pc += 4;
//@ jltu rrt
if ((R[fp + C[pc + 1]] >>> 0) < (R[fp + C[pc + 2]] >>> 0)) pc = C[pc + 3];
else pc += 4;
//@ jgts rrt
if (R[fp + C[pc + 1]] > R[fp + C[pc + 2]]) pc = C[pc + 3];
else pc += 4;
//@ jgtu rrt
if ((R[fp + C[pc + 1]] >>> 0) > (R[fp + C[pc + 2]] >>> 0)) pc = C[pc + 3];
else pc += 4;
//@ jles rrt
if (R[fp + C[pc + 1]] <= R[fp + C[pc + 2]]) pc = C[pc + 3];
else pc += 4;
//@ jleu rrt
if ((R[fp + C[pc + 1]] >>> 0) <= (R[fp + C[pc + 2]] >>> 0)) pc = C[pc + 3];
else pc += 4;
//@ jges rrt
if (R[fp + C[pc + 1]] >= R[fp + C[pc + 2]]) pc = C[pc + 3];
else pc += 4;
//@ jgeu rrt
if ((R[fp + C[pc + 1]] >>> 0) >= (R[fp + C[pc + 2]] >>> 0)) pc = C[pc + 3];
else pc += 4;
//@ jeqi rkt
if (R[fp + C[pc + 1]] == C[pc + 2]) pc = C[pc + 3];
else pc += 4;
//@ jnei rkt
if (R[fp + C[pc + 1]] != C[pc + 2]) pc = C[pc + 3];
else pc += 4;
//@ jltsi rkt
if (R[fp + C[pc + 1]] < C[pc + 2]) pc = C[pc + 3];
else pc += 4;
//@ jltui rkt
if ((R[fp + C[pc + 1]] >>> 0) < (C[pc + 2] >>> 0)) pc = C[pc + 3];
else pc += 4;
//@ jgtsi rkt
if (R[fp + C[pc + 1]] > C[pc + 2]) pc = C[pc + 3];
else pc += 4;
//@ jgtui rkt
if ((R[fp + C[pc + 1]] >>> 0) > (C[pc + 2] >>> 0)) pc = C[pc + 3];
else pc += 4;
//@ jlesi rkt
if (R[fp + C[pc + 1]] <= C[pc + 2]) pc = C[pc + 3];
else pc += 4;
//@ jleui rkt
if ((R[fp + C[pc + 1]] >>> 0) <= (C[pc + 2] >>> 0)) pc = C[pc + 3];
else pc += 4;
//@ jgesi rkt
if (R[fp + C[pc + 1]] >= C[pc + 2]) pc = C[pc + 3];
else pc += 4;
//@ jgeui rkt
if ((R[fp + C[pc + 1]] >>> 0) >= (C[pc + 2] >>> 0)) pc = C[pc + 3];
else pc += 4;
//@ brtab rn
a = R[fp + C[pc + 1]] >>> 0;
n = C[pc + 2];
if (a > n) a = n;
pc = C[pc + 3 + a];
//@ call tk
S[sp] = pc + 3;
S[sp + 1] = fp;
sp += 2;
fp = fp + C[pc + 2];
pc = C[pc + 1];
//@ enter kkk
if (fp + C[pc + 3] > wi_LIMIT) {
  wi_trap = 7;
  return;
}
a = fp + C[pc + 1];
b = a + C[pc + 2];
while (a < b) {
  R[a] = 0;
  H[a] = 0;
  a++;
}
pc += 4;
//@ ret r
R[fp] = R[fp + C[pc + 1]];
sp -= 2;
pc = S[sp];
fp = S[sp + 1];
if (pc < 0) {
  wi_sp = sp;
  return;
}
//@ ret64 r
a = fp + C[pc + 1];
x = R[a];
H[fp] = H[a];
R[fp] = x;
sp -= 2;
pc = S[sp];
fp = S[sp + 1];
if (pc < 0) {
  wi_sp = sp;
  return;
}
//@ ret0
sp -= 2;
pc = S[sp];
fp = S[sp + 1];
if (pc < 0) {
  wi_sp = sp;
  return;
}
//@ callimp kkk
wi_sp = sp;
wi_top = fp + C[pc + 3];
wi_imp(C[pc + 1], fp + C[pc + 2]);
if (wi_trap) return;
pc += 4;
//@ callind rkkk
a = R[fp + C[pc + 1]] >>> 0;
if (a >= wi_TN || wi_T[a] < 0) {
  wi_trap = 5;
  return;
}
a = wi_T[a];
if (wi_SIG[a] != C[pc + 2]) {
  wi_trap = 6;
  return;
}
if (a < wi_NIMP) {
  wi_sp = sp;
  wi_top = fp + C[pc + 4];
  wi_imp(a, fp + C[pc + 3]);
  if (wi_trap) return;
  pc += 5;
} else {
  S[sp] = pc + 5;
  S[sp + 1] = fp;
  sp += 2;
  fp = fp + C[pc + 3];
  pc = wi_FE[a - wi_NIMP];
}
//@ trap k
wi_trap = C[pc + 1] + 1;
return;
//@ ld rrk
a = R[fp + C[pc + 2]] + C[pc + 3];
if (a & 3) x = wi_ld32u(a);
else x = M[a >> 2];
R[fp + C[pc + 1]] = x;
pc += 4;
//@ ld8u rrk
a = R[fp + C[pc + 2]] + C[pc + 3];
R[fp + C[pc + 1]] = M[a >> 2] >>> ((a & 3) << 3) & 255;
pc += 4;
//@ ld8s rrk
a = R[fp + C[pc + 2]] + C[pc + 3];
R[fp + C[pc + 1]] = M[a >> 2] << (24 - ((a & 3) << 3)) >> 24;
pc += 4;
//@ ld16u rrk
R[fp + C[pc + 1]] = wi_ld16(R[fp + C[pc + 2]] + C[pc + 3]);
pc += 4;
//@ ld16s rrk
R[fp + C[pc + 1]] = wi_ld16(R[fp + C[pc + 2]] + C[pc + 3]) << 16 >> 16;
pc += 4;
//@ ldf32 rrk
R[fp + C[pc + 1]] = wi_f32from(wi_ld32u(R[fp + C[pc + 2]] + C[pc + 3]));
pc += 4;
//@ ldf64 rrk
a = R[fp + C[pc + 2]] + C[pc + 3];
R[fp + C[pc + 1]] = wi_f64from(wi_ld32u(a), wi_ld32u(a + 4));
pc += 4;
//@ ld64 rrk
a = R[fp + C[pc + 2]] + C[pc + 3];
d = fp + C[pc + 1];
if (a & 3) {
  x = wi_ld32u(a);
  y = wi_ld32u(a + 4);
} else {
  x = M[a >> 2];
  y = M[(a >> 2) + 1];
}
R[d] = x;
H[d] = y;
pc += 4;
//@ ld64_8s rrk
a = R[fp + C[pc + 2]] + C[pc + 3];
x = M[a >> 2] << (24 - ((a & 3) << 3)) >> 24;
d = fp + C[pc + 1];
R[d] = x;
H[d] = x >> 31;
pc += 4;
//@ ld64_8u rrk
d = fp + C[pc + 1];
R[d] = wi_ld8(R[fp + C[pc + 2]] + C[pc + 3]);
H[d] = 0;
pc += 4;
//@ ld64_16s rrk
x = wi_ld16(R[fp + C[pc + 2]] + C[pc + 3]) << 16 >> 16;
d = fp + C[pc + 1];
R[d] = x;
H[d] = x >> 31;
pc += 4;
//@ ld64_16u rrk
d = fp + C[pc + 1];
R[d] = wi_ld16(R[fp + C[pc + 2]] + C[pc + 3]);
H[d] = 0;
pc += 4;
//@ ld64_32s rrk
x = wi_ld32u(R[fp + C[pc + 2]] + C[pc + 3]);
d = fp + C[pc + 1];
R[d] = x;
H[d] = x >> 31;
pc += 4;
//@ ld64_32u rrk
x = wi_ld32u(R[fp + C[pc + 2]] + C[pc + 3]);
d = fp + C[pc + 1];
R[d] = x;
H[d] = 0;
pc += 4;
//@ st rrk
a = R[fp + C[pc + 1]] + C[pc + 3];
if (a & 3) wi_st32u(a, R[fp + C[pc + 2]]);
else M[a >> 2] = R[fp + C[pc + 2]];
pc += 4;
//@ st8 rrk
a = R[fp + C[pc + 1]] + C[pc + 3];
d = a >> 2;
s = (a & 3) << 3;
M[d] = M[d] & ~(255 << s) | (R[fp + C[pc + 2]] & 255) << s;
pc += 4;
//@ st16 rrk
wi_st16(R[fp + C[pc + 1]] + C[pc + 3], R[fp + C[pc + 2]]);
pc += 4;
//@ stf32 rrk
wi_st32u(R[fp + C[pc + 1]] + C[pc + 3], wi_f32bits(R[fp + C[pc + 2]]));
pc += 4;
//@ stf64 rrk
a = R[fp + C[pc + 1]] + C[pc + 3];
wi_f64to(R[fp + C[pc + 2]]);
wi_st32u(a, wi_lo);
wi_st32u(a + 4, wi_hi);
pc += 4;
//@ st64 rrk
a = R[fp + C[pc + 1]] + C[pc + 3];
b = fp + C[pc + 2];
if (a & 3) {
  wi_st32u(a, R[b]);
  wi_st32u(a + 4, H[b]);
} else {
  M[a >> 2] = R[b];
  M[(a >> 2) + 1] = H[b];
}
pc += 4;
//@ memsize r
R[fp + C[pc + 1]] = wi_pages;
pc += 2;
//@ memgrow rr
R[fp + C[pc + 1]] = wi_grow(R[fp + C[pc + 2]]);
pc += 3;
//@ memcopy rrr
wi_copy(R[fp + C[pc + 1]], R[fp + C[pc + 2]], R[fp + C[pc + 3]]);
pc += 4;
//@ memfill rrr
wi_fill(R[fp + C[pc + 1]], R[fp + C[pc + 2]], R[fp + C[pc + 3]]);
pc += 4;
//@ add rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] + R[fp + C[pc + 3]] | 0;
pc += 4;
//@ sub rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] - R[fp + C[pc + 3]] | 0;
pc += 4;
//@ mul rrr
a = R[fp + C[pc + 2]];
b = R[fp + C[pc + 3]];
x = a & 65535;
y = b & 65535;
R[fp + C[pc + 1]] = x * y + ((a >>> 16) * y + x * (b >>> 16) << 16) | 0;
pc += 4;
//@ divs rrr
a = R[fp + C[pc + 2]];
b = R[fp + C[pc + 3]];
if (b == 0) {
  wi_trap = 2;
  return;
}
if (b == -1 && a == -2147483648) {
  wi_trap = 3;
  return;
}
R[fp + C[pc + 1]] = a / b | 0;
pc += 4;
//@ divu rrr
b = R[fp + C[pc + 3]];
if (b == 0) {
  wi_trap = 2;
  return;
}
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) / (b >>> 0) | 0;
pc += 4;
//@ rems rrr
b = R[fp + C[pc + 3]];
if (b == 0) {
  wi_trap = 2;
  return;
}
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] % b | 0;
pc += 4;
//@ remu rrr
b = R[fp + C[pc + 3]];
if (b == 0) {
  wi_trap = 2;
  return;
}
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) % (b >>> 0) | 0;
pc += 4;
//@ and rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] & R[fp + C[pc + 3]];
pc += 4;
//@ or rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] | R[fp + C[pc + 3]];
pc += 4;
//@ xor rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] ^ R[fp + C[pc + 3]];
pc += 4;
//@ shl rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] << R[fp + C[pc + 3]];
pc += 4;
//@ shrs rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] >> R[fp + C[pc + 3]];
pc += 4;
//@ shru rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] >>> R[fp + C[pc + 3]] | 0;
pc += 4;
//@ rotl rrr
a = R[fp + C[pc + 2]];
b = R[fp + C[pc + 3]] & 31;
R[fp + C[pc + 1]] = a << b | a >>> (32 - b);
pc += 4;
//@ rotr rrr
a = R[fp + C[pc + 2]];
b = R[fp + C[pc + 3]] & 31;
R[fp + C[pc + 1]] = a >>> b | a << (32 - b);
pc += 4;
//@ eq rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] == R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ ne rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] != R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ lts rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] < R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ ltu rrr
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) < (R[fp + C[pc + 3]] >>> 0) ? 1 : 0;
pc += 4;
//@ gts rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] > R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ gtu rrr
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) > (R[fp + C[pc + 3]] >>> 0) ? 1 : 0;
pc += 4;
//@ les rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] <= R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ leu rrr
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) <= (R[fp + C[pc + 3]] >>> 0) ? 1 : 0;
pc += 4;
//@ ges rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] >= R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ geu rrr
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) >= (R[fp + C[pc + 3]] >>> 0) ? 1 : 0;
pc += 4;
//@ addi rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] + C[pc + 3] | 0;
pc += 4;
//@ muli rrk
a = R[fp + C[pc + 2]];
b = C[pc + 3];
x = a & 65535;
y = b & 65535;
R[fp + C[pc + 1]] = x * y + ((a >>> 16) * y + x * (b >>> 16) << 16) | 0;
pc += 4;
//@ andi rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] & C[pc + 3];
pc += 4;
//@ ori rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] | C[pc + 3];
pc += 4;
//@ xori rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] ^ C[pc + 3];
pc += 4;
//@ shli rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] << C[pc + 3];
pc += 4;
//@ shrsi rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] >> C[pc + 3];
pc += 4;
//@ shrui rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] >>> C[pc + 3] | 0;
pc += 4;
//@ eqi rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] == C[pc + 3] ? 1 : 0;
pc += 4;
//@ nei rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] != C[pc + 3] ? 1 : 0;
pc += 4;
//@ ltsi rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] < C[pc + 3] ? 1 : 0;
pc += 4;
//@ ltui rrk
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) < (C[pc + 3] >>> 0) ? 1 : 0;
pc += 4;
//@ gtsi rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] > C[pc + 3] ? 1 : 0;
pc += 4;
//@ gtui rrk
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) > (C[pc + 3] >>> 0) ? 1 : 0;
pc += 4;
//@ lesi rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] <= C[pc + 3] ? 1 : 0;
pc += 4;
//@ leui rrk
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) <= (C[pc + 3] >>> 0) ? 1 : 0;
pc += 4;
//@ gesi rrk
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] >= C[pc + 3] ? 1 : 0;
pc += 4;
//@ geui rrk
R[fp + C[pc + 1]] = (R[fp + C[pc + 2]] >>> 0) >= (C[pc + 3] >>> 0) ? 1 : 0;
pc += 4;
//@ eqz rr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] == 0 ? 1 : 0;
pc += 3;
//@ clz rr
R[fp + C[pc + 1]] = wi_clz(R[fp + C[pc + 2]]);
pc += 3;
//@ ctz rr
R[fp + C[pc + 1]] = wi_ctz(R[fp + C[pc + 2]]);
pc += 3;
//@ popcnt rr
R[fp + C[pc + 1]] = wi_popcnt(R[fp + C[pc + 2]]);
pc += 3;
//@ ext8 rr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] << 24 >> 24;
pc += 3;
//@ ext16 rr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] << 16 >> 16;
pc += 3;
//@ add64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
lo = (R[a] >>> 0) + (R[b] >>> 0);
hi = H[a] + H[b] + (lo > 4294967295 ? 1 : 0) | 0;
d = fp + C[pc + 1];
R[d] = lo | 0;
H[d] = hi;
pc += 4;
//@ sub64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
lo = (R[a] >>> 0) - (R[b] >>> 0);
hi = H[a] - H[b] - (lo < 0 ? 1 : 0) | 0;
d = fp + C[pc + 1];
R[d] = lo | 0;
H[d] = hi;
pc += 4;
//@ mul64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
wi_mul64(R[a], H[a], R[b], H[b]);
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 4;
//@ divs64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
n = wi_div64(R[a], H[a], R[b], H[b], 1, 0);
if (n) {
  wi_trap = n;
  return;
}
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 4;
//@ divu64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
n = wi_div64(R[a], H[a], R[b], H[b], 0, 0);
if (n) {
  wi_trap = n;
  return;
}
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 4;
//@ rems64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
n = wi_div64(R[a], H[a], R[b], H[b], 1, 1);
if (n) {
  wi_trap = n;
  return;
}
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 4;
//@ remu64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
n = wi_div64(R[a], H[a], R[b], H[b], 0, 1);
if (n) {
  wi_trap = n;
  return;
}
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 4;
//@ and64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
x = R[a] & R[b];
y = H[a] & H[b];
d = fp + C[pc + 1];
R[d] = x;
H[d] = y;
pc += 4;
//@ or64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
x = R[a] | R[b];
y = H[a] | H[b];
d = fp + C[pc + 1];
R[d] = x;
H[d] = y;
pc += 4;
//@ xor64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
x = R[a] ^ R[b];
y = H[a] ^ H[b];
d = fp + C[pc + 1];
R[d] = x;
H[d] = y;
pc += 4;
//@ shl64 rrr
a = fp + C[pc + 2];
s = R[fp + C[pc + 3]] & 63;
lo = R[a];
hi = H[a];
if (s >= 32) {
  hi = lo << (s - 32);
  lo = 0;
} else if (s > 0) {
  hi = hi << s | lo >>> (32 - s);
  lo = lo << s;
}
d = fp + C[pc + 1];
R[d] = lo;
H[d] = hi;
pc += 4;
//@ shrs64 rrr
a = fp + C[pc + 2];
s = R[fp + C[pc + 3]] & 63;
lo = R[a];
hi = H[a];
if (s >= 32) {
  lo = hi >> (s - 32);
  hi = hi >> 31;
} else if (s > 0) {
  lo = lo >>> s | hi << (32 - s);
  hi = hi >> s;
}
d = fp + C[pc + 1];
R[d] = lo;
H[d] = hi;
pc += 4;
//@ shru64 rrr
a = fp + C[pc + 2];
s = R[fp + C[pc + 3]] & 63;
lo = R[a];
hi = H[a];
if (s >= 32) {
  lo = hi >>> (s - 32) | 0;
  hi = 0;
} else if (s > 0) {
  lo = lo >>> s | hi << (32 - s);
  hi = hi >>> s | 0;
}
d = fp + C[pc + 1];
R[d] = lo;
H[d] = hi;
pc += 4;
//@ rotl64 rrr
a = fp + C[pc + 2];
wi_rot64(R[a], H[a], R[fp + C[pc + 3]], 1);
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 4;
//@ rotr64 rrr
a = fp + C[pc + 2];
wi_rot64(R[a], H[a], R[fp + C[pc + 3]], 0);
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 4;
//@ eq64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = R[a] == R[b] && H[a] == H[b] ? 1 : 0;
pc += 4;
//@ ne64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = R[a] != R[b] || H[a] != H[b] ? 1 : 0;
pc += 4;
//@ lts64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = H[a] < H[b] || (H[a] == H[b] && (R[a] >>> 0) < (R[b] >>> 0)) ? 1 : 0;
pc += 4;
//@ ltu64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = (H[a] >>> 0) < (H[b] >>> 0) || (H[a] == H[b] && (R[a] >>> 0) < (R[b] >>> 0)) ? 1 : 0;
pc += 4;
//@ gts64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = H[a] > H[b] || (H[a] == H[b] && (R[a] >>> 0) > (R[b] >>> 0)) ? 1 : 0;
pc += 4;
//@ gtu64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = (H[a] >>> 0) > (H[b] >>> 0) || (H[a] == H[b] && (R[a] >>> 0) > (R[b] >>> 0)) ? 1 : 0;
pc += 4;
//@ les64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = H[a] < H[b] || (H[a] == H[b] && (R[a] >>> 0) <= (R[b] >>> 0)) ? 1 : 0;
pc += 4;
//@ leu64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = (H[a] >>> 0) < (H[b] >>> 0) || (H[a] == H[b] && (R[a] >>> 0) <= (R[b] >>> 0)) ? 1 : 0;
pc += 4;
//@ ges64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = H[a] > H[b] || (H[a] == H[b] && (R[a] >>> 0) >= (R[b] >>> 0)) ? 1 : 0;
pc += 4;
//@ geu64 rrr
a = fp + C[pc + 2];
b = fp + C[pc + 3];
R[fp + C[pc + 1]] = (H[a] >>> 0) > (H[b] >>> 0) || (H[a] == H[b] && (R[a] >>> 0) >= (R[b] >>> 0)) ? 1 : 0;
pc += 4;
//@ eqz64 rr
a = fp + C[pc + 2];
R[fp + C[pc + 1]] = R[a] == 0 && H[a] == 0 ? 1 : 0;
pc += 3;
//@ clz64 rr
a = fp + C[pc + 2];
if (H[a]) x = wi_clz(H[a]);
else x = 32 + wi_clz(R[a]);
d = fp + C[pc + 1];
R[d] = x;
H[d] = 0;
pc += 3;
//@ ctz64 rr
a = fp + C[pc + 2];
if (R[a]) x = wi_ctz(R[a]);
else x = 32 + wi_ctz(H[a]);
d = fp + C[pc + 1];
R[d] = x;
H[d] = 0;
pc += 3;
//@ popcnt64 rr
a = fp + C[pc + 2];
x = wi_popcnt(R[a]) + wi_popcnt(H[a]);
d = fp + C[pc + 1];
R[d] = x;
H[d] = 0;
pc += 3;
//@ ext8_64 rr
x = R[fp + C[pc + 2]] << 24 >> 24;
d = fp + C[pc + 1];
R[d] = x;
H[d] = x >> 31;
pc += 3;
//@ ext16_64 rr
x = R[fp + C[pc + 2]] << 16 >> 16;
d = fp + C[pc + 1];
R[d] = x;
H[d] = x >> 31;
pc += 3;
//@ ext32_64 rr
x = R[fp + C[pc + 2]];
d = fp + C[pc + 1];
R[d] = x;
H[d] = x >> 31;
pc += 3;
//@ extends rr
x = R[fp + C[pc + 2]];
d = fp + C[pc + 1];
R[d] = x;
H[d] = x >> 31;
pc += 3;
//@ extendu rr
x = R[fp + C[pc + 2]];
d = fp + C[pc + 1];
R[d] = x;
H[d] = 0;
pc += 3;
//@ fadd rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] + R[fp + C[pc + 3]];
pc += 4;
//@ fsub rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] - R[fp + C[pc + 3]];
pc += 4;
//@ fmul rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] * R[fp + C[pc + 3]];
pc += 4;
//@ fdiv rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] / R[fp + C[pc + 3]];
pc += 4;
//@ fadd32 rrr
R[fp + C[pc + 1]] = wi_fround(R[fp + C[pc + 2]] + R[fp + C[pc + 3]]);
pc += 4;
//@ fsub32 rrr
R[fp + C[pc + 1]] = wi_fround(R[fp + C[pc + 2]] - R[fp + C[pc + 3]]);
pc += 4;
//@ fmul32 rrr
R[fp + C[pc + 1]] = wi_fround(R[fp + C[pc + 2]] * R[fp + C[pc + 3]]);
pc += 4;
//@ fdiv32 rrr
R[fp + C[pc + 1]] = wi_fround(R[fp + C[pc + 2]] / R[fp + C[pc + 3]]);
pc += 4;
//@ fmin rrr
R[fp + C[pc + 1]] = wi_fmin(R[fp + C[pc + 2]], R[fp + C[pc + 3]]);
pc += 4;
//@ fmax rrr
R[fp + C[pc + 1]] = wi_fmax(R[fp + C[pc + 2]], R[fp + C[pc + 3]]);
pc += 4;
//@ fcopysign rrr
R[fp + C[pc + 1]] = wi_copysign(R[fp + C[pc + 2]], R[fp + C[pc + 3]]);
pc += 4;
//@ feq rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] == R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ fne rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] != R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ flt rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] < R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ fgt rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] > R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ fle rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] <= R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ fge rrr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] >= R[fp + C[pc + 3]] ? 1 : 0;
pc += 4;
//@ fabs rr
x = R[fp + C[pc + 2]];
if (x < 0 || (x == 0 && 1 / x < 0)) x = -x;
R[fp + C[pc + 1]] = x;
pc += 3;
//@ fneg rr
R[fp + C[pc + 1]] = -R[fp + C[pc + 2]];
pc += 3;
//@ fsqrt rr
R[fp + C[pc + 1]] = Math.sqrt(R[fp + C[pc + 2]]);
pc += 3;
//@ fsqrt32 rr
R[fp + C[pc + 1]] = wi_fround(Math.sqrt(R[fp + C[pc + 2]]));
pc += 3;
//@ fceil rr
R[fp + C[pc + 1]] = -Math.floor(-R[fp + C[pc + 2]]);
pc += 3;
//@ ffloor rr
R[fp + C[pc + 1]] = Math.floor(R[fp + C[pc + 2]]);
pc += 3;
//@ ftrunc rr
R[fp + C[pc + 1]] = wi_trunc(R[fp + C[pc + 2]]);
pc += 3;
//@ fnearest rr
R[fp + C[pc + 1]] = wi_nearest(R[fp + C[pc + 2]]);
pc += 3;
//@ fdemote rr
R[fp + C[pc + 1]] = wi_fround(R[fp + C[pc + 2]]);
pc += 3;
//@ truncs rr
x = wi_truncs32(R[fp + C[pc + 2]]);
if (wi_tc) {
  wi_trap = wi_tc;
  return;
}
R[fp + C[pc + 1]] = x;
pc += 3;
//@ truncu rr
x = wi_truncu32(R[fp + C[pc + 2]]);
if (wi_tc) {
  wi_trap = wi_tc;
  return;
}
R[fp + C[pc + 1]] = x;
pc += 3;
//@ truncsats rr
R[fp + C[pc + 1]] = wi_truncsats32(R[fp + C[pc + 2]]);
pc += 3;
//@ truncsatu rr
R[fp + C[pc + 1]] = wi_truncsatu32(R[fp + C[pc + 2]]);
pc += 3;
//@ trunc64s rr
wi_truncs64(R[fp + C[pc + 2]], 0);
if (wi_tc) {
  wi_trap = wi_tc;
  return;
}
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 3;
//@ trunc64u rr
wi_truncu64(R[fp + C[pc + 2]], 0);
if (wi_tc) {
  wi_trap = wi_tc;
  return;
}
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 3;
//@ truncsat64s rr
wi_truncs64(R[fp + C[pc + 2]], 1);
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 3;
//@ truncsat64u rr
wi_truncu64(R[fp + C[pc + 2]], 1);
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 3;
//@ cvtu rr
R[fp + C[pc + 1]] = R[fp + C[pc + 2]] >>> 0;
pc += 3;
//@ cvt32s rr
R[fp + C[pc + 1]] = wi_fround(R[fp + C[pc + 2]]);
pc += 3;
//@ cvt32u rr
R[fp + C[pc + 1]] = wi_fround(R[fp + C[pc + 2]] >>> 0);
pc += 3;
//@ cvt64s rr
a = fp + C[pc + 2];
R[fp + C[pc + 1]] = H[a] * 4294967296 + (R[a] >>> 0);
pc += 3;
//@ cvt64u rr
a = fp + C[pc + 2];
R[fp + C[pc + 1]] = (H[a] >>> 0) * 4294967296 + (R[a] >>> 0);
pc += 3;
//@ cvt64s32 rr
a = fp + C[pc + 2];
R[fp + C[pc + 1]] = wi_fround(H[a] * 4294967296 + (R[a] >>> 0));
pc += 3;
//@ cvt64u32 rr
a = fp + C[pc + 2];
R[fp + C[pc + 1]] = wi_fround((H[a] >>> 0) * 4294967296 + (R[a] >>> 0));
pc += 3;
//@ bitsf32 rr
R[fp + C[pc + 1]] = wi_f32from(R[fp + C[pc + 2]]);
pc += 3;
//@ f32bits rr
R[fp + C[pc + 1]] = wi_f32bits(R[fp + C[pc + 2]]);
pc += 3;
//@ bitsf64 rr
a = fp + C[pc + 2];
R[fp + C[pc + 1]] = wi_f64from(R[a], H[a]);
pc += 3;
//@ f64bits rr
wi_f64to(R[fp + C[pc + 2]]);
d = fp + C[pc + 1];
R[d] = wi_lo;
H[d] = wi_hi;
pc += 3;
//@end
