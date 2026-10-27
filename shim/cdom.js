"use strict";
// cdom shim: applies the C op stream to the DOM and posts events back. Fixed per app.
var cdom = (() => {
  const EVENTS = ["click", "dblclick", "input", "change", "keydown", "keyup", "focusin",
    "focusout", "submit", "pointerdown", "pointerup", "pointermove"];
  const TRANSIENT = 0x40000000, DETACHED = 0xffffffff, SMALL = 64;
  const doc = document, td = new TextDecoder(), te = new TextEncoder();
  const nodes = [], strs = [], tmp = [], tpl = [], stack = [], on = [];
  let x, mem, buf, u8, u32, i32, OPS, ARGS, SB, SM, INBOX;
  let sp = 0, last = null, root = null, base = DETACHED, ref = 0;

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

  function listen(type) {
    on[type] = 1;
    const bit = 1 << type, top = nodes[0];
    top.addEventListener(EVENTS[type], (e) => {
      for (let n = e.target; n && n !== top; n = n.parentNode) {
        if (!(n.$l & bit)) continue;
        if (buf !== mem.buffer) views();
        const k = INBOX >> 2;
        i32[k] = type; i32[k + 1] = n.$i; i32[k + 2] = e.clientX | 0; i32[k + 3] = e.clientY | 0;
        i32[k + 4] = e.button | 0; i32[k + 5] = e.keyCode | 0;
        i32[k + 6] = e.shiftKey | (e.ctrlKey << 1) | (e.altKey << 2) | (e.metaKey << 3);
        x.cd_event();
        return;
      }
    });
  }

  function apply(nops, nargs, nstr, nbytes) {
    if (buf !== mem.buffer) views();
    if (nstr) decode(nstr, nbytes);
    let q = ARGS >> 2, n, el;
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
        case 9: n = u32[q++]; nodes[n] = last; last.$i = n; n = u32[q++]; last.$l |= 1 << n; if (!on[n]) listen(n); break; // LISTEN
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
        case 21: n = u32[q++]; nodes[n].remove(); nodes[n] = undefined; break; // REMOVE
        case 22: nodes[u32[q++]] = undefined; break; // FREE
        case 23: el = nodes[u32[q++]]; n = u32[q++]; el.parentNode.insertBefore(el, n ? nodes[n] : null); break; // MOVE
        case 24: nodes[u32[q++]].textContent = ""; break; // CLEAR
        default: throw new Error("cdom: bad op " + u8[p] + " at " + (p - OPS));
      }
    }
    tmp.length = 0;
  }

  function value(id, dst, cap) {
    const r = te.encodeInto(String(nodes[id].value ?? ""), new Uint8Array(mem.buffer, dst, cap));
    return r.written;
  }

  async function start(url, mount) {
    nodes[0] = mount;
    const imports = { cdom: { apply, value } };
    const res = fetch(url);
    const { instance } = WebAssembly.instantiateStreaming
      ? await WebAssembly.instantiateStreaming(res, imports)
      : await WebAssembly.instantiate(await (await res).arrayBuffer(), imports);
    x = instance.exports; mem = x.memory; views();
    const L = x.cd_layout() >> 2;
    [OPS, ARGS, SB, SM, INBOX] = [u32[L], u32[L + 1], u32[L + 2], u32[L + 3], u32[L + 4]];
    x.cd_start((Math.random() * 4294967296) >>> 0);
    return x;
  }
  return { start };
})();
