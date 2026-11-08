/* cdom boot. The build inlines this into the page, after <script src="cdom.js">. It is ES3, so that it runs
 * in a browser too old to parse cdom.js.
 * With WebAssembly it starts url on cdom.js. Without it, it loads cdom.legacy.js (the ES5 shim and its
 * polyfills, next to the page), then url + ".js" (the same wasm compiled to JS by wasm2js, which sets
 * window.cdomFactory), and starts that. Neither fallback file is fetched when WebAssembly exists.
 * ready, if given, receives the exports. */
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
      var f = window.cdomFactory, x;
      window.cdomFactory = undefined;
      x = cdom.start(f, mount, env);
      if (ready) ready(x);
    });
  });
}
