"use strict";
// cdom shim: C calls one of these functions per DOM operation, and the shim posts events back.
// Fixed per app. Wasm passes i32, so ids and numbers that may pass 2^31 are made unsigned (>>> 0).
var cdom = (() => {
  const DETACHED = 0xffffffff, SMALL = 64;
  const doc = document, td = new TextDecoder(), te = new TextEncoder();
  const nodes = [], strs = [], tpl = [], stack = [];
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
  const S = (a) => strs[a];

  // Strings arrive together: one TextDecoder call per batch, and a char loop for tiny batches.
  function decode(n, nbytes) {
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

  // ---- hydration: the first render adopts the server's DOM (each op below that creates,
  // places or writes starts with `if (hy)` and comes here) ----
  function bad(n, want) { throw new Error("cdom: hydration mismatch at " + (n ? n.nodeName : "the end") + ", want " + want); }
  function into() { const p = sp ? stack[sp - 1] : base === DETACHED ? null : nodes[base]; return p && p.isConnected ? p : null; }
  function cursor(p) { let c = hy.get(p); if (!c) hy.set(p, c = { n: p.firstChild, text: false }); return c; }
  // An element goes to the cursor's node, or, when it is inserted before a node (AT with a
  // before) into a parent that holds parked nodes, to the first parked one.
  function adopt(p, tag) {
    const c = cursor(p);
    let n = c.n;
    if (!sp && ref && c.q && c.q.length) {
      n = c.q.shift(); n.$x = 0;
      if (verify && !(n.compareDocumentPosition(nodes[ref]) & 4)) bad(n, tag + " before the insert's anchor");
    } else { c.n = n && n.nextSibling; c.text = false; }
    if (!n || n.nodeType !== 1 || (verify && n.localName !== tag.toLowerCase())) bad(n, tag);
    stats.adopted++; return n;
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
  function size(n) { let k = 1; if (!n.$h) for (let c = n.firstChild; c; c = c.nextSibling) k += size(c); return k; }
  // A clone adopted over the server's node s: template t's children are matched in order, the
  // <!----> separators dropped and the empty text nodes the server could not write created, as
  // at page level. A node the server marked <!--i--> was inserted into the clone later (an
  // {#if} branch before an anchor): it is parked on s, skipped by walks, and adopted by the
  // insert that places it. What follows the template's last child is left at s's cursor for
  // later appends. With verify, a template slot's text (a template text node with an id, $s)
  // must be the server's. Returns the number of server nodes matched below s.
  function fit(s, t, root) {
    let k = 0, c = s.firstChild, txt = false;
    for (let u = t.firstChild; u; u = u.nextSibling) {
      while (c && c.nodeType === 8 && c.data === "i") {
        const e = c.nextSibling, r = cursor(s); c.remove();
        if (!e) bad(e, "a node after <!--i-->");
        (r.q || (r.q = [])).push(e); e.$x = 1; c = e.nextSibling; txt = false;
      }
      if (u.nodeType === 3) {
        if (txt) { if (!c || c.nodeType !== 8) bad(c, "<!---->"); const m = c; c = c.nextSibling; m.remove(); }
        txt = true;
        const want = verify && u.$s ? u.nodeValue : null;
        if (c && c.nodeType === 3) { if (want !== null && c.nodeValue !== want) bad(c, want); k++; c = c.nextSibling; continue; }
        if (want) bad(c, want);
        const m = c && c.nodeType === 8 && c.data === "" && c.nextSibling; // <!----><!--i-->: empty, before a parked node
        if (m && m.nodeType === 8 && m.data === "i") { c.remove(); c = m; }
        s.insertBefore(doc.createTextNode(""), c); stats.created++;
      } else {
        txt = false;
        if (!c || c.nodeType !== 1 || (verify && c.localName !== u.localName)) bad(c, u.nodeName);
        k += 1 + fit(c, u, false); c = c.nextSibling;
      }
    }
    if (c || root || hy.has(s)) { const r = cursor(s); r.n = c; r.text = txt; }
    return k;
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
    if ((h = into())) { el = adopt(h, tpl[t].localName); stats.adopted += fit(el, tpl[t], true); }
    else { el = tpl[t].cloneNode(true); stats.templateNodes += tsize[t]; if (sp) stack[sp - 1].appendChild(el); }
    stack[sp++] = el; last = el;
  }
  // A walk skips the parked nodes (inserted into a clone later), as the template never had them.
  function hWalk(from, steps, n) {
    let el = from === -1 ? last : nodes[from];
    for (; n; n--, steps >>>= 2) {
      const k = steps & 3;
      el = k === 1 ? el.firstChild : k === 2 ? el.nextSibling : el.parentNode;
      if (k !== 3) while (el && el.$x) el = el.nextSibling;
    }
    last = el;
  }

  // One listener per event type or name at the mount, installed by C on the first listen of it
  // (kind: CD_EV_*, CD_EV_CUSTOM for a name; capture for names, so events that do not bubble
  // arrive too). It posts the nearest node with an id that C says takes the event, with the
  // element-child indices from that node down toward the target.
  function delegate(kind, h, capture) {
    const top = nodes[0];
    top.addEventListener(S(h), (e) => {
      let path = 0, depth = 0;
      for (let n = e.target; n && n !== top; n = n.parentNode) {
        const i = n.$i;
        if (i !== undefined && nodes[i] === n && x.cd_wants(i, kind, h)) return post(kind, i, e, kind < 12 ? e.keyCode | 0 : h, path, depth);
        let k = 0;
        for (let s = n.previousElementSibling; s; s = s.previousElementSibling) k++;
        path = (path << 8) | (k & 255); depth++;
      }
    }, capture);
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
  function at(p, b) { base = p >>> 0; ref = b; }
  function open(t) { if (hy) return hOpen(S(t)); const el = doc.createElement(S(t)); if (sp) stack[sp - 1].appendChild(el); stack[sp++] = el; last = el; }
  function close() { if (hy) return hClose(); const el = stack[--sp]; if (!sp) { root = el; place(el); } }
  function text(s) { if (hy) return hText(S(s)); last = doc.createTextNode(S(s)); place(last); }
  function text_num(n) { if (hy) return hText(n >>> 0); last = doc.createTextNode(n >>> 0); place(last); }
  function attr(a, v) { if (hy) return hAttr(S(a), S(v)); last.setAttribute(S(a), S(v)); }
  function cls(v) { if (hy) return hClass(S(v)); last.className = S(v); }
  function name(n) { nodes[n] = last; if (hy && !last.isConnected) last.$s = 1; }
  function listen(n) { nodes[n] = last; last.$i = n; }
  function value(s) { if (hy) return hValue(S(s)); last.nodeValue = S(s); }
  function value_num(n) { if (hy) return hValue(n >>> 0); last.nodeValue = n >>> 0; }
  function tpl_save(t) { tpl[t] = root; if (hy) tsize[t] = size(root); }
  function clone(t) {
    if (hy) return hClone(t);
    const el = tpl[t].cloneNode(true); if (sp) stack[sp - 1].appendChild(el); stack[sp++] = el; last = el;
  }
  // A walk: n steps of 2 bits (1 firstChild, 2 nextSibling, 3 parentNode) from a named node, or
  // from the last node when from is -1.
  function walk(from, steps, n) {
    if (hy) return hWalk(from, steps, n);
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
  function remove(id) { nodes[id].remove(); nodes[id] = undefined; }
  function free(id) { nodes[id] = undefined; }
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
      value_num, tpl_save, clone, walk, walk_value, walk_value_num, set_text,
      set_text_num, set_attr, set_class, remove, free, move, clear, detach, attach, input, host, delegate }, env: env || {} };
    const { instance } = await WebAssembly.instantiateStreaming(fetch(url), imports);
    if (typeof mount === "string") {
      if (doc.readyState === "loading") await new Promise((r) => doc.addEventListener("DOMContentLoaded", r, { once: true }));
      mount = doc.getElementById(mount);
    }
    nodes[0] = mount;
    x = instance.exports; mem = x.memory; views();
    const L = x.cd_layout() >> 2;
    [SB, SM, INBOX] = [u32[L], u32[L + 1], u32[L + 2]];
    if (!opts || !opts.hydrate) { x.cd_start((Math.random() * 4294967296) >>> 0); api.stats = stats; return x; }
    const b64 = doc.getElementById(opts.hydrate).textContent, dst = x.cd_state_buf(b64.length);
    views(); te.encodeInto(b64, u8.subarray(dst, dst + b64.length)); // base64 is ASCII: C decodes it
    hy = new Map(); verify = !!opts.verify;
    try {
      if (x.cd_start_state(b64.length)) throw new Error("cdom: the state blob did not load");
      if (verify && size(mount) - 1 !== stats.adopted + stats.created) bad(mount, "every server node adopted");
    } finally { hy = null; }
    api.stats = stats; // set once started: {created, adopted, templateNodes} of the hydration
    return x;
  }
  const api = { start, stats: null };
  return api;
})();
