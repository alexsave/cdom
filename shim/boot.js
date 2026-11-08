/* cdom boot. The build inlines this at a page's /*CDOM_BOOT*\/, in a <script> of its own after the shim's:
 * a browser too old to parse the shim fails that script with a syntax error, and this one still runs,
 * because it is ES3.
 * Three tiers, and a browser fetches only its own:
 * 1. WebAssembly: url on the inlined shim.
 * 2. No WebAssembly, typed arrays (IE 10 and 11): cdom.legacy.js (the ES5 shim and its polyfills, next to the
 *    page) and url + ".js" (the same wasm compiled to JS by wasm2js).
 * 3. No typed arrays either (IE 9): cdom.legacy-ie9.js and url + ".ie9.js", the same two with C's memory in a
 *    plain Array of words (tools/legacy/words.js) instead of typed arrays.
 * Either fallback's two files are fetched at once, and the app file sets window.cdomFactory.
 * mount is an element or its id, as cdom.start takes it; ready, if given, receives the exports.
 * The floor is ES5 (IE 9): what the fallback files call and no polyfill supplies. Below it the boot throws one
 * error naming each missing thing, before fetching anything or drawing at all. Without crypto.getRandomValues
 * (IE 9 and 10) the app still starts: the shim's seed() says how, and why cd_seed is then no secret. */
function cdomBoot(url, mount, env, ready) {
  if (typeof WebAssembly === "object") {
    cdom.start(url, mount, env).then(ready);
    return;
  }
  var typed = "ArrayBuffer Int8Array Uint8Array Int16Array Uint16Array Int32Array Uint32Array Float32Array Float64Array"
    .split(" "), words = false, lack = [], i, O = Object, A = Array.prototype;
  for (i = 0; i < typed.length; i++) {
    if (!window[typed[i]]) words = true;
  }
  /* IE 8 has defineProperty for DOM objects alone, and throws on any other. */
  try {
    O.defineProperty({}, "x", { value: 1 });
  } catch (e) {
    lack.push("Object.defineProperty");
  }
  var es5 = [[O.create, "Object.create"], [O.keys, "Object.keys"], [O.getPrototypeOf, "Object.getPrototypeOf"],
    [Array.isArray, "Array.isArray"], [A.forEach, "Array.prototype.forEach"], [A.reduce, "Array.prototype.reduce"],
    [Function.prototype.bind, "Function.prototype.bind"], [Date.now, "Date.now"],
    [document.addEventListener, "addEventListener"]];
  for (i = 0; i < es5.length; i++) {
    if (!es5[i][0]) lack.push(es5[i][1]);
  }
  if (lack.length) throw new Error("cdom: this browser cannot run cdom: it has no " + lack.join(", "));
  var head = document.getElementsByTagName("head")[0];
  function load(src, next) {
    var s = document.createElement("script");
    s.onload = next;
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
    cdom.start(f, mount, env).then(ready);
  }
  load(words ? "cdom.legacy-ie9.js" : "cdom.legacy.js", loaded);
  load(url + (words ? ".ie9.js" : ".js"), loaded);
}
