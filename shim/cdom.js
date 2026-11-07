"use strict";
// cdom shim: applies the C op stream to the DOM and posts events back. Fixed per app.
var cdom = (() => {
  const EVENTS = ["click", "dblclick", "input", "change", "keydown", "keyup", "focusin",
    "focusout", "submit", "pointerdown", "pointerup", "pointermove", "contextmenu", "wheel",
    "pointerover", "pointerout", "pointercancel", "dragover", "drop", "focus", "blur", "scroll", "load",
    "error", "loadedmetadata", "timeupdate", "ended", "play", "pause", "resize", "fullscreenchange",
    "visibilitychange"];
  const CAPTURE = 0x1ff80000; // focus to pause do not bubble: the mount hears them in the capture phase
  const TRANSIENT = 0x40000000, DETACHED = 0xffffffff, SMALL = 64, EVENT = 4, RESULT = 5;
  const doc = document, td = new TextDecoder(), te = new TextEncoder();
  const nodes = [], strs = [], tmp = [], tpl = [], stack = [], on = [], segs = [], queue = [];
  const T64 = new Float64Array(1), T32 = new Uint32Array(T64.buffer);
  let lmask = new Uint32Array(1024); // listened event types per node id
  let x, mem, buf, u8, u32, i32, f32, OPS, ARGS, SB, SM, INBOX;
  let sp = 0, last = null, root = null, base = DETACHED, ref = 0, raf = 0, aq = 0, busy = 0;

  function views() {
    buf = mem.buffer;
    u8 = new Uint8Array(buf); u32 = new Uint32Array(buf); i32 = new Int32Array(buf); f32 = new Float32Array(buf);
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

  // Fills the inbox and runs the app's handler. An event dispatched while C runs (focus() in a batch) waits
  // for a microtask, so it never lands in the middle of a batch or overwrites the inbox.
  function deliver(type, id, e, path, depth) {
    if (busy) { if (queue.push([type, id, e, path, depth]) === 1) queueMicrotask(drain); return; }
    busy = 1;
    if (buf !== mem.buffer) views();
    const k = INBOX >> 2;
    i32[k] = type; i32[k + 1] = id; i32[k + 2] = e.clientX | 0; i32[k + 3] = e.clientY | 0;
    i32[k + 4] = e.button | 0; i32[k + 5] = e.keyCode | 0;
    i32[k + 6] = e.shiftKey | (e.ctrlKey << 1) | (e.altKey << 2) | (e.metaKey << 3);
    i32[k + 7] = path; i32[k + 8] = depth; i32[k + 9] = e.pointerId | 0; i32[k + 10] = e.buttons | 0;
    f32[k + 11] = e.deltaX || 0; f32[k + 12] = e.deltaY || 0;
    nodes[EVENT] = e;
    const f = x.cd_event();
    nodes[EVENT] = undefined; busy = 0;
    if (f & 1) e.preventDefault();
    if (f & 2) e.stopPropagation();
  }
  function drain() { busy = 0; for (const a of queue.splice(0)) deliver(...a); }

  // n is the mount (delegated to the nearest listening node) or a reserved id (window, document, body).
  function listen(type, n) {
    on[type + 32 * n] = 1;
    if (n) return nodes[n].addEventListener(EVENTS[type], (e) => deliver(type, n, e, 0, 0), { passive: false });
    const bit = 1 << type, top = nodes[0];
    top.addEventListener(EVENTS[type], (e) => {
      let path = 0, depth = 0;
      for (let n = e.target; n && n !== top; n = n.parentNode) {
        if (n.$i !== undefined && lmask[n.$i] & bit) { deliver(type, n.$i, e, path, depth); return; }
        let i = 0;
        for (let s = n.previousElementSibling; s; s = s.previousElementSibling) i++;
        path = (path << 8) | (i & 255); depth++;
      }
    }, { capture: (CAPTURE >> type & 1) === 1, passive: false });
  }

  // Host access along dotted paths; a "--" segment is a custom property of the style object before it.
  const K = (p) => (segs[p] ??= p ? S(p).split(".") : []);
  function walk(o, k, n) {
    for (let i = 0; i < n && o != null; i++) o = k[i].startsWith("--") ? o.getPropertyValue(k[i]) : o[k[i]];
    return o;
  }
  function val() { // one tagged value at u32[aq], as put_vals in cdom.c writes it
    switch (u32[aq++]) {
      case 0: T32[0] = u32[aq++]; T32[1] = u32[aq++]; return T64[0];
      case 1: return S(u32[aq++]);
      case 2: return u32[aq++] === 1;
      case 3: return nodes[u32[aq++]];
      case 5: { const o = {}; for (let c = u32[aq++]; c--;) o[val()] = val(); return o; }
      default: return null;
    }
  }
  // SET (a null) or CALLA: the last segment on the object before it. A failure is reported, not thrown, so
  // the rest of the batch still applies.
  function host(el, p, v, a) {
    try {
      const k = K(p), o = walk(el, k, k.length - 1), s = k[k.length - 1];
      if (a) (nodes[RESULT] = o[s](...a))?.catch?.(() => {});
      else if (s.startsWith("--")) o.setProperty(s, v); else o[s] = v;
    } catch (err) { if (a) nodes[RESULT] = undefined; reportError(err); }
  }

  function tick(t) { raf = requestAnimationFrame(tick); x.cd_frame(t); }

  function apply(nops, nargs, nstr, nbytes) {
    const was = busy; busy = 1;
    if (buf !== mem.buffer) views();
    if (nstr) decode(nstr, nbytes);
    let q = ARGS >> 2, n, el, w;
    for (let p = OPS, e = OPS + nops; p < e; p++) {
      switch (u8[p]) {
        case 1: base = u32[q++]; ref = u32[q++]; break; // AT
        case 2: el = doc.createElement(S(u32[q++])); if (sp) stack[sp - 1].appendChild(el); stack[sp++] = el; last = el; break; // OPEN
        case 3: el = stack[--sp]; if (!sp) { root = el; place(el); } break; // CLOSE
        case 4: last = doc.createTextNode(S(u32[q++])); place(last); break; // TEXT
        case 5: last = doc.createTextNode(u32[q++]); place(last); break; // TEXT_NUM
        case 6: last.setAttribute(S(u32[q++]), S(u32[q++])); break; // ATTR
        case 7: last.className = S(u32[q++]); break; // CLASS
        case 8: nodes[u32[q++]] = last; break; // NAME
        case 9: // LISTEN
          n = u32[q++]; w = u32[q++];
          if (n && n < EVENT) { if (!on[w + 32 * n]) listen(w, n); break; }
          nodes[n] = last; last.$i = n;
          if (n >= lmask.length) { const g = new Uint32Array(n * 2); g.set(lmask); lmask = g; }
          lmask[n] |= 1 << w; if (!on[w]) listen(w, 0); break;
        case 10: last.nodeValue = S(u32[q++]); break; // VALUE
        case 11: last.nodeValue = u32[q++]; break; // VALUE_NUM
        case 12: tpl[u32[q++]] = root; root.remove(); break; // TPL_SAVE
        case 13: el = tpl[u32[q++]].cloneNode(true); stack[sp++] = el; last = el; break; // CLONE
        case 14: last = last.firstChild; break; // DOWN
        case 15: last = last.nextSibling; break; // NEXT
        case 16: last = last.parentNode; break; // UP
        case 17: nodes[u32[q++]].nodeValue = S(u32[q++]); break; // SET_TEXT
        case 18: nodes[u32[q++]].nodeValue = u32[q++]; break; // SET_TEXT_NUM
        case 19: nodes[u32[q++]].setAttribute(S(u32[q++]), S(u32[q++])); break; // SET_ATTR
        case 20: nodes[u32[q++]].className = S(u32[q++]); break; // SET_CLASS
        case 21: n = u32[q++]; nodes[n].remove(); nodes[n] = undefined; lmask[n] = 0; break; // REMOVE
        case 22: n = u32[q++]; nodes[n] = undefined; lmask[n] = 0; break; // FREE
        case 23: el = nodes[u32[q++]]; n = u32[q++]; el.parentNode.insertBefore(el, n ? nodes[n] : null); break; // MOVE
        case 24: nodes[u32[q++]].textContent = ""; break; // CLEAR
        case 25: el = nodes[u32[q++]]; el.$p = el.parentNode; el.$s = el.nextSibling; el.remove(); break; // DETACH
        case 26: el = nodes[u32[q++]]; el.$p.insertBefore(el, el.$s); el.$p = el.$s = null; break; // ATTACH
        case 27: last = nodes[u32[q++]]; break; // SEL
        case 28: el = nodes[u32[q++]]; n = u32[q++]; w = u32[q++]; // BLIT
          (el.$g ??= el.getContext("2d")).putImageData(new ImageData(new Uint8ClampedArray(buf, n, w * u32[q] * 4), w, u32[q++]), 0, 0); break;
        case 29: if (u32[q++]) raf ||= requestAnimationFrame(tick); else { cancelAnimationFrame(raf); raf = 0; } break; // FRAMES
        case 30: el = nodes[u32[q++]]; el[S(u32[q++])]()?.catch?.(() => {}); break; // CALL
        case 31: el = nodes[u32[q++]]; n = u32[q++]; aq = q; w = val(); q = aq; host(el, n, w, null); break; // SET
        case 32: el = nodes[u32[q++]]; n = u32[q++]; aq = q + 1; w = []; // CALLA
          for (let c = u32[q]; c--;) w.push(val());
          q = aq; host(el, n, null, w); break;
        default: throw new Error("cdom: bad op " + u8[p] + " at " + (p - OPS));
      }
    }
    tmp.length = 0; busy = was;
  }

  // The import behind cd_get_*: kind 0 number, 1 string into dst, 2 node id, 3 rect into dst. Never throws.
  function get(id, p, kind, dst, cap) {
    let v;
    try { const k = K(p); v = walk(nodes[id], k, k.length); } catch {}
    if (kind === 0) return typeof v === "boolean" ? +v : typeof v === "number" ? v : NaN;
    if (kind === 1) return te.encodeInto(v == null ? "" : String(v), new Uint8Array(mem.buffer, dst, cap)).written;
    if (kind === 2) { const i = v == null ? -1 : nodes.indexOf(v); return i === RESULT ? nodes.indexOf(v, RESULT + 1) : i; }
    const r = v?.getBoundingClientRect?.();
    if (!r) return 0;
    new Float64Array(mem.buffer, dst, 4).set([r.left, r.top, r.width, r.height]); return 1;
  }

  async function start(url, mount, env) {
    const imports = { cdom: { apply, get }, env: env || {} };
    const res = fetch(url);
    const { instance } = WebAssembly.instantiateStreaming
      ? await WebAssembly.instantiateStreaming(res, imports)
      : await WebAssembly.instantiate(await (await res).arrayBuffer(), imports);
    x = instance.exports; mem = x.memory; views();
    nodes[0] = mount; nodes[1] = window; nodes[2] = doc; nodes[3] = doc.body;
    const L = x.cd_layout() >> 2;
    [OPS, ARGS, SB, SM, INBOX] = [u32[L], u32[L + 1], u32[L + 2], u32[L + 3], u32[L + 4]];
    x.cd_start((Math.random() * 4294967296) >>> 0);
    return x;
  }
  return { start, node: (id) => nodes[id] };
})();
