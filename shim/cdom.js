"use strict";
// cdom shim: C calls one of these functions per DOM operation, and the shim posts events back.
// Fixed per app. Wasm passes i32, so ids and numbers that may pass 2^31 are made unsigned (>>> 0).
var cdom = (() => {
  const EVENTS = ["click", "dblclick", "input", "change", "keydown", "keyup", "focusin",
    "focusout", "submit", "pointerdown", "pointerup", "pointermove"];
  const TRANSIENT = 0x40000000, DETACHED = 0xffffffff, SMALL = 64;
  const doc = document, td = new TextDecoder(), te = new TextEncoder();
  const nodes = [], strs = [], tmp = [], tpl = [], stack = [], on = [], tslot = [], named = new Set();
  let slots = []; // slot text nodes of the template being built
  let lmask = new Uint16Array(1024); // listened event types per node id
  let x, mem, buf, u8, u32, i32, SB, SM, INBOX;
  let sp = 0, last = null, root = null, base = DETACHED, ref = 0;
  // Hydration (start's opts.hydrate): an op that would create a node under a parent in the
  // document adopts the server's node there instead. hy: parent -> {n: next child, text}.
  let hy = null, verify = false;
  const stats = { created: 0, adopted: 0, templateNodes: 0 }, tsize = [];

  function views() {
    buf = mem.buffer;
    u8 = new Uint8Array(buf); u32 = new Uint32Array(buf); i32 = new Int32Array(buf);
  }
  const S = (a) => (a < TRANSIENT ? strs[a] : tmp[a - TRANSIENT]);
  function put(dst, s) { if (dst < TRANSIENT) strs[dst] = s; else tmp[dst - TRANSIENT] = s; }

  // Strings arrive together: one TextDecoder call per batch, and a char loop for tiny batches.
  function decode(n, nbytes) {
    let m = SM >> 2, o = 0;
    if (nbytes > SMALL) {
      const all = td.decode(u8.subarray(SB, SB + nbytes));
      for (let i = 0; i < n; i++, m += 3) { const len = u32[m + 2]; put(u32[m], all.substring(o, o + len)); o += len; }
      return;
    }
    for (let i = 0, p = SB; i < n; i++, m += 3) {
      const nb = u32[m + 1];
      let s;
      if (nb === u32[m + 2]) { s = ""; for (let k = 0; k < nb; k++) s += String.fromCharCode(u8[p + k]); }
      else s = td.decode(u8.subarray(p, p + nb));
      put(u32[m], s); p += nb;
    }
  }

  function place(n) {
    if (sp) stack[sp - 1].appendChild(n);
    else if (base !== DETACHED) nodes[base].insertBefore(n, ref ? nodes[ref] : null);
  }

  // ---- hydration: the first render adopts the server's DOM (each op below that creates,
  // places or writes starts with `if (hy)` and comes here) ----
  function bad(n, want) { throw new Error("cdom: hydration mismatch at " + (n ? n.nodeName : "the end") + ", want " + want); }
  function into() { const p = sp ? stack[sp - 1] : base === DETACHED ? null : nodes[base]; return p && p.isConnected ? p : null; }
  function cursor(p) { let c = hy.get(p); if (!c) hy.set(p, c = { n: p.firstChild, text: false }); return c; }
  function adopt(p, tag) {
    const c = cursor(p), n = c.n;
    if (!n || n.nodeType !== 1 || (verify && n.localName !== tag.toLowerCase())) bad(n, tag);
    c.n = n.nextSibling; c.text = false; stats.adopted++; return n;
  }
  function adoptText(p, s) {
    const c = cursor(p);
    if (c.text) { const m = c.n; if (!m || m.nodeType !== 8) bad(m, "<!---->"); c.n = m.nextSibling; m.remove(); }
    c.text = true;
    const n = c.n;
    if (n && n.nodeType === 3) { if (verify && n.nodeValue !== String(s)) bad(n, s); c.n = n.nextSibling; stats.adopted++; return n; }
    if (verify && String(s) !== "") bad(n, s); // an empty text node writes as nothing: create it
    stats.created++; return p.insertBefore(doc.createTextNode(s), n);
  }
  function size(n) { let k = 1; for (let c = n.firstChild; c; c = c.nextSibling) k += size(c); return k; }
  function same(a, b) {
    if (a.nodeType !== b.nodeType || a.nodeName !== b.nodeName || a.childNodes.length !== b.childNodes.length) bad(a, b.nodeName);
    for (let i = 0; i < a.childNodes.length; i++) same(a.childNodes[i], b.childNodes[i]);
  }
  function check(ok, n, want) { if (verify && !ok) bad(n, want); }
  function hOpen(s) {
    let el, h;
    if ((h = into())) el = adopt(h, s);
    else { el = doc.createElement(s); stats.templateNodes++; if (sp) stack[sp - 1].appendChild(el); }
    stack[sp++] = el; last = el;
  }
  function hClose() { const el = stack[--sp]; if (!sp) { root = el; if (!el.isConnected) place(el); } }
  function hText(s) {
    const h = into();
    if (h) last = adoptText(h, s);
    else { last = doc.createTextNode(s); stats.templateNodes++; place(last); }
  }
  // ATTR, CLASS and VALUE on an adopted node: the server wrote them already.
  function hAttr(a, v) { if (last.isConnected) check(last.getAttribute(a) === v, last, a + "=" + v); else last.setAttribute(a, v); }
  function hClass(v) { if (last.isConnected) check(last.className === v, last, v); else last.className = v; }
  function hValue(s) { if (last.isConnected) check(last.nodeType !== 3 || last.nodeValue === String(s), last, s); else last.nodeValue = s; }
  function hClone(t) {
    let el, h;
    if (!sp && (h = into())) { el = adopt(h, tpl[t].localName); stats.adopted += tsize[t] - 1; if (verify) same(el, tpl[t]); }
    else { el = tpl[t].cloneNode(true); stats.templateNodes += tsize[t]; if (sp) stack[sp - 1].appendChild(el); }
    stack[sp++] = el; last = el;
  }

  function delegate(type) {
    on[type] = 1;
    const bit = 1 << type, top = nodes[0];
    top.addEventListener(EVENTS[type], (e) => {
      let path = 0, depth = 0;
      for (let n = e.target; n && n !== top; n = n.parentNode) {
        if (n.$i !== undefined && lmask[n.$i] & bit) return post(type, n.$i, e, e.keyCode | 0, path, depth);
        let i = 0;
        for (let s = n.previousElementSibling; s; s = s.previousElementSibling) i++;
        path = (path << 8) | (i & 255); depth++;
      }
    });
  }
  // listen_named: events by name (a web component's), captured at the mount so ones that do
  // not bubble arrive too; delivered as CD_EV_CUSTOM (12) with key = the name's string id.
  function delegateNamed(h) {
    named.add(h);
    const top = nodes[0];
    top.addEventListener(S(h), (e) => {
      let path = 0, depth = 0;
      for (let n = e.target; n && n !== top; n = n.parentNode) {
        if (n.$n && n.$n.has(h) && nodes[n.$i] === n) return post(12, n.$i, e, h, path, depth);
        let i = 0;
        for (let s = n.previousElementSibling; s; s = s.previousElementSibling) i++;
        path = (path << 8) | (i & 255); depth++;
      }
    }, true);
  }
  function post(type, id, e, key, path, depth) {
    if (buf !== mem.buffer) views();
    const k = INBOX >> 2;
    i32[k] = type; i32[k + 1] = id; i32[k + 2] = e.clientX | 0; i32[k + 3] = e.clientY | 0;
    i32[k + 4] = e.button | 0; i32[k + 5] = key;
    i32[k + 6] = e.shiftKey | (e.ctrlKey << 1) | (e.altKey << 2) | (e.metaKey << 3);
    i32[k + 7] = path; i32[k + 8] = depth;
    x.cd_event();
  }

  // cdv (cdom.h): one value from linear memory at word vq.
  const f64 = new Float64Array(1), w64 = new Uint32Array(f64.buffer);
  let vq = 0;
  function val() {
    const t = u32[vq++];
    switch (t) {
      case 0: return null;
      case 1: return false;
      case 2: return true;
      case 3: return u32[vq++];
      case 4: return i32[vq++];
      case 5: w64[0] = u32[vq++]; w64[1] = u32[vq++]; return f64[0];
      case 6: return S(u32[vq++]);
      case 7: { const n = u32[vq++], a = new Float64Array(n); new Uint32Array(a.buffer).set(u32.subarray(vq, vq += 2 * n)); return a; }
      case 8: { const a = []; for (let n = u32[vq++]; n; n--) a.push(val()); return a; }
      case 9: { const o = {}; for (let n = u32[vq++]; n; n--) { const k = S(u32[vq++]); o[k] = val(); } return o; }
    }
    throw new Error("cdom: bad value tag " + t);
  }

  // HOST (cdom.h): MOUNT, CALL, SET, PROP, UNMOUNT on the widget instance el.$h; the value is
  // the cdv at address v (its nwords are not needed: the tree delimits itself).
  function host(id, kind, h, v) {
    if (buf !== mem.buffer) views();
    vq = v >>> 2; v = val();
    const el = nodes[id], name = S(h);
    if (kind === 3) { el[name] = v; return; }
    const p = name.split(".");
    let o = kind ? el.$h : window;
    if (!kind) {
      for (const k of p) o = o == null ? o : o[k];
      if (typeof o !== "function") throw new Error("cdom: no widget constructor window." + name);
      el.$h = new o(el, v); return;
    }
    if (!o) throw new Error("cdom: no widget mounted for " + name);
    if (kind === 2) { for (let i = 0; i < p.length - 1; i++) o = o[p[i]]; o[p[p.length - 1]] = v; return; }
    o[name](...(kind === 1 ? v : []));
    if (kind === 4) delete el.$h;
  }

  function strings(n, nbytes) {
    if (buf !== mem.buffer) views();
    decode(n, nbytes);
  }
  function at(p, b) { base = p >>> 0; ref = b; if (hy && ref && into()) bad(null, "an append"); }
  function open(t) { if (hy) return hOpen(S(t)); const el = doc.createElement(S(t)); if (sp) stack[sp - 1].appendChild(el); stack[sp++] = el; last = el; }
  function close() { if (hy) return hClose(); const el = stack[--sp]; if (!sp) { root = el; place(el); } }
  function text(s) { if (hy) return hText(S(s)); last = doc.createTextNode(S(s)); place(last); }
  function text_num(n) { if (hy) return hText(n >>> 0); last = doc.createTextNode(n >>> 0); place(last); }
  function attr(a, v) { if (hy) return hAttr(S(a), S(v)); last.setAttribute(S(a), S(v)); }
  function cls(v) { if (hy) return hClass(S(v)); last.className = S(v); }
  function name(n) { nodes[n] = last; }
  function listen(n, ev) {
    nodes[n] = last; last.$i = n;
    if (n >= lmask.length) { const g = new Uint16Array(n * 2); g.set(lmask); lmask = g; }
    lmask[n] |= 1 << ev; if (!on[ev]) delegate(ev);
  }
  function listen_named(n, h) {
    nodes[n] = last; last.$i = n;
    (last.$n || (last.$n = new Set())).add(h); if (!named.has(h)) delegateNamed(h);
  }
  function value(s) { if (hy) return hValue(S(s)); last.nodeValue = S(s); }
  function value_num(n) { if (hy) return hValue(n >>> 0); last.nodeValue = n >>> 0; }
  function tpl_save(t) { tpl[t] = root; if (hy) tsize[t] = size(root); tslot[t] = slots; slots = []; root.remove(); }
  function clone(t) {
    if (hy) return hClone(t);
    const el = tpl[t].cloneNode(true); if (sp) stack[sp - 1].appendChild(el); stack[sp++] = el; last = el;
  }
  function slot(k) { slots[k] = last; }
  function tpl_text(t, k, s) { tslot[t][k].nodeValue = S(s); }
  function tpl_text_num(t, k, n) { tslot[t][k].nodeValue = n >>> 0; }
  // A walk: n steps of 2 bits (1 firstChild, 2 nextSibling, 3 parentNode) from a named node, or
  // from the last node when from is -1.
  function walk(from, steps, n) {
    let el = from === -1 ? last : nodes[from];
    for (; n; n--, steps >>>= 2) el = (steps & 3) === 1 ? el.firstChild : (steps & 3) === 2 ? el.nextSibling : el.parentNode;
    last = el;
  }
  function walk_value(from, steps, n, s) { walk(from, steps, n); if (hy) return hValue(S(s)); last.nodeValue = S(s); }
  function walk_value_num(from, steps, n, v) { walk(from, steps, n); if (hy) return hValue(v >>> 0); last.nodeValue = v >>> 0; }
  function set_text(id, s) { nodes[id].nodeValue = S(s); }
  function set_text_num(id, n) { nodes[id].nodeValue = n >>> 0; }
  function set_attr(id, a, v) { nodes[id].setAttribute(S(a), S(v)); }
  function set_class(id, v) { nodes[id].className = S(v); }
  function remove(id) { nodes[id].remove(); nodes[id] = undefined; lmask[id] = 0; }
  function free(id) { nodes[id] = undefined; lmask[id] = 0; }
  function move(id, b) { const el = nodes[id]; el.parentNode.insertBefore(el, b ? nodes[b] : null); }
  function clear(id) { nodes[id].textContent = ""; }
  function detach(id) { if (hy) return; const el = nodes[id]; el.$p = el.parentNode; el.$s = el.nextSibling; el.remove(); }
  function attach(id) { if (hy) return; const el = nodes[id]; el.$p.insertBefore(el, el.$s); el.$p = el.$s = null; }

  function input(id, dst, cap) {
    const r = te.encodeInto(String(nodes[id].value ?? ""), new Uint8Array(mem.buffer, dst, cap));
    return r.written;
  }

  // mount is an element, or the id of one that may not be parsed yet: then the wasm loads
  // while the parser and the stylesheets run, and the app starts once the DOM is ready.
  // opts.hydrate: the id of the page's base64 state script; the first render then adopts the
  // server's DOM under mount. opts.verify: also compare every tag, attribute and text.
  async function start(url, mount, env, opts) {
    const imports = { cdom: { strings, at, open, close, text, text_num, attr, class: cls, name, listen, value,
      value_num, tpl_save, clone, slot, tpl_text, tpl_text_num, walk, walk_value, walk_value_num, set_text,
      set_text_num, set_attr, set_class, remove, free, move, clear, detach, attach, input, host, listen_named }, env: env || {} };
    const res = fetch(url);
    const { instance } = WebAssembly.instantiateStreaming
      ? await WebAssembly.instantiateStreaming(res, imports)
      : await WebAssembly.instantiate(await (await res).arrayBuffer(), imports);
    if (typeof mount === "string") {
      if (doc.readyState === "loading") await new Promise((r) => doc.addEventListener("DOMContentLoaded", r, { once: true }));
      mount = doc.getElementById(mount);
    }
    nodes[0] = mount;
    x = instance.exports; mem = x.memory; views();
    const L = x.cd_layout() >> 2;
    [SB, SM, INBOX] = [u32[L], u32[L + 1], u32[L + 2]];
    if (!opts || !opts.hydrate) { x.cd_start((Math.random() * 4294967296) >>> 0); api.stats = stats; return x; }
    const b64 = doc.getElementById(opts.hydrate).textContent;
    const blob = Uint8Array.fromBase64 ? Uint8Array.fromBase64(b64) : Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const dst = x.cd_state_buf(blob.length);
    views(); u8.set(blob, dst);
    hy = new Map(); verify = !!opts.verify;
    try {
      if (x.cd_start_state(blob.length)) throw new Error("cdom: the state blob did not load");
      if (verify && size(mount) - 1 !== stats.adopted + stats.created) bad(mount, "every server node adopted");
    } finally { hy = null; }
    api.stats = stats; // set once started: {created, adopted, templateNodes} of the hydration
    return x;
  }
  const api = { start, stats: null };
  return api;
})();
