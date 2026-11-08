/* What the ES5 shim and wasm2js output need from a browser that has no WebAssembly. Each is installed only
 * when it is missing. cdom.legacy.js puts this first, then promise-polyfill, then PEP for pointer events,
 * then the shim compiled to ES5 by Babel.
 * Where MDN once published a polyfill, it is that one (MDN's code samples are CC0), as the comment on it says;
 * the rest MDN never had (or had only for arrays), and are written here. tools/legacy.mjs checks every one
 * against the built-in on the same inputs. */
(function (w) {
  "use strict";
  var M = Math;

  /* wasm2js lowers i32 multiply, count-leading-zeros, f32 rounding and truncation to these. */
  /* MDN's Math.imul polyfill (its first form). */
  if (!M.imul) M.imul = function (a, b) {
    var aHi = (a >>> 16) & 0xffff;
    var aLo = a & 0xffff;
    var bHi = (b >>> 16) & 0xffff;
    var bLo = b & 0xffff;
    return ((aLo * bLo) + (((aHi * bLo + aLo * bHi) << 16) >>> 0) | 0);
  };
  /* MDN's Math.clz32 polyfill. */
  if (!M.clz32) M.clz32 = (function (log, LN2) {
    return function (x) {
      var asUint = x >>> 0;
      if (asUint === 0) return 32;
      return 31 - (log(asUint) / LN2 | 0) | 0;
    };
  })(M.log, M.LN2);
  /* MDN's Math.fround polyfill (its typed-array form). */
  if (!M.fround) M.fround = (function (array) {
    return function (x) { return array[0] = x, array[0]; };
  })(new Float32Array(1));
  if (!M.trunc) M.trunc = function (v) { return v < 0 ? M.ceil(v) : M.floor(v); };

  /* wasm2js lowers memory.copy and memory.fill to these, on a Uint8Array over the whole memory. set copies
   * as if through a temporary when the two ranges share a buffer, so copyWithin may overlap. */
  var U8 = Uint8Array.prototype;
  /* An index as the built-ins take it: negative counts from the end, and it is clamped to the array. */
  var at = function (v, n, d) {
    if (v === undefined) return d;
    v = M.trunc(+v) || 0;
    return v < 0 ? M.max(n + v, 0) : M.min(v, n);
  };
  if (!U8.copyWithin) U8.copyWithin = function (to, from, end) {
    var n = this.length, count;
    to = at(to, n, 0); from = at(from, n, 0); end = at(end, n, n);
    count = M.min(end - from, n - to);
    if (count > 0) this.set(this.subarray(from, from + count), to);
    return this;
  };
  if (!U8.fill) U8.fill = function (v, from, end) {
    var n = this.length, i = at(from, n, 0), e = at(end, n, n);
    for (; i < e; i++) this[i] = v;
    return this;
  };

  /* The shim tests path segments for a "--" prefix; PEP reads touch-action values with includes.
   * MDN's String.prototype.startsWith and String.prototype.includes polyfills. */
  if (!String.prototype.startsWith) {
    Object.defineProperty(String.prototype, "startsWith", {
      value: function (search, rawPos) {
        var pos = rawPos > 0 ? rawPos | 0 : 0;
        return this.substring(pos, pos + search.length) === search;
      }
    });
  }
  if (!String.prototype.includes) {
    String.prototype.includes = function (search, start) {
      if (search instanceof RegExp) throw TypeError("first argument must not be a RegExp");
      if (start === undefined) start = 0;
      return this.indexOf(search, start) !== -1;
    };
  }

  /* UTF-8 only, which is all the shim asks of them. Invalid bytes decode as U+FFFD one at a time, and a
   * lone surrogate encodes as U+FFFD, as the real ones do. encodeInto never writes part of a character. */
  if (!w.TextDecoder) {
    var cont = function (b, i) { return (b[i] & 0xc0) === 0x80; };
    w.TextDecoder = function () {};
    w.TextDecoder.prototype.decode = function (b) {
      var s = "", u = [], i = 0, n = b ? b.length : 0, c, cp;
      while (i < n) {
        c = b[i++];
        if (c < 0x80) cp = c;
        else if (c >= 0xc2 && c < 0xe0 && i < n && cont(b, i)) cp = (c & 0x1f) << 6 | b[i++] & 0x3f;
        else if (c >= 0xe0 && c < 0xf0 && i + 1 < n && cont(b, i) && cont(b, i + 1)) {
          cp = (c & 0x0f) << 12 | (b[i] & 0x3f) << 6 | b[i + 1] & 0x3f;
          if (cp < 0x800 || (cp >= 0xd800 && cp < 0xe000)) cp = 0xfffd; else i += 2;
        } else if (c >= 0xf0 && c < 0xf5 && i + 2 < n && cont(b, i) && cont(b, i + 1) && cont(b, i + 2)) {
          cp = (c & 0x07) << 18 | (b[i] & 0x3f) << 12 | (b[i + 1] & 0x3f) << 6 | b[i + 2] & 0x3f;
          if (cp < 0x10000 || cp > 0x10ffff) cp = 0xfffd; else i += 3;
        } else cp = 0xfffd;
        if (cp > 0xffff) { cp -= 0x10000; u.push(0xd800 | cp >> 10, 0xdc00 | cp & 0x3ff); } else u.push(cp);
        if (u.length > 8190) { s += String.fromCharCode.apply(null, u); u = []; }
      }
      return s + String.fromCharCode.apply(null, u);
    };
  }
  if (!w.TextEncoder || !w.TextEncoder.prototype.encodeInto) {
    var into = function (s, d) {
      var i = 0, o = 0, n = s.length, cap = d.length, c, c2, k, len;
      for (; i < n; i += k) {
        c = s.charCodeAt(i); k = 1;
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < n) {
          c2 = s.charCodeAt(i + 1);
          if (c2 >= 0xdc00 && c2 < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00); k = 2; }
        }
        if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
        len = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
        if (o + len > cap) break;
        if (len === 1) d[o++] = c;
        else if (len === 2) { d[o++] = 0xc0 | c >> 6; d[o++] = 0x80 | c & 0x3f; }
        else if (len === 3) { d[o++] = 0xe0 | c >> 12; d[o++] = 0x80 | c >> 6 & 0x3f; d[o++] = 0x80 | c & 0x3f; }
        else {
          d[o++] = 0xf0 | c >> 18; d[o++] = 0x80 | c >> 12 & 0x3f;
          d[o++] = 0x80 | c >> 6 & 0x3f; d[o++] = 0x80 | c & 0x3f;
        }
      }
      return { read: i, written: o };
    };
    if (!w.TextEncoder) w.TextEncoder = function () {};
    w.TextEncoder.prototype.encodeInto = into;
    if (!w.TextEncoder.prototype.encode) w.TextEncoder.prototype.encode = function (s) {
      var d = new Uint8Array(s.length * 3);
      return d.subarray(0, into(s, d).written);
    };
  }

  /* Events dispatched while the shim applies a batch wait for this. A MutationObserver callback is a
   * microtask; without one, a timeout is still after the batch, which is what the shim needs. */
  if (typeof w.queueMicrotask !== "function") {
    var MO = w.MutationObserver || w.WebKitMutationObserver;
    if (MO) {
      var q = [], node = document.createTextNode(""), flip = 0;
      new MO(function () {
        var run = q, i;
        q = [];
        for (i = 0; i < run.length; i++) {
          try { run[i](); } catch (e) { w.reportError(e); }
        }
      }).observe(node, { characterData: true });
      w.queueMicrotask = function (f) { if (q.push(f) === 1) node.data = String(flip ^= 1); };
    } else w.queueMicrotask = function (f) { setTimeout(f, 0); };
  }

  /* An uncaught error in a timeout reaches window.onerror and error listeners, as reportError's does. */
  if (typeof w.reportError !== "function") w.reportError = function (e) { setTimeout(function () { throw e; }, 0); };

  if (!w.requestAnimationFrame) {
    var raf = w.webkitRequestAnimationFrame, now = function () {
      return w.performance && w.performance.now ? w.performance.now() : new Date().getTime();
    };
    w.requestAnimationFrame = raf ? function (f) { return raf.call(w, f); }
      : function (f) { return setTimeout(function () { f(now()); }, 16); };
    w.cancelAnimationFrame = w.webkitCancelAnimationFrame || w.webkitCancelRequestAnimationFrame || clearTimeout;
  }

  /* Before options objects, addEventListener's third argument was useCapture, so the shim's
   * { passive: false } would register every listener in the capture phase. Where the options object is not
   * read (MDN's passiveSupported test), pass its capture member instead. */
  var options = false, probe;
  try {
    probe = Object.defineProperty({}, "passive", { get: function () { options = true; return false; } });
    w.addEventListener("cdom-probe", null, probe);
    w.removeEventListener("cdom-probe", null, probe);
  } catch (e) {}
  if (!options) {
    var wrap = function (o) {
      if (!o || !Object.prototype.hasOwnProperty.call(o, "addEventListener")) return;
      var add = o.addEventListener, rem = o.removeEventListener;
      var cap = function (c) { return typeof c === "object" && c !== null ? !!c.capture : !!c; };
      o.addEventListener = function (t, f, c) { return add.call(this, t, f, cap(c)); };
      o.removeEventListener = function (t, f, c) { return rem.call(this, t, f, cap(c)); };
    };
    wrap(w.EventTarget && w.EventTarget.prototype);
    wrap(w.Node && w.Node.prototype);
    wrap(w.Window && w.Window.prototype);
    wrap(w);
  }
})(window);
