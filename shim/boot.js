/* cdom boot. The build inlines this at a page's /*CDOM_BOOT*\/, in a <script> of its own after the shim's:
 * a browser too old to parse the shim fails that script with a syntax error, and this one still runs,
 * because it is ES3.
 * With WebAssembly it starts url on the shim. Without it, it loads cdom.legacy.js (the ES5 shim and its
 * polyfills, next to the page), then url + ".js" (the same wasm compiled to JS by wasm2js, which sets
 * window.cdomFactory), and starts that. Neither fallback file is fetched when WebAssembly exists.
 * mount is an element or its id, as cdom.start takes it; ready, if given, receives the exports. */
function cdomBoot(url, mount, env, ready) {
  if (typeof WebAssembly === "object") {
    cdom.start(url, mount, env).then(ready);
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
  load("cdom.legacy.js", function () {
    load(url + ".js", function () {
      var f = window.cdomFactory;
      window.cdomFactory = undefined;
      cdom.start(f, mount, env).then(ready);
    });
  });
}
