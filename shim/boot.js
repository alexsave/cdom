/* cdom boot. The build inlines this at a page's /*CDOM_BOOT*\/, in a <script> of its own after the shim's:
 * a browser too old to parse the shim fails that script with a syntax error, and this one still runs.
 * It is two parts, which the build writes as two <script>s. The first is JavaScript 1.0, so Netscape 2 parses it
 * (tools/legacy/js11.mjs, js10): cdomBoot, which takes the fifth tier itself or hands the others to the second part,
 * cdomBootDom, which is ES3 (try, throw, closures) and which the frames tier's browsers fail to parse and never call.
 * Five tiers, and a browser fetches only its own:
 * 1. WebAssembly: url on the inlined shim.
 * 2. No WebAssembly, typed arrays (IE 10 and 11): cdom.legacy.js (the ES5 shim and its polyfills, next to the
 *    page) and url + ".js" (the same wasm compiled to JS by wasm2js).
 * 3. No typed arrays either (IE 9): cdom.legacy-ie9.js and url + ".ie9.js", the same two with C's memory in a
 *    plain Array of words (tools/legacy/words.js) instead of typed arrays.
 * 4. No ES5 or no addEventListener (IE 5 to 8): cdom.legacy-ie6.js (the shim in ES3 on the old DOM,
 *    tools/legacy/dom-old.js) and the same url + ".ie9.js", which is ES3 too (docs/ie6-design.md).
 * 5. No getElementById or no createElement (Netscape 2 to 4, IE 3 and 4): the frames tier (docs/frames-design.md).
 *    cdom.frames.js (tools/legacy/frames.js, JavaScript 1.1) and url + ".js1.js" (the wasm compiled to JavaScript 1.0
 *    by tools/js1c, docs/js1-compile-design.md), written into the page while it parses, then a frameset whose one
 *    frame the shim writes whole.
 * Either of the middle fallbacks' two files are fetched at once, and the app file sets window.cdomFactory.
 * url may be the pair [scalar, SIMD] cdom.start takes (cdom.mk's APP_SIMD): cdom.start picks one, and every fallback
 * is the scalar module's build, url[0] + ".js" or url[0] + ".ie9.js".
 * mount is an element or its id, as cdom.start takes it; ready, if given, receives the exports; options
 * is cdom.start's fourth argument.
 * The floor of the DOM tiers is the fourth tier's: attachEvent or addEventListener. A browser with getElementById and
 * neither, the boot throws one error naming each missing thing, before fetching anything or drawing at all. Without
 * crypto.getRandomValues (IE 9 and 10) the app still starts: the shim's seed() says how, and why cd_seed is then no
 * secret. */
function cdomBoot(url, mount, env, ready, options) {
  var d = document;
  if (d.getElementById && d.createElement) {
    cdomBootDom(url, mount, env, ready, options);
    return;
  }
  cdomBootFrames(url, mount, env, ready, options);
}
/* The fifth tier: no DOM to build in, so the page becomes a frameset (docs/frames-design.md, section 3). The arguments
 * wait in globals, since JavaScript 1.0 has no closures, for the script written after the two files, which runs once
 * both have; a SCRIPT SRC the parser meets blocks it, so they run in order. cdomFramesStart then writes the frameset.
 * url is a string or a pair: only a string has substring. */
function cdomBootFrames(url, mount, env, ready, options) {
  var base = url;
  if (!url.substring) base = url[0];
  cdomBootMount = mount;
  cdomBootEnv = env;
  cdomBootReady = ready;
  cdomBootOptions = options;
  document.write("<script src=\"cdom.frames.js\"></scr" + "ipt><script src=\"" + base + ".js1.js\"></scr" + "ipt>"
    + "<script>cdomFramesStart(window.cdomFactory, cdomBootMount, cdomBootEnv, cdomBootReady, cdomBootOptions)</scr" + "ipt>");
}
/*CDOM_BOOT_ES3*/
/* The four DOM tiers. */
function cdomBootDom(url, mount, env, ready, options) {
  /* A start that fails rejects its promise, which no browser reports by itself on the old tiers (the polyfilled
   * Promise has no unhandled-rejection event): it is thrown again from a timeout, so it reaches window.onerror. */
  function failed(e) {
    setTimeout(function () { throw e; }, 0);
  }
  /* JScript 5.0 (IE 5.0) takes a number in Error(n), not a message: the message is set by hand (build.mjs refuses an
   * Error(message) here). */
  function error(message) {
    var e = new Error();
    e.message = e.description = message;
    return e;
  }
  if (typeof WebAssembly === "object") {
    cdom.start(url, mount, env, options).then(ready, failed);
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
  /* The boot runs before any polyfill, so it names nothing JScript 5.0 (IE 5.0) lacks: no push, no undefined. Without
   * getElementById it is the fifth tier (cdomBoot), never here. */
  if (!d.addEventListener && !d.attachEvent) lack[lack.length] = "addEventListener or attachEvent";
  if (lack.length) throw error("cdom: this browser cannot run cdom: it has no " + lack.join(", "));
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
    s.onerror = function () { throw error("cdom: could not load " + src); };
    s.src = src;
    head.appendChild(s);
  }
  // Both are fetched at once and may run in either order: the app file only defines window.cdomFactory,
  // so the app starts when the second of them has run.
  var left = 2;
  function loaded() {
    if (--left) return;
    var f = window.cdomFactory;
    window.cdomFactory = null;
    cdom.start(f, mount, env, options).then(ready, failed);
  }
  var base = typeof url === "string" ? url : url[0];
  load(old ? "cdom.legacy-ie6.js" : words ? "cdom.legacy-ie9.js" : "cdom.legacy.js", loaded);
  load(base + (old || words ? ".ie9.js" : ".js"), loaded);
}
