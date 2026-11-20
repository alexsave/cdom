/* What the fourth tier (IE 6, 7 and 8: docs/ie6-design.md) lacks of ES5 and the shim, Babel's helpers or
 * promise-polyfill call. cdom.legacy-ie6.js puts this first, before polyfills.js. Each is installed only when it is
 * missing; with no defineProperty (IE 6 and 7) they are plain assignments, so the Array ones are enumerable there,
 * and nothing in cdom walks an Array with for-in. ES5_SUPPLIED in build.mjs names exactly these, and the build
 * refuses any other ES5 name in the tier's file. tools/legacy.mjs checks each against the built-in.
 * Also JScript's own bug: splice(start) with no count removes nothing (the standard removes to the end).
 * ES3, as JScript 5.6 reads it. */
(function (w) {
  var O = Object, A = Array.prototype, F = Function.prototype, has = O.prototype.hasOwnProperty;

  /* JScript does not enumerate an own property that shadows a built-in one (the DontEnum bug). */
  var dontEnumBug = !{ toString: null }.propertyIsEnumerable("toString");
  var DONT_ENUM = ["toString", "toLocaleString", "valueOf", "hasOwnProperty", "isPrototypeOf",
    "propertyIsEnumerable", "constructor"];
  if (!O.keys) O.keys = function (o) {
    var out = [], k, i;
    for (k in o) {
      if (has.call(o, k)) out.push(k);
    }
    if (dontEnumBug) {
      for (i = 0; i < DONT_ENUM.length; i++) {
        if (has.call(o, DONT_ENUM[i])) out.push(DONT_ENUM[i]);
      }
    }
    return out;
  };
  /* The one-argument form. ES3 has no object without a prototype, so Object.create(null) is refused rather than
   * answered with one that inherits Object.prototype. */
  if (!O.create) O.create = function (proto, props) {
    if (props !== undefined || proto === null) throw new TypeError("Object.create: no property descriptors or null prototype on this browser");
    function T() {}
    T.prototype = proto;
    return new T();
  };
  if (!Array.isArray) Array.isArray = function (v) { return O.prototype.toString.call(v) === "[object Array]"; };
  if (!Date.now) Date.now = function () { return new Date().getTime(); };
  if (!String.prototype.trim) String.prototype.trim = function () { return String(this).replace(/^\s+|\s+$/g, ""); };
  if (!F.bind) F.bind = function (self) {
    var f = this, bound = A.slice.call(arguments, 1);
    if (typeof f !== "function") throw new TypeError("Function.prototype.bind: not a function");
    return function () { return f.apply(self, bound.concat(A.slice.call(arguments))); };
  };

  /* The Array methods, each over the indexes the array has, as the standard's are. */
  if (!A.indexOf) A.indexOf = function (v, from) {
    var n = this.length >>> 0, i = from | 0;
    if (i < 0) i = Math.max(n + i, 0);
    for (; i < n; i++) {
      if (i in this && this[i] === v) return i;
    }
    return -1;
  };
  if (!A.lastIndexOf) A.lastIndexOf = function (v, from) {
    var n = this.length >>> 0, i = arguments.length > 1 ? from | 0 : n - 1;
    if (i < 0) i = n + i;
    if (i >= n) i = n - 1;
    for (; i >= 0; i--) {
      if (i in this && this[i] === v) return i;
    }
    return -1;
  };
  if (!A.forEach) A.forEach = function (f, self) {
    for (var i = 0, n = this.length >>> 0; i < n; i++) {
      if (i in this) f.call(self, this[i], i, this);
    }
  };
  if (!A.map) A.map = function (f, self) {
    var n = this.length >>> 0, out = new Array(n);
    for (var i = 0; i < n; i++) {
      if (i in this) out[i] = f.call(self, this[i], i, this);
    }
    return out;
  };
  if (!A.filter) A.filter = function (f, self) {
    var out = [];
    for (var i = 0, n = this.length >>> 0; i < n; i++) {
      if (i in this && f.call(self, this[i], i, this)) out.push(this[i]);
    }
    return out;
  };
  if (!A.some) A.some = function (f, self) {
    for (var i = 0, n = this.length >>> 0; i < n; i++) {
      if (i in this && f.call(self, this[i], i, this)) return true;
    }
    return false;
  };
  if (!A.every) A.every = function (f, self) {
    for (var i = 0, n = this.length >>> 0; i < n; i++) {
      if (i in this && !f.call(self, this[i], i, this)) return false;
    }
    return true;
  };
  if (!A.reduce) A.reduce = function (f, init) {
    var n = this.length >>> 0, i = 0, acc;
    if (arguments.length > 1) acc = init;
    else {
      while (i < n && !(i in this)) i++;
      if (i >= n) throw new TypeError("Array.prototype.reduce: an empty array and no initial value");
      acc = this[i++];
    }
    for (; i < n; i++) {
      if (i in this) acc = f(acc, this[i], i, this);
    }
    return acc;
  };

  /* JScript's splice(start) removes nothing; the standard removes everything from start. */
  var probe = [1, 2];
  probe.splice(0);
  if (probe.length) {
    var splice = A.splice;
    A.splice = function (start) {
      if (arguments.length === 1) return splice.call(this, start, this.length);
      return splice.apply(this, arguments);
    };
  }

  /* The shim's clock is performance.now (polyfills.js gives it now); IE 6 to 8 have no performance at all. */
  if (!w.performance) w.performance = {};
})(window);
