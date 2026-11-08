/* cdom boot. The build inlines this at a page's /*CDOM_BOOT*\/, in a <script> of its own after the shim's:
 * a browser too old to parse the shim fails that script with a syntax error, and this one still runs,
 * because it is ES3.
 * With WebAssembly it starts url on the shim. Without it, it loads cdom.legacy.js (the ES5 shim and its
 * polyfills, next to the page) and url + ".js" (the same wasm compiled to JS by wasm2js, which sets
 * window.cdomFactory) in parallel, and starts that. Neither fallback file is fetched when WebAssembly
 * exists.
 * mount is an element or its id, as cdom.start takes it; ready, if given, receives the exports.
 * The fallback's floor is what no polyfill can supply: typed arrays (IE 10), which wasm2js output and the
 * shim keep C's memory in, and crypto.getRandomValues (IE 11, as msCrypto), cd_seed's only source. Below
 * it the boot throws one error naming each missing thing, before fetching anything or drawing at all. */
function cdomBoot(url, mount, env, ready) {
  if (typeof WebAssembly === "object") {
    cdom.start(url, mount, env).then(ready);
    return;
  }
  var need = "ArrayBuffer Int8Array Uint8Array Int16Array Uint16Array Int32Array Uint32Array Float32Array Float64Array"
    .split(" "), lack = [], i, c = window.crypto || window.msCrypto;
  for (i = 0; i < need.length; i++) {
    if (!window[need[i]]) lack.push(need[i]);
  }
  if (!c || !c.getRandomValues) lack.push("crypto.getRandomValues");
  if (lack.length) throw new Error("cdom: this browser cannot run cdom without WebAssembly: it has no " + lack.join(", "));
  var head = document.getElementsByTagName("head")[0];
  function load(src, next) {
    var s = document.createElement("script");
    s.onload = next;
    s.onerror = function () { throw new Error("cdom: could not load " + src); };
    s.src = src;
    head.appendChild(s);
  }
  // Both are fetched at once and may run in either order: url + ".js" only defines window.cdomFactory,
  // so the app starts when the second of them has run.
  var left = 2;
  function loaded() {
    if (--left) return;
    var f = window.cdomFactory;
    window.cdomFactory = undefined;
    cdom.start(f, mount, env).then(ready);
  }
  load("cdom.legacy.js", loaded);
  load(url + ".js", loaded);
}
