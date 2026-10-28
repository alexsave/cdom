"use strict";
// Experiment (branch direct-calls), arm (b): the shim for CDOM_NATIVE builds. Wasm calls the
// browser's DOM methods directly (Function.prototype.call.bind), holding nodes and strings in
// externref tables; this file only decodes strings into the wasm string table, installs the
// delegated listeners and reads input values.
var cdom = (() => {
  const EVENTS = ["click", "dblclick", "input", "change", "keydown", "keyup", "focusin",
    "focusout", "submit", "pointerdown", "pointerup", "pointermove"];
  const SMALL = 64, doc = document, td = new TextDecoder(), te = new TextEncoder();
  const call = Function.prototype.call, P = (C, k) => Object.getOwnPropertyDescriptor(C.prototype, k);
  let x, mem, u8, u32, i32, SB, SM, INBOX, top;

  function views() { u8 = new Uint8Array(mem.buffer); u32 = new Uint32Array(mem.buffer); i32 = new Int32Array(mem.buffer); }
  function decode(n, nbytes) {
    let m = SM >> 2, o = 0;
    if (nbytes > SMALL) {
      const all = td.decode(u8.subarray(SB, SB + nbytes));
      for (let i = 0; i < n; i++, m += 3) { const len = u32[m + 2]; x.cd_str_put(u32[m], all.substring(o, o + len)); o += len; }
      return;
    }
    for (let i = 0, p = SB; i < n; i++, m += 3) {
      const nb = u32[m + 1];
      let s;
      if (nb === u32[m + 2]) { s = ""; for (let k = 0; k < nb; k++) s += String.fromCharCode(u8[p + k]); }
      else s = td.decode(u8.subarray(p, p + nb));
      x.cd_str_put(u32[m], s); p += nb;
    }
  }
  function apply(nops, nargs, nstr, nbytes) { // strings only: ops are calls
    if (u8.buffer !== mem.buffer) views();
    if (nstr) decode(nstr, nbytes);
  }
  function listen(type) {
    const bit = 1 << type;
    top.addEventListener(EVENTS[type], (e) => {
      let path = 0, depth = 0;
      const lm = new Uint16Array(mem.buffer, x.cd_lmask());
      for (let n = e.target; n && n !== top; n = n.parentNode) {
        if (n.$i !== undefined && lm[n.$i] & bit) {
          if (u8.buffer !== mem.buffer) views();
          const k = INBOX >> 2;
          i32[k] = type; i32[k + 1] = n.$i; i32[k + 2] = e.clientX | 0; i32[k + 3] = e.clientY | 0;
          i32[k + 4] = e.button | 0; i32[k + 5] = e.keyCode | 0;
          i32[k + 6] = e.shiftKey | (e.ctrlKey << 1) | (e.altKey << 2) | (e.metaKey << 3);
          i32[k + 7] = path; i32[k + 8] = depth;
          x.cd_event();
          return;
        }
        let i = 0;
        for (let s = n.previousElementSibling; s; s = s.previousElementSibling) i++;
        path = (path << 8) | (i & 255); depth++;
      }
    });
  }
  function value(id, dst, cap) {
    return te.encodeInto(String(x.cd_node(id).value ?? ""), new Uint8Array(mem.buffer, dst, cap)).written;
  }
  const n = {
    createElement: call.bind(Document.prototype.createElement),
    createTextNode: call.bind(Document.prototype.createTextNode),
    appendChild: call.bind(Node.prototype.appendChild),
    insertBefore: call.bind(Node.prototype.insertBefore),
    setAttribute: call.bind(Element.prototype.setAttribute),
    className: call.bind(P(Element, "className").set),
    nodeValue: call.bind(P(Node, "nodeValue").set),
    textContent: call.bind(P(Node, "textContent").set),
    cloneNode: call.bind(Node.prototype.cloneNode),
    firstChild: call.bind(P(Node, "firstChild").get),
    nextSibling: call.bind(P(Node, "nextSibling").get),
    parentNode: call.bind(P(Node, "parentNode").get),
    remove: call.bind(Element.prototype.remove),
    set: Reflect.set,
    listen,
  };
  async function start(url, mount, env) {
    const imports = { cdom: { apply, value }, n, env: env || {} };
    const res = fetch(url);
    const { instance } = await WebAssembly.instantiateStreaming(res, imports);
    if (typeof mount === "string") {
      if (doc.readyState === "loading") await new Promise((r) => doc.addEventListener("DOMContentLoaded", r, { once: true }));
      mount = doc.getElementById(mount);
    }
    top = mount;
    x = instance.exports; mem = x.memory; views();
    const L = x.cd_layout() >> 2;
    [SB, SM, INBOX] = [u32[L + 2], u32[L + 3], u32[L + 4]];
    x.cd_init(doc, mount, "$i", "");
    x.cd_start((Math.random() * 4294967296) >>> 0);
    return x;
  }
  return { start };
})();
