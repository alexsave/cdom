/* What the ES5 shim and wasm2js output need from a browser that has no WebAssembly. Each is installed only
 * when it is missing. cdom.legacy.js and cdom.legacy-ie9.js put this first, then promise-polyfill, then PEP for
 * pointer events, then the shim compiled to ES5 by Babel. In a browser without typed arrays (IE 9, which runs
 * cdom.legacy-ie9.js and app.wasm.ie9.js), the ones over typed arrays are left out: there the app keeps its
 * memory in words.js's plain Array and rounds floats in software, and TextEncoder makes plain Arrays.
 * Where MDN once published a polyfill, it is that one (MDN's code samples are CC0), as the comment on it says;
 * the rest MDN never had (or had only for arrays), and are written here. tools/legacy.mjs checks every one
 * against the built-in on the same inputs. */
(function (w) {
  "use strict";
  var M = Math;

  /* JScript 5.0's (IE 5.0's) Error(n) takes a number, not a message: new Error("x") there has no message. Every error the
   * fourth tier's files make with a message, the shim's and the app's (app.wasm.ie9.js, which the third tier runs too),
   * is made by this instead: build.mjs's pass (es3.mjs's messages) rewrites each new Error(m) to cdomError(m), and es3()
   * refuses one left. The message is in description too, as JScript's own errors carry it. */
  w.cdomError = function (message) {
    var e = new Error();
    e.message = e.description = message;
    return e;
  };

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
  if (!M.fround && w.Float32Array) M.fround = (function (array) {
    return function (x) { return array[0] = x, array[0]; };
  })(new Float32Array(1));
  if (!M.trunc) M.trunc = function (v) { return v < 0 ? M.ceil(v) : M.floor(v); };

  /* The shim checks globalThis.crypto; in a page the global object is the window. */
  if (typeof globalThis === "undefined") w.globalThis = w;
  /* IE 11 has getRandomValues only as msCrypto; the shim seeds cd_seed from crypto.getRandomValues. */
  if (!w.crypto && w.msCrypto) w.crypto = w.msCrypto;
  /* MDN's Object.entries polyfill: the shim wraps each export it gets from C. */
  if (!Object.entries) {
    Object.entries = function (obj) {
      var ownProps = Object.keys(obj), i = ownProps.length, resArray = new Array(i);
      while (i--) resArray[i] = [ownProps[i], obj[ownProps[i]]];
      return resArray;
    };
  }

  /* wasm2js lowers memory.copy and memory.fill to these, on a Uint8Array over the whole memory. set copies
   * as if through a temporary when the two ranges share a buffer, so copyWithin may overlap. */
  var U8 = w.Uint8Array ? Uint8Array.prototype : {};
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
  /* IE 6 and 7 have no defineProperty, and IE 8's takes DOM objects alone: there it is a plain assignment. */
  if (!String.prototype.startsWith) {
    var startsWith = function (search, rawPos) {
      var pos = rawPos > 0 ? rawPos | 0 : 0;
      return this.substring(pos, pos + search.length) === search;
    };
    try {
      Object.defineProperty(String.prototype, "startsWith", { value: startsWith });
    } catch (e) {
      String.prototype.startsWith = startsWith;
    }
  }
  /* String.prototype.trim, which the network's headers use (IE 9 has it, and the polyfill is behind a check for browsers that
   * do not). MDN's polyfill. */
  if (!String.prototype.trim) {
    String.prototype.trim = function () {
      return this.replace(/^[\s\uFEFF\xA0]+|[\s\uFEFF\xA0]+$/g, "");
    };
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
    /* Without typed arrays, a plain Array of the bytes. */
    if (!w.TextEncoder.prototype.encode) w.TextEncoder.prototype.encode = function (s) {
      var d;
      if (!w.Uint8Array) {
        d = new Array(s.length * 3);
        d.length = into(s, d).written;
        return d;
      }
      d = new Uint8Array(s.length * 3);
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

  /* No IE has ChildNode.remove, which the shim removes nodes with. MDN's polyfill (the CharacterData and
   * DocumentType prototypes as well as Element's), without its Array.prototype.forEach. The fourth tier calls
   * dom-old.js's remove instead: IE 6 and 7 have no Element to put it on, and an ES3 browser that has one (Firefox
   * 1.0) has no defineProperty. */
  if (Object.defineProperty) (function (list) {
    for (var i = 0; i < list.length; i++) {
      if (list[i] && !Object.prototype.hasOwnProperty.call(list[i], "remove")) {
        Object.defineProperty(list[i], "remove", {
          configurable: true, enumerable: true, writable: true,
          value: function remove() { if (this.parentNode !== null) this.parentNode.removeChild(this); }
        });
      }
    }
  })([w.Element && Element.prototype, w.CharacterData && CharacterData.prototype, w.DocumentType && DocumentType.prototype]);

  /* performance.now is IE 10's (performance is IE 9's): the ms since the page's navigation started, as the real
   * one counts them, so cd_now and a frame's time keep one clock. */
  if (w.performance && !w.performance.now) {
    var start = w.performance.timing && w.performance.timing.navigationStart || new Date().getTime();
    w.performance.now = function () { return new Date().getTime() - start; };
  }

  /* devicePixelRatio is IE 11's; IE 9 and 10 have the same ratio as screen.deviceXDPI over logicalXDPI. Read
   * through a getter, as the real one is, since zoom changes it. Without it, the shim's viewport read throws. */
  /* IE 6 to 8 have no getters on the window: there the fourth tier's dom-old.js reads the ratio itself. */
  if (w.devicePixelRatio === void 0) {
    try {
      Object.defineProperty(w, "devicePixelRatio", {
        configurable: true,
        get: function () {
          var s = w.screen;
          return s && s.deviceXDPI && s.logicalXDPI ? s.deviceXDPI / s.logicalXDPI : 1;
        }
      });
    } catch (e) {}
  }

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

  /* The network (src/cdom_net.c) asks the shim for fetch, which IE 5 to 11 have none of: an XMLHttpRequest stands in, and on IE 5
   * and 6, which have none either, ActiveXObject("Microsoft.XMLHTTP"). Only what the shim's one call needs: fetch(url,
   * { method, headers: [[name, value]...], body: bytes or undefined, signal }) answering { status, arrayBuffer() }, and an
   * AbortController for the deadline. A request that never arrived (a network failure, a status of 0 or one of WinInet's
   * 12000s, a header the browser refuses) rejects, as fetch's does; abort() rejects too, and the shim tells the two apart by
   * the signal. What comes back is bytes, however the browser lets a script read them (docs/ie6-design.md, "The network"):
   * - a browser with typed arrays and responseType (IE 10, 11): an ArrayBuffer, exact;
   * - one that has overrideMimeType (every browser but IE: Firefox 1.0 on): "text/plain; charset=x-user-defined", which
   *   reads each byte as a character of its own, exact;
   * - IE 5 to 9, which have neither and, without VBScript, no way to the bytes but responseText: a response the server
   *   labels charset=windows-1252 (or iso-8859-1) is its bytes, exact (Windows maps all 256 to a character, which is
   *   mapped back); any other is text, and its characters are encoded as UTF-8, exact for ASCII and for UTF-8 text
   *   the server says is utf-8, and not for a binary body without that label.
   * A body to send is bytes: sent as they are where typed arrays are, and otherwise as text, which only UTF-8 bytes can be
   * (XMLHttpRequest sends a string as UTF-8): any other body rejects. IE 8 and 9 reach only the page's own origin (their
   * cross-origin object is XDomainRequest, which is not used). */
  if (!w.AbortController) {
    w.AbortController = function () {
      var fns = [], signal = { aborted: false, addEventListener: function (type, f) { fns.push(f); } };
      this.signal = signal;
      this.abort = function () {
        var i;
        if (signal.aborted) return;
        signal.aborted = true;
        for (i = 0; i < fns.length; i++) fns[i]();
      };
    };
  }
  if (typeof w.fetch !== "function") (function () {
    /* Windows-1252's 0x80 to 0x9f, as Unicode (the five it leaves out are the C1 controls of the same number). */
    var C1 = [0x20ac, 0x81, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021, 0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0x8d, 0x17d, 0x8f,
      0x90, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0x9d, 0x17e, 0x178];
    var back = null;
    var newXhr = function () {
      if (w.XMLHttpRequest) return new w.XMLHttpRequest();
      if (w.ActiveXObject) {
        try { return new w.ActiveXObject("Microsoft.XMLHTTP"); } catch (e) {}
      }
      return null;
    };
    var asBytes = function (s, mime) {
      var n = s.length, out = new Array(n), i, c;
      if (!back) {
        back = {};
        for (i = 0; i < C1.length; i++) back["c" + C1[i]] = 0x80 + i;
      }
      for (i = 0; i < n; i++) {
        c = s.charCodeAt(i);
        out[i] = mime ? c & 0xff : c < 256 ? c : back["c" + c] || 63;
      }
      return out;
    };
    var charsetOf = function (xhr) {
      var type = "", m;
      try { type = xhr.getResponseHeader("Content-Type") || ""; } catch (e) {}
      m = /charset=([^;\s"]+)/i.exec(type);
      return m ? m[1].toLowerCase() : "";
    };
    /* The text of a body of bytes, or null when the bytes are not UTF-8 (the polyfilled decoder makes U+FFFD of what is not). */
    var textOf = function (bytes) {
      var s = new w.TextDecoder().decode(bytes);
      /* One byte that is not UTF-8 becomes U+FFFD, which is three bytes: the lengths differ whenever the bytes were not UTF-8. */
      return new w.TextEncoder().encode(s).length === bytes.length ? s : null;
    };
    w.fetch = function (url, init) {
      init = init || {};
      return new w.Promise(function (resolve, reject) {
        var xhr = newXhr(), signal = init.signal, headers = init.headers || [], body = init.body, typed, mime = false, sent = null, i;
        var finished = false;
        var fail = function () {
          if (finished) return;
          finished = true;
          reject(w.cdomError("fetch failed"));
        };
        if (!xhr) { fail(); return; }
        typed = typeof w.ArrayBuffer !== "undefined" && typeof xhr.responseType === "string";
        if (body && !typed) {
          sent = textOf(body);
          if (sent === null) { fail(); return; }
        } else if (body) sent = body;
        try {
          xhr.open(String(init.method || "GET").toUpperCase(), url, true);
          for (i = 0; i < headers.length; i++) xhr.setRequestHeader(headers[i][0], headers[i][1]);
          if (typed) xhr.responseType = "arraybuffer";
          else if (xhr.overrideMimeType) {
            xhr.overrideMimeType("text/plain; charset=x-user-defined");
            mime = true;
          }
        } catch (e) { fail(); return; }
        xhr.onreadystatechange = function () {
          var status = 0, data, charset;
          if (finished || xhr.readyState !== 4) return;
          xhr.onreadystatechange = function () {};
          try { status = xhr.status; } catch (e) {}
          if (status === 1223) status = 204;
          if (!(status >= 100 && status < 600)) { fail(); return; }
          try {
            if (typed) data = xhr.response || new w.ArrayBuffer(0);
            else {
              charset = charsetOf(xhr);
              data = mime || charset === "windows-1252" || charset === "iso-8859-1" ? asBytes(xhr.responseText, mime)
                : new w.TextEncoder().encode(xhr.responseText);
            }
          } catch (e) { fail(); return; }
          finished = true;
          resolve({ status: status, arrayBuffer: function () { return w.Promise.resolve(data); } });
        };
        if (signal) {
          if (signal.aborted) { fail(); return; }
          signal.addEventListener("abort", function () {
            if (finished) return;
            fail();
            xhr.onreadystatechange = function () {};
            try { xhr.abort(); } catch (e) {}
          });
        }
        try { xhr.send(sent === null ? null : sent); } catch (e) { fail(); }
      });
    };
  })();
})(window);
