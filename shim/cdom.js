"use strict";
// cdom shim: C calls one of these functions per DOM operation, and the shim posts events back.
// Fixed per app. Wasm passes i32, so ids and numbers that may pass 2^31 are made unsigned (>>> 0).
var cdom = (() => {
  const DETACHED = -1, SMALL = 64; // CD_DETACHED as wasm passes it
  const doc = document, td = new TextDecoder(), te = new TextEncoder();
  const nodes = [], strs = [], tpl = [], stack = [], on = {}, queue = [];
  let x, mem, buf, u8, u32, i32, SB, SM, INBOX, PEND, TRAIL, page, loading, wasm;
  let sp = 0, last = null, root = null, base = DETACHED, ref = 0;

  function views() {
    buf = mem.buffer;
    u8 = new Uint8Array(buf); u32 = new Uint32Array(buf); i32 = new Int32Array(buf);
  }
  // Strings arrive together, right before the call that first needs one: n strings in nbytes,
  // with (id, bytes, UTF-16 length) each. One TextDecoder call per batch, and a char loop for a
  // tiny batch.
  function strings(n, nbytes) {
    if (buf !== mem.buffer) views();
    let m = SM >> 2, o = 0;
    if (nbytes > SMALL) {
      const all = td.decode(u8.subarray(SB, SB + nbytes));
      for (let i = 0; i < n; i++, m += 3) { const len = u32[m + 2]; strs[u32[m]] = all.substring(o, o + len); o += len; }
      return;
    }
    for (let i = 0, p = SB; i < n; i++, m += 3) {
      const nb = u32[m + 1];
      let s;
      if (nb === u32[m + 2]) { s = ""; for (let k = 0; k < nb; k++) s += String.fromCharCode(u8[p + k]); }
      else s = td.decode(u8.subarray(p, p + nb));
      strs[u32[m]] = s; p += nb;
    }
  }

  function place(n) {
    if (sp) stack[sp - 1].appendChild(n);
    else if (base !== DETACHED) nodes[base].insertBefore(n, ref ? nodes[ref] : null);
  }

  // The server's comments (cdom.h, resuming): <!----> keeps two texts apart and is passed over;
  // <!--e--> stands for an empty text node, which HTML cannot carry, and becomes one when a walk
  // first reaches it: the one node resuming creates. C resolves a server node by walking to it.
  function skip(c) {
    while (c && c.nodeType === 8) {
      if (c.data !== "e") c = c.nextSibling;
      else { const t = doc.createTextNode(""); c.replaceWith(t); c = t; }
    }
    return c;
  }
  // One listener per event type or name at the mount: C installs it on the first listen of it
  // (kind: CD_EV_*, CD_EV_CUSTOM for a name; capture for names, so events that do not bubble
  // arrive too), or resume does, for the types the server's render listened to. C calls
  // delegate once per type (its own check saves a call per listen); hear's check is for the
  // types resume put on the mount before C ran.
  function hear(t, capture) { if (!(t in on)) { on[t] = 0; nodes[0].addEventListener(t, (e) => handle(t, e), capture); } }
  function delegate(kind, h, capture) { hear(strs[h], capture); on[strs[h]] = [kind, h]; }
  // Walking up from the target, the shim writes each level's index among its siblings (as a walk
  // counts them, <!----> left out) and its element index to TRAIL, up to the nearest node with an
  // id that C says takes the event, or up to the mount while server nodes that listen are still
  // unresolved (u32[PEND]); C picks the node and makes the record the app sees (cdom.h). Before
  // the module is there, events wait in a queue (64 at most), and the first one starts it.
  function handle(t, e) {
    if (!x) { if (queue.push([t, e]) > 64) queue.shift(); boot(); return; }
    const o = on[t], top = nodes[0];
    if (!o) return;
    if (buf !== mem.buffer) views();
    let depth = 0, hit = -1, hd = 0;
    for (let n = e.target; n !== top; n = n.parentNode, depth++) {
      if (!n) return;
      const i = n.$i;
      if (hit < 0 && i !== undefined && nodes[i] === n && x.cd_wants(i, o[0], o[1])) { hit = i; hd = depth; if (!u32[PEND]) break; }
      let k = 0, m = 0;
      for (let s = n.previousSibling; s; s = s.previousSibling) { if (s.nodeType === 1) m++; if (s.nodeType !== 8 || s.data === "e") k++; }
      if (depth < 512) { u32[TRAIL + 2 * depth] = k; u32[TRAIL + 2 * depth + 1] = m; }
    }
    post(o[0], hit, e, o[0] < 12 ? e.keyCode | 0 : o[1], hd, depth);
  }
  function post(type, id, e, key, path, depth) {
    if (buf !== mem.buffer) views();
    const k = INBOX >> 2;
    i32[k] = type; i32[k + 1] = id; i32[k + 2] = e.clientX | 0; i32[k + 3] = e.clientY | 0;
    i32[k + 4] = e.button | 0; i32[k + 5] = key;
    i32[k + 6] = e.shiftKey | (e.ctrlKey << 1) | (e.altKey << 2) | (e.metaKey << 3);
    i32[k + 7] = path; i32[k + 8] = depth; // the trail form: path is the levels below id, depth the levels written
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
      case 6: return strs[u32[vq++]];
      case 7: { const n = u32[vq++], a = new Float64Array(n); new Uint32Array(a.buffer).set(u32.subarray(vq, vq += 2 * n)); return a; }
      case 8: { const a = []; for (let n = u32[vq++]; n; n--) a.push(val()); return a; }
      case 9: { const o = {}; for (let n = u32[vq++]; n; n--) { const k = strs[u32[vq++]]; o[k] = val(); } return o; }
    }
    throw new Error("cdom: bad value tag " + t);
  }

  // HOST (cdom.h): MOUNT, CALL, SET, PROP, UNMOUNT on the widget instance el.$h; the value is
  // the cdv at address v (its nwords are not needed: the tree delimits itself).
  function host(id, kind, h, v) {
    if (buf !== mem.buffer) views();
    vq = v >>> 2; v = val();
    const el = nodes[id], name = strs[h];
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

  function at(p, b) { base = p; ref = b; }
  function open(t) { const el = doc.createElement(strs[t]); if (sp) stack[sp - 1].appendChild(el); stack[sp++] = el; last = el; }
  function close() { const el = stack[--sp]; if (!sp) { root = el; place(el); } }
  function text(s) { last = doc.createTextNode(strs[s]); place(last); }
  function text_num(n) { last = doc.createTextNode(n >>> 0); place(last); }
  function attr(a, v) { last.setAttribute(strs[a], strs[v]); }
  function cls(v) { last.className = strs[v]; }
  function name(n) { nodes[n] = last; }
  function listen(n) { nodes[n] = last; last.$i = n; }
  function value(s) { last.nodeValue = strs[s]; }
  function value_num(n) { last.nodeValue = n >>> 0; }
  function tpl_save(t) { tpl[t] = root; }
  function clone(t) { const el = tpl[t].cloneNode(true); if (sp) stack[sp - 1].appendChild(el); stack[sp++] = el; last = el; }
  // A walk: n steps of 2 bits (1 firstChild, 2 nextSibling, 3 parentNode) from a named node, or
  // from the last node when from is -1. A resumed page's comments are read as skip() reads them;
  // a page that never resumed has none, so it skips the per-step nodeType read (1.3 us per update click).
  function walk(from, steps, n) {
    let el = from === -1 ? last : nodes[from];
    for (; n; n--, steps >>>= 2) {
      el = (steps & 3) === 1 ? el.firstChild : (steps & 3) === 2 ? el.nextSibling : el.parentNode;
      if (page && el && el.nodeType === 8) el = skip(el);
    }
    last = el;
  }
  function walk_value(from, steps, n, s) { walk(from, steps, n); last.nodeValue = strs[s]; }
  function walk_value_num(from, steps, n, v) { walk(from, steps, n); last.nodeValue = v >>> 0; }
  function set_text(id, s) { nodes[id].nodeValue = strs[s]; }
  function set_text_num(id, n) { nodes[id].nodeValue = n >>> 0; }
  function set_attr(id, a, v) { nodes[id].setAttribute(strs[a], strs[v]); }
  function set_class(id, v) { nodes[id].className = strs[v]; }
  function remove(id) { nodes[id].remove(); nodes[id] = undefined; }
  function free(id) { nodes[id] = undefined; }
  function move(id, b) { const el = nodes[id]; el.parentNode.insertBefore(el, b ? nodes[b] : null); }
  function clear(id) { nodes[id].textContent = ""; }
  function detach(id) { const el = nodes[id]; el.$p = el.parentNode; el.$s = el.nextSibling; el.remove(); }
  function attach(id) { const el = nodes[id]; el.$p.insertBefore(el, el.$s); el.$p = el.$s = null; }

  // CDOM_DEV builds only (cdom.h): a misuse the generated code found at run time; a release module never imports it.
  function dev_warn(p, n) { if (buf !== mem.buffer) views(); console.warn("cdom: " + td.decode(u8.subarray(p, p + n))); }
  function input(id, dst, cap) {
    const r = te.encodeInto(String(nodes[id].value ?? ""), new Uint8Array(mem.buffer, dst, cap));
    return r.written;
  }

  function load(url, env) {
    return WebAssembly.instantiateStreaming(fetch(url), { cdom: { strings, at, open, close, text, text_num, attr, class: cls,
      name, listen, value, value_num, tpl_save, clone, walk, walk_value, walk_value_num, set_text, set_text_num, set_attr,
      set_class, remove, free, move, clear, detach, attach, input, host, delegate, dev_warn }, env: env || {} });
  }
  function init(instance) {
    x = instance.exports; mem = x.memory; views();
    const L = x.cd_layout() >> 2;
    [SB, SM, INBOX, PEND, TRAIL] = [u32[L], u32[L + 1], u32[L + 2], u32[L + 3] >> 2, u32[L + 4] >> 2];
  }
  // mount is an element, or the id of one that may not be parsed yet: then the wasm loads
  // while the parser and the stylesheets run, and the app starts once the DOM is ready.
  async function start(url, mount, env) {
    const { instance } = await load(url, env);
    if (typeof mount === "string") {
      if (doc.readyState === "loading") await new Promise((r) => doc.addEventListener("DOMContentLoaded", r, { once: true }));
      mount = doc.getElementById(mount);
    }
    nodes[0] = mount; init(instance);
    x.cd_start((Math.random() * 4294967296) >>> 0); return x;
  }
  // A server-rendered page (cdom.h, resuming): the listeners for the event types its render
  // listened to go on the mount now, and nothing else runs. The module is fetched after the load
  // event; on the first event (or boot()) C decodes the page's base64 state (the element stateId
  // holds it) and renders the first frame virtually over the server's DOM; then the queued events
  // are delivered.
  function resume(url, mountId, stateId, types, names) {
    page = [url, stateId]; nodes[0] = doc.getElementById(mountId);
    for (const t of types) hear(t, false);
    for (const t of names) hear(t, true);
    addEventListener("load", prefetch);
  }
  // After the load event the module is fetched, compiled and instantiated, and none of it runs:
  // the first event then pays only the virtual render. An event before that starts it at once.
  function prefetch() { return wasm || (wasm = load(page[0])); }
  function boot() {
    return loading || (loading = (async () => {
      api.loaded = true;
      const { instance } = await prefetch();
      init(instance);
      const b64 = doc.getElementById(page[1]).textContent, dst = x.cd_state_buf(b64.length);
      views(); te.encodeInto(b64, u8.subarray(dst, dst + b64.length)); // base64 is ASCII: C decodes it
      if (x.cd_start_state(b64.length)) throw new Error("cdom: the state blob did not load");
      for (const [t, e] of queue.splice(0)) handle(t, e);
      return x;
    })());
  }
  const api = { start, resume, boot, loaded: false };
  return api;
})();
