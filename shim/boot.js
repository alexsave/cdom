/* cdom boot. The build inlines this at a page's /*CDOM_BOOT*\/, in a <script> of its own after the shim's:
 * a browser too old to parse the shim fails that script with a syntax error, and this one still runs,
 * because it is ES3.
 * With WebAssembly it starts url on the shim. Without it, it loads cdom.legacy.js (the ES5 shim and its
 * polyfills, next to the page) and url + ".js" (the same wasm compiled to JS by wasm2js, which sets
 * window.cdomFactory) in parallel, and starts that. Neither fallback file is fetched when WebAssembly
 * exists.
 * url may be the pair [scalar, SIMD] cdom.start takes (cdom.mk's APP_SIMD): cdom.start picks one, and the fallback
 * is the scalar module's wasm2js build, url[0] + ".js".
 * mount is an element or its id, as cdom.start takes it; ready, if given, receives the exports; options
 * is cdom.start's fourth argument. */
function cdomBoot(url, mount, env, ready, options) {
  if (typeof WebAssembly === "object") {
    cdom.start(url, mount, env, options).then(ready);
    return;
  }
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
    cdom.start(f, mount, env, options).then(ready);
  }
  load("cdom.legacy.js", loaded);
  load((typeof url === "string" ? url : url[0]) + ".js", loaded);
}
