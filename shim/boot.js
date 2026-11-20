/* cdom boot. The build inlines this at a page's /*CDOM_BOOT*\/, in a <script> of its own after the shim's:
 * a browser too old to parse the shim fails that script with a syntax error, and this one still runs,
 * because it is ES3.
 * Four tiers, and a browser fetches only its own:
 * 1. WebAssembly: url on the inlined shim.
 * 2. No WebAssembly, typed arrays (IE 10 and 11): cdom.legacy.js (the ES5 shim and its polyfills, next to the
 *    page) and url + ".js" (the same wasm compiled to JS by wasm2js).
 * 3. No typed arrays either (IE 9): cdom.legacy-ie9.js and url + ".ie9.js", the same two with C's memory in a
 *    plain Array of words (tools/legacy/words.js) instead of typed arrays.
 * 4. No ES5 or no addEventListener (IE 6, 7 and 8): cdom.legacy-ie6.js (the shim in ES3 on the old DOM,
 *    tools/legacy/dom-old.js) and the same url + ".ie9.js", which is ES3 too (docs/ie6-design.md).
 * Either fallback's two files are fetched at once, and the app file sets window.cdomFactory.
 * url may be the pair [scalar, SIMD] cdom.start takes (cdom.mk's APP_SIMD): cdom.start picks one, and either fallback
 * is the scalar module's build, url[0] + ".js" or url[0] + ".ie9.js".
 * mount is an element or its id, as cdom.start takes it; ready, if given, receives the exports; options
 * is cdom.start's fourth argument.
 * The floor is the fourth tier's: document.getElementById, and attachEvent or addEventListener. Below it the boot
 * throws one error naming each missing thing, before fetching anything or drawing at all. Without crypto.getRandomValues
 * (IE 9 and 10) the app still starts: the shim's seed() says how, and why cd_seed is then no secret. */
function cdomBoot(url, mount, env, ready, options) {
  if (typeof WebAssembly === "object") {
    cdom.start(url, mount, env, options).then(ready);
    return;
  }
  var typed = "ArrayBuffer Int8Array Uint8Array Int16Array Uint16Array Int32Array Uint32Array Float32Array Float64Array"
    .split(" "), words = false, old = false, lack = [], i, O = Object, A = Array.prototype, d = document;
  for (i = 0; i < typed.length; i++) {
    if (!window[typed[i]]) words = true;
  }
  /* IE 8 has defineProperty for DOM objects alone, and throws on any other; IE 6 and 7 have none, and calling it
   * throws too. */
  try {
    O.defineProperty({}, "x", { value: 1 });
  } catch (e) {
    old = true;
  }
  /* The ES5 the second and third tiers' files call and no polyfill of theirs supplies, and the W3C listeners: any
   * one missing is the fourth tier, which brings its own (polyfills-es3.js, dom-old.js). */
  var es5 = [O.create, O.keys, O.getPrototypeOf, Array.isArray, A.forEach, A.reduce, Function.prototype.bind, Date.now,
    d.addEventListener];
  for (i = 0; i < es5.length; i++) {
    if (!es5[i]) old = true;
  }
  if (!d.getElementById) lack.push("document.getElementById");
  if (!d.addEventListener && !d.attachEvent) lack.push("addEventListener or attachEvent");
  if (lack.length) throw new Error("cdom: this browser cannot run cdom: it has no " + lack.join(", "));
  var head = d.getElementsByTagName("head")[0];
  /* IE 6 to 8 fire no load on a script: readystatechange, loaded or complete (either may come, and IE 9 and 10 fire
   * both kinds), so next runs once. */
  function load(src, next) {
    var s = d.createElement("script"), done = false;
    s.onload = s.onreadystatechange = function () {
      if (done || (s.readyState && s.readyState !== "loaded" && s.readyState !== "complete")) return;
      done = true;
      s.onload = s.onreadystatechange = null;
      next();
    };
    s.onerror = function () { throw new Error("cdom: could not load " + src); };
    s.src = src;
    head.appendChild(s);
  }
  // Both are fetched at once and may run in either order: the app file only defines window.cdomFactory,
  // so the app starts when the second of them has run.
  var left = 2;
  function loaded() {
    if (--left) return;
    var f = window.cdomFactory;
    window.cdomFactory = undefined;
    cdom.start(f, mount, env, options).then(ready);
  }
  var base = typeof url === "string" ? url : url[0];
  load(old ? "cdom.legacy-ie6.js" : words ? "cdom.legacy-ie9.js" : "cdom.legacy.js", loaded);
  load(base + (old || words ? ".ie9.js" : ".js"), loaded);
}
