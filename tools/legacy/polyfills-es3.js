/* What the fourth tier (IE 6, 7 and 8: docs/ie6-design.md) lacks of ES5 and the shim, Babel's helpers or
 * promise-polyfill call. cdom.legacy-ie6.js puts this first, before polyfills.js. Each is installed only when it is
 * missing; with no defineProperty (IE 6 and 7) they are plain assignments, so the Array ones are enumerable there,
 * and nothing in cdom walks an Array with for-in. ES5_SUPPLIED in build.mjs names exactly these, and the build
 * refuses any other ES5 name in the tier's file. tools/legacy.mjs checks each against the built-in.
 * Also JScript's own bug: splice(start) with no count removes nothing (the standard removes to the end).
 * And what JScript 5.0 (IE 5.0) lacks of what JScript 5.5 has, which the rest of this file and every later part call:
 * the global undefined, Array's push, pop, shift, unshift and splice, Function's call and apply, and Object's
 * hasOwnProperty, and TypeError (made by polyfills.js's cdomError, which it calls only once an error is thrown). So those come first, each written without the others.
 * ES3, as JScript 5.0 reads it. */
(function (w) {
  var O = Object, A = Array.prototype, F = Function.prototype, P = O.prototype;

  /* JScript 5.0 has no binary in, only for-in (`k in o` is a syntax error there, which fails the whole file, so es3()
   * refuses one written here). Where in exists (IE 6 to 8, Firefox 1.0) it is used through new Function, chosen once:
   * JScript 5.0 throws that syntax error when new Function runs, so it is caught here. Only JScript 5.0 takes the fallback,
   * which is O(n) a key: o holds k when its value there is not undefined, or else when a for-in over o lists it. */
  var holds;
  try {
    holds = new Function("o", "k", "return k in o");
  } catch (noIn) {
    holds = function (o, k) {
      if (o[k] !== void 0) return true;
      for (var x in o) {
        if (x == k) return true;
      }
      return false;
    };
  }
  /* JScript 5.0 has no global named undefined: reading it throws. As a property of the window it is one. */
  w["undefined"] = void 0;
  /* JScript 5.0 has Error alone, no TypeError: the files here throw one only on a misuse, which must still throw an
   * error, not "TypeError is undefined". */
  if (!w.TypeError) w.TypeError = function (message) {
    var e = w.cdomError(message);
    e.name = "TypeError";
    return e;
  };
  if (!A.push) A.push = function () {
    for (var i = 0; i < arguments.length; i++) this[this.length] = arguments[i];
    return this.length;
  };
  if (!A.pop) A.pop = function () {
    var n = this.length, v;
    if (!n) return void 0;
    v = this[n - 1];
    this.length = n - 1;
    return v;
  };
  /* splice as the standard has it: from start, count removed (to the end when left out), the rest inserted there. */
  if (!A.splice) A.splice = function (start, count) {
    var n = this.length, add = arguments.length > 2 ? arguments.length - 2 : 0, out = [], tail = [], i, k = 0;
    start = start < 0 ? Math.max(n + start, 0) : Math.min(start | 0, n);
    count = arguments.length < 2 ? n - start : Math.min(Math.max(count | 0, 0), n - start);
    for (i = 0; i < count; i++) out[i] = this[start + i];
    for (i = start + count; i < n; i++) tail[k++] = this[i];
    this.length = start;
    for (i = 0; i < add; i++) this[this.length] = arguments[i + 2];
    for (i = 0; i < k; i++) this[this.length] = tail[i];
    return out;
  };
  if (!A.shift) A.shift = function () {
    return this.length ? this.splice(0, 1)[0] : void 0;
  };
  if (!A.unshift) A.unshift = function () {
    var args = [0, 0];
    for (var i = 0; i < arguments.length; i++) args[i + 2] = arguments[i];
    this.splice.apply(this, args);
    return this.length;
  };
  /* apply, with no apply to build it from: the function is set on the object under a key of this call's own and called
   * through eval with the arguments by index, then the key is taken off. */
  var applying = 0;
  if (!F.apply) F.apply = function (self, args) {
    var o = self == null ? w : self, key = "__cdomApply" + applying++, list = [], i, n = args ? args.length : 0, r;
    if (typeof o !== "object" && typeof o !== "function") o = new O(o);
    for (i = 0; i < n; i++) list[i] = "args[" + i + "]";
    o[key] = this;
    try {
      r = eval("o[key](" + list.join(",") + ")");
    } finally {
      applying--;
      try {
        delete o[key];
      } catch (gone) {
        o[key] = void 0;
      }
    }
    return r;
  };
  if (!F.call) F.call = function (self) {
    var args = [];
    for (var i = 1; i < arguments.length; i++) args[i - 1] = arguments[i];
    return this.apply(self, args);
  };
  /* hasOwnProperty, as JScript 5.0 can tell it: the object has the key and its constructor's prototype does not hold the
   * same value under it. It cannot tell an own property that equals the prototype's, which nothing here sets. */
  if (!P.hasOwnProperty) P.hasOwnProperty = function (k) {
    var proto = this.constructor && this.constructor.prototype;
    if (!holds(this, k)) return false;
    return !proto || proto === this || proto[k] !== this[k];
  };
  var has = P.hasOwnProperty;

  /* JScript does not enumerate an own property that shadows a built-in one (the DontEnum bug). Tested by enumerating,
   * not with propertyIsEnumerable, which JScript 5.0 lacks. */
  var dontEnumBug = (function () {
    for (var k in { toString: null }) if (k === "toString") return false;
    return true;
  })();
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
      if (holds(this, i) && this[i] === v) return i;
    }
    return -1;
  };
  if (!A.lastIndexOf) A.lastIndexOf = function (v, from) {
    var n = this.length >>> 0, i = arguments.length > 1 ? from | 0 : n - 1;
    if (i < 0) i = n + i;
    if (i >= n) i = n - 1;
    for (; i >= 0; i--) {
      if (holds(this, i) && this[i] === v) return i;
    }
    return -1;
  };
  if (!A.forEach) A.forEach = function (f, self) {
    for (var i = 0, n = this.length >>> 0; i < n; i++) {
      if (holds(this, i)) f.call(self, this[i], i, this);
    }
  };
  if (!A.map) A.map = function (f, self) {
    var n = this.length >>> 0, out = new Array(n);
    for (var i = 0; i < n; i++) {
      if (holds(this, i)) out[i] = f.call(self, this[i], i, this);
    }
    return out;
  };
  if (!A.filter) A.filter = function (f, self) {
    var out = [];
    for (var i = 0, n = this.length >>> 0; i < n; i++) {
      if (holds(this, i) && f.call(self, this[i], i, this)) out.push(this[i]);
    }
    return out;
  };
  if (!A.some) A.some = function (f, self) {
    for (var i = 0, n = this.length >>> 0; i < n; i++) {
      if (holds(this, i) && f.call(self, this[i], i, this)) return true;
    }
    return false;
  };
  if (!A.every) A.every = function (f, self) {
    for (var i = 0, n = this.length >>> 0; i < n; i++) {
      if (holds(this, i) && !f.call(self, this[i], i, this)) return false;
    }
    return true;
  };
  if (!A.reduce) A.reduce = function (f, init) {
    var n = this.length >>> 0, i = 0, acc;
    if (arguments.length > 1) acc = init;
    else {
      while (i < n && !holds(this, i)) i++;
      if (i >= n) throw new TypeError("Array.prototype.reduce: an empty array and no initial value");
      acc = this[i++];
    }
    for (; i < n; i++) {
      if (holds(this, i)) acc = f(acc, this[i], i, this);
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
