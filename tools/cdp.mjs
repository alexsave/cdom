// Minimal CDP client: one headless Chrome per process, killed by its own PID. CHROME names the
// binary (default: the macOS app's).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
export const sleep = ms => new Promise(r => setTimeout(r, ms));
// LEGACY=1 makes every page an old browser before its first script runs: no WebAssembly, and none of the
// APIs tools/legacy/polyfills.js supplies, so those polyfills and PEP are what the page runs on; crypto is
// only msCrypto, as in IE 11 (__noCrypto records that it was gone). Chrome still fires its own pointer
// events, so trusted ones are stopped at the window and only PEP's, made from mouse events, get through.
// addEventListener reads its third argument as useCapture, as before options objects; __adds logs what
// each call passed (type, typeof, capture) for tools/legacy.mjs.
// No IE has ChildNode.remove, so LEGACY=1 deletes it too, and the polyfill puts it back.
// LEGACY=ie9 is all of that and then IE 9: IE9_GAPS below deletes what IE 9 lacks that IE 11 has, and IE9_WALLS
// its typed arrays and msCrypto, so the boot takes the third tier (shim/boot.js).
// LEGACY=ie9-gaps is IE 9 with those two walls put back, so IE 9's other gaps are also tested on the second tier.
// LEGACY=ie6 is IE 9 and then what IE 6 lacks beyond it (IE6_OLD): ES5 and the W3C DOM of dom-old.js's table, with
// attachEvent in their place, so the boot takes the fourth tier.
// No IE has fetch or XMLHttpRequest.overrideMimeType (the page's own ?mime keeps it, for the browsers that do, Firefox 1.0 on),
// and the network of tools/legacy/polyfills.js stands on XMLHttpRequest: IE 9 and 10 have no responseType either (IE9_WALLS),
// and IE 5 and 6 have no XMLHttpRequest, only ActiveXObject("Microsoft.XMLHTTP") (IE6_OLD: a stand-in over the real one with
// what an ActiveX one has, no more: responseText and no responseType, overrideMimeType or addEventListener, and a status that
// throws where the request failed; __activeX counts the ones made).
// LEGACY=below is IE 6 without attachEvent either: below the floor, which the boot must refuse.
// LEGACY=frames is IE 6 and then no DOM mutation at all (FRAMES_OLD), in every frame: the fifth tier, where only
// document.write builds anything (docs/frames-design.md; tools/frames.mjs drives it).
export const LEVEL = process.env.LEGACY || '';
if (LEVEL && !['1', 'ie9', 'ie9-gaps', 'ie6', 'below', 'frames'].includes(LEVEL)) throw new Error(`cdp: LEGACY=${LEVEL}: want 1, ie9, ie9-gaps, ie6, below or frames`);
export const LEGACY = !!LEVEL;
// The boot's tier (shim/boot.js): 1 wasm, 2 wasm2js on typed arrays, 3 wasm2js on words, 4 that in ES3 on the old
// DOM, 5 the frames tier; 0 refused.
export const TIER = !LEVEL ? 1 : LEVEL === 'below' ? 0 : LEVEL === 'frames' ? 5 : LEVEL === 'ie6' ? 4 : LEVEL === 'ie9' ? 3 : 2;
export const IE9 = TIER === 3;
export const BELOW = TIER === 0; // below cdom's floor: the boot must refuse it
// IE 9's gaps above its walls (IE9_GAPS) are in the page: every IE level.
export const GAPS = LEVEL !== '' && LEVEL !== '1';
// The files of each tier, for a page whose wasm is app (a name without .wasm): only its own may be fetched.
// The files of each tier, for a page whose wasm is app (a name without .wasm): only its own may be fetched. wasm names
// what the first tier fetches, when the page starts a pair (APP_SIMD) and this Chrome takes the SIMD twin; the
// fallbacks take the scalar module's builds, and fetch no .wasm at all.
export const TIER_FILES = (app, wasm = `${app}.wasm`) => ({ 1: [wasm], 2: ['cdom.legacy.js', `${app}.wasm.js`], 3: ['cdom.legacy-ie9.js', `${app}.wasm.ie9.js`],
  4: ['cdom.legacy-ie6.js', `${app}.wasm.ie9.js`], 5: ['cdom.frames.js', `${app}.wasm.ie9.js`] });
// [pass, message] for the files a page fetched: all of its tier's, none of another's, and only the tier's own .wasm.
export function tierFetched(got, app, wasm) {
  const files = TIER_FILES(app, wasm), want = files[TIER] || [], other = Object.values(files).flat().filter((f) => !want.includes(f));
  const wasms = got.filter((f) => f.endsWith('.wasm'));
  return [want.every((f) => got.includes(f)) && !other.some((f) => got.includes(f)) && wasms.every((f) => want.includes(f)),
    `tier ${TIER} files only (${want.join(' ') || 'none'}): fetched ${got.filter((f) => /\.(js|wasm)$/.test(f)).join(' ')}`];
}
// What IE 9 lacks that IE 11 has, each with the IE version it arrived in, from mdn/browser-compat-data
// (main, read 2026-11-08); "never" is a feature BCD lists with no IE entry, which is how BCD records an IE
// that never had it. ES5 itself (Object.create, defineProperty, bind, forEach, getPrototypeOf) is IE 9's.
// Kept on purpose: performance (IE 9) and performance.getEntriesByType (IE 10), which the harness reads to
// see what a page fetched (FETCHED); the app never calls it.
const IE9_WALLS = `
  // Typed arrays, IE 10: ArrayBuffer, DataView and every view, Uint8ClampedArray included.
  for (const k of ['ArrayBuffer', 'DataView', 'Int8Array', 'Uint8Array', 'Uint8ClampedArray', 'Int16Array',
    'Uint16Array', 'Int32Array', 'Uint32Array', 'Float32Array', 'Float64Array', 'BigInt64Array', 'BigUint64Array',
    'Float16Array', 'SharedArrayBuffer']) delete window[k];
  // crypto.getRandomValues, IE 11 (as msCrypto): IE 9 and 10 have no cryptographic generator at all.
  delete window.msCrypto;
  // responseType and response, IE 10.
  delete XMLHttpRequest.prototype.responseType; delete XMLHttpRequest.prototype.response;`;
const IE9_GAPS = `
  // requestAnimationFrame and cancelAnimationFrame, IE 10 (and no prefixed ones to fall back on).
  for (const k of ['requestAnimationFrame', 'cancelAnimationFrame', 'webkitRequestAnimationFrame',
    'webkitCancelAnimationFrame', 'webkitCancelRequestAnimationFrame']) delete window[k];
  // MutationObserver, IE 11.
  delete window.MutationObserver; delete window.WebKitMutationObserver;
  // classList and DOMTokenList, IE 10; dataset (DOMStringMap), IE 11.
  delete Element.prototype.classList; delete window.DOMTokenList;
  delete HTMLElement.prototype.dataset; delete window.DOMStringMap;
  // history.pushState and replaceState, IE 10.
  delete History.prototype.pushState; delete History.prototype.replaceState;
  // WebSocket, Blob, File, FileReader, FormData, URL (with createObjectURL), IE 10; URLSearchParams, never.
  for (const k of ['WebSocket', 'Blob', 'File', 'FileReader', 'FormData', 'URL', 'URLSearchParams']) delete window[k];
  // indexedDB, atob and btoa, IE 10.
  for (const k of ['indexedDB', 'IDBFactory', 'IDBDatabase', 'IDBKeyRange', 'IDBRequest', 'IDBTransaction',
    'IDBObjectStore']) delete window[k];
  delete window.atob; delete window.btoa;
  // performance.now, IE 10 (performance itself is IE 9's).
  delete Performance.prototype.now;
  // Map, Set and WeakMap, IE 11; WeakSet and Symbol, never; Object.setPrototypeOf, IE 11.
  for (const k of ['Map', 'Set', 'WeakMap', 'WeakSet', 'Symbol']) delete window[k];
  delete Object.setPrototypeOf;
  // matchMedia, document.hidden and String.prototype.trim, IE 10; devicePixelRatio, IE 11.
  delete window.matchMedia; delete Document.prototype.hidden; delete String.prototype.trim;
  delete window.devicePixelRatio;
  // Element.matches, never: IE 9 to 11 have it only as msMatchesSelector.
  Element.prototype.msMatchesSelector = Element.prototype.matches; delete Element.prototype.matches;`;
// What IE 6 lacks beyond IE 9 (LEGACY=ie6 and below), from mdn/browser-compat-data: the ES5 that polyfills-es3.js
// supplies (Object.defineProperty and the rest of ES5's Object too, which nothing supplies), and the W3C DOM of
// tools/legacy/dom-old.js's table: addEventListener (attachEvent in its place, as IE's: window.event during the call,
// srcElement, returnValue and cancelBubble, IE's button bits, wheelDelta, and a field's input as propertychange on
// value), textContent, previousElementSibling, style.setProperty and getPropertyValue, createElementNS,
// elementsFromPoint, a rect's width and height, innerWidth and innerHeight; and a DOM method's apply and call; and
// JScript's splice, which removes nothing given a start alone. setAttribute sets an expando for class,
// style and for, as IE 6 and 7's sets a same-named property. Kept: JSON (IE 8's), getElementById, querySelector and
// innerText, which the harness reads the page with. Under LEGACY=below there is no attachEvent either. And a stand-in
// for VML (below).
const IE6_OLD = `
  const define = Object.defineProperty;
  // IE 6 to 8 refuse an input's type once the input has had a parent, even after it is taken out again: setAttribute
  // throws "This command is not supported." unless the type is the one it has (measured in IE 6, docs/ie6-design.md).
  // A lone input takes any type, one IE does not know (search, email, ...) being text, as IE's parser makes it; and
  // IE's mergeAttributes copies another element's attributes, the type among them, onto an input with a parent.
  // Before the loop below, which takes apply and call from the DOM's methods.
  // A mark on the node, not a WeakSet: IE9_GAPS took WeakSet.
  const HAD = '__ieHadParent';
  for (const k of ['appendChild', 'insertBefore', 'replaceChild']) {
    const f = Node.prototype[k];
    Node.prototype[k] = function (child) { if (child && !child[HAD]) define(child, HAD, { value: true }); return Reflect.apply(f, this, arguments); };
  }
  const nativeSetAttr = Element.prototype.setAttribute;
  const IE_TYPES = { __proto__: null, text: 1, password: 1, checkbox: 1, radio: 1, submit: 1, reset: 1, file: 1, hidden: 1, image: 1, button: 1 };
  Element.prototype.setAttribute = function (name, value) {
    if (this.nodeName !== 'INPUT' || String(name).toLowerCase() !== 'type') return Reflect.apply(nativeSetAttr, this, arguments);
    const v = String(value).toLowerCase();
    if (this[HAD] && v !== this.type) throw new Error('This command is not supported.');
    return Reflect.apply(nativeSetAttr, this, [name, IE_TYPES[v] ? v : 'text']);
  };
  Element.prototype.mergeAttributes = function (from) {
    for (const a of Array.prototype.slice.call(from.attributes)) Reflect.apply(nativeSetAttr, this, [a.name, a.value]);
  };
  // A VML stand-in (tools/legacy/vml.js, docs/vml-design.md): Chrome draws no VML, but IE's setup calls are here and
  // recorded in window.__vml (namespaces.add's arguments, createStyleSheet's rules), and every element made with a v: tag
  // is recorded too, so a test reads the VML the shim built. Chrome makes an unknown element of v:shape, which keeps the
  // properties set on it. Before the loop below, which takes apply and call from createElement as from the DOM's methods.
  // What real IE 6 did with VML made by script (docs/vml-design.md, section 9), each a way the stand-in fails:
  // - a v: node made before namespaces.add("v", URN) is an unknown element, not VML: listed in __vml.bad;
  // - namespaces.add with a third argument (an implementation) binds the behavior as the node is made, and every VML
  //   property set by script after that is lost (IE kept its defaults, coordsize 1000,1000 among them): the stand-in's
  //   nodes then keep those defaults whatever is set;
  // - a v: tag with no behavior rule for it (v\:* or v\:tag) draws nothing: listed in __vml.bad when it is made;
  // - a v:group is measured by what it holds, not by its width and height, and does not clip: listed in __vml.bad.
  const vml = window.__vml = { ns: [], rules: [], made: [], bad: [] };
  const URN = 'urn:schemas-microsoft-com:vml';
  const BS = String.fromCharCode(92);   // a backslash, written so in this template
  let bound = null;   // null, 'script' (the two-argument add) or 'implementation'
  Document.prototype.namespaces = { add(prefix, urn, behavior) {
    vml.ns.push([prefix, urn, behavior === undefined ? null : behavior]);
    if (prefix === 'v' && urn === URN) bound = behavior === undefined ? 'script' : 'implementation';
  } };
  Document.prototype.createStyleSheet = function () { return { addRule(selector, rule) { vml.rules.push([selector, rule]); } }; };
  const DEFAULTS = { path: '', coordsize: '1000,1000', coordorigin: '0,0', filled: true, fillcolor: 'white', stroked: true,
    strokecolor: 'black', strokeweight: '0.75pt', opacity: '1', joinstyle: 'round', miterlimit: '8', endcap: 'round' };
  const createElement = Document.prototype.createElement;
  Document.prototype.createElement = function (tag) {
    const el = Reflect.apply(createElement, this, [tag]);
    if (!/^v:/i.test(tag)) return el;
    const name = tag.slice(2).toLowerCase();
    vml.made.push(el);
    if (!bound) vml.bad.push('<' + tag + '> made before the v namespace was added: IE 6 makes an unknown element of it, not VML');
    if (!vml.rules.some(([s, r]) => (s === 'v' + BS + ':*' || s === 'v' + BS + ':' + name) && r.indexOf('behavior:url(#default#VML)') >= 0))
      vml.bad.push('<' + tag + '> has no behavior rule for it: IE 6 draws nothing of it');
    if (name === 'group') vml.bad.push('<v:group>: IE 6 measures it by what it holds, not by its width and height, and does not clip to it');
    if (bound === 'implementation')
      for (const [k, v] of Object.entries(DEFAULTS)) define(el, k, { get: () => v, set: () => {}, configurable: true });
    return el;
  };
  // IE 6 to 8's DOM methods are host objects, not functions: no apply or call (EventTarget's are kept: attachEvent
  // below calls them). Through Reflect: IE9_GAPS took Object.setPrototypeOf.
  for (const proto of [Node.prototype, Element.prototype, HTMLElement.prototype, Document.prototype, HTMLInputElement.prototype])
    for (const k of Object.getOwnPropertyNames(proto)) {
      const d = Object.getOwnPropertyDescriptor(proto, k);
      if (k !== 'constructor' && d && typeof d.value === 'function') Reflect.setPrototypeOf(d.value, null);
    }
  for (const k of ['create', 'keys', 'getPrototypeOf', 'defineProperty', 'defineProperties', 'getOwnPropertyNames',
    'getOwnPropertyDescriptor', 'freeze', 'seal', 'preventExtensions', 'isFrozen', 'isSealed', 'isExtensible']) delete Object[k];
  delete Array.isArray; delete Date.now; delete Function.prototype.bind; delete String.prototype.trim;
  for (const k of ['indexOf', 'lastIndexOf', 'forEach', 'map', 'filter', 'some', 'every', 'reduce', 'reduceRight']) delete Array.prototype[k];
  // JScript's splice(start) with no count removes nothing (the standard removes to the end).
  const splice = Array.prototype.splice;
  Array.prototype.splice = function (start) {
    return Reflect.apply(splice, this, arguments.length === 1 ? [start, 0] : arguments);
  };
  delete P.addEventListener; delete P.removeEventListener;
  delete Node.prototype.textContent; delete Element.prototype.previousElementSibling;
  delete CSSStyleDeclaration.prototype.setProperty; delete CSSStyleDeclaration.prototype.getPropertyValue;
  delete Document.prototype.createElementNS; delete Document.prototype.elementsFromPoint;
  for (const R of [DOMRectReadOnly.prototype, DOMRect.prototype]) { delete R.width; delete R.height; }
  delete window.innerWidth; delete window.innerHeight;
  // IE 5 and 6: no XMLHttpRequest (IE 7's), only ActiveX's, whose request is a stand-in over the real one with an ActiveX one's
  // surface: its status throws before the request is done or when it failed (WinInet's error), as IE's does.
  const Real = window.XMLHttpRequest, made = window.__activeX = [];
  if (!/xhr/.test(location.search)) { delete window.XMLHttpRequest; window.ActiveXObject = function (id) {
    if (id !== 'Microsoft.XMLHTTP') throw new Error("Automation server can't create object");
    const x = new Real(), self = this;
    made.push(this);
    for (const k of ['open', 'send', 'setRequestHeader', 'getResponseHeader', 'abort'])
      this[k] = function () { return x[k].apply(x, arguments); };
    define(this, 'readyState', { get: () => x.readyState });
    define(this, 'responseText', { get: () => x.responseText });
    define(this, 'statusText', { get: () => x.statusText });
    define(this, 'status', { get: () => { if (x.readyState !== 4 || !x.status) throw new Error('The system cannot locate the resource specified.'); return x.status; } });
    define(this, 'onreadystatechange', { set: (f) => { x.onreadystatechange = function () { f.call(self); }; } });
  }; }
  const setAttr = Element.prototype.setAttribute;
  Element.prototype.setAttribute = function (n, v) {
    if (/^(class|style|for)$/i.test(n)) { this['$ie' + n] = v; return; }
    return Reflect.apply(setAttr, this, [n, v]);
  };
  ${LEVEL === 'below' ? '' : `let current;
  define(window, 'event', { configurable: true, get: () => current });
  const IE_BIT = [1, 4, 2], REAL = { propertychange: 'input' }, attached = window.__attached = [];
  const ie = (e, type) => {
    const o = { type, srcElement: e.target, clientX: e.clientX, clientY: e.clientY, keyCode: e.keyCode,
      shiftKey: e.shiftKey, ctrlKey: e.ctrlKey, altKey: e.altKey, wheelDelta: e.wheelDelta,
      button: type === 'mousemove' ? e.buttons : type === 'mousedown' || type === 'mouseup' ? IE_BIT[e.button] : 0,
      propertyName: type === 'propertychange' ? 'value' : undefined };
    define(o, 'returnValue', { set(v) { if (v === false) e.preventDefault(); } });
    define(o, 'cancelBubble', { set(v) { if (v) e.stopPropagation(); } });
    return o;
  };
  P.attachEvent = function (on, f) {
    const type = on.slice(2), real = REAL[type] || type;
    const g = (e) => { const was = current; current = ie(e, type); try { f.call(window, current); } finally { current = was; } };
    attached.push([this, on, f, g, real]);
    add.call(this, real, g, false);
    return true;
  };
  P.detachEvent = function (on, f) {
    const i = attached.findIndex((r) => r[0] === this && r[1] === on && r[2] === f);
    if (i >= 0) { rem.call(this, attached[i][4], attached[i][3], false); attached.splice(i, 1); }
  };`}`;
// What the frames tier's browsers (Netscape 2 to 4, IE 3 and 4) lack beyond IE 6 (LEGACY=frames): any way to change a
// document after it has loaded, so only document.write and open builds one. Deleted in every frame (this runs in each
// on its creation): making nodes, putting them in and taking them out, markup set from script, attributes, and the
// listeners (attachEvent too, which IE6_OLD put in). Kept for the harness: querySelector, getBoundingClientRect and
// innerText's getter (its setter goes).
const FRAMES_OLD = `
  for (const k of ['createElement', 'createElementNS', 'createTextNode', 'createComment', 'createDocumentFragment',
    'getElementById', 'importNode', 'adoptNode', 'append', 'prepend', 'replaceChildren']) delete Document.prototype[k];
  for (const k of ['appendChild', 'insertBefore', 'removeChild', 'replaceChild']) delete Node.prototype[k];
  for (const k of ['innerHTML', 'outerHTML', 'insertAdjacentHTML', 'insertAdjacentElement', 'insertAdjacentText', 'append',
    'prepend', 'before', 'after', 'replaceWith', 'replaceChildren', 'setAttribute', 'removeAttribute', 'setAttributeNS',
    'toggleAttribute', 'attachShadow', 'setHTMLUnsafe']) delete Element.prototype[k];
  delete ShadowRoot.prototype.innerHTML;
  for (const k of ['before', 'after', 'replaceWith']) delete CharacterData.prototype[k];
  // Two flavours, by the top page's query string, for the caret: ?ie4, IE 4's fields (no selectionStart, End or
  // setSelectionRange, but createTextRange and document.selection.createRange, TextRanges of character moves), and
  // ?ns4, Netscape 4's (neither). Both keep select() and focus(), which both browsers have.
  const caretQ = (() => { try { return window.top.location.search; } catch { return ''; } })();
  const caretFlavour = /[?&]ie4(&|$)/.test(caretQ) ? 'ie4' : /[?&]ns4(&|$)/.test(caretQ) ? 'ns4' : '';
  if (caretFlavour) {
    const real = []; // [input's, textarea's]: no Map, and Reflect.apply, not call (IE 6's profile took both)
    for (const C of [HTMLInputElement, HTMLTextAreaElement]) {
      const p = C.prototype, d = (k) => Reflect.getOwnPropertyDescriptor(p, k);
      real.push({ start: d('selectionStart').get, end: d('selectionEnd').get, set: p.setSelectionRange });
      for (const k of ['selectionStart', 'selectionEnd', 'selectionDirection', 'setSelectionRange', 'setRangeText']) delete p[k];
    }
    const kind = (el) => el instanceof HTMLInputElement ? real[0] : el instanceof HTMLTextAreaElement ? real[1] : null;
    if (caretFlavour === 'ie4') {
      // A TextRange over a field: characters s to e of its value.
      class TextRange {
        constructor(el, s, e) { this.el = el; this.s = s; this.e = e; }
        get text() { return this.el.value.substring(this.s, this.e); }
        duplicate() { return new TextRange(this.el, this.s, this.e); }
        collapse(start) { if (start === false) this.s = this.e; else this.e = this.s; }
        moveStart(unit, n) { this.s = Math.max(0, Math.min(this.el.value.length, this.s + n)); if (this.s > this.e) this.e = this.s; return n; }
        moveEnd(unit, n) { this.e = Math.max(0, Math.min(this.el.value.length, this.e + n)); if (this.e < this.s) this.s = this.e; return n; }
        select() { const k = kind(this.el); if (k) Reflect.apply(k.set, this.el, [this.s, this.e]); }
      }
      for (const C of [HTMLInputElement, HTMLTextAreaElement]) C.prototype.createTextRange = function () { return new TextRange(this, 0, this.value.length); };
      Reflect.defineProperty(Document.prototype, 'selection', { configurable: true, get() {
        const doc = this;
        return { createRange() {
          const a = doc.activeElement, k = a && kind(a);
          return k ? new TextRange(a, Reflect.apply(k.start, a, []), Reflect.apply(k.end, a, [])) : new TextRange({ value: '' }, 0, 0);
        } };
      } });
    }
  }
  const inner = Reflect.getOwnPropertyDescriptor(HTMLElement.prototype, 'innerText');
  Reflect.defineProperty(HTMLElement.prototype, 'innerText', { configurable: true, get: inner.get });
  delete P.attachEvent; delete P.detachEvent;`;
const OLD = `(() => {
  for (const k of ['WebAssembly', 'TextDecoder', 'TextEncoder', 'queueMicrotask', 'reportError', 'Promise', 'fetch',
    'PointerEvent', 'globalThis', 'AbortController', 'AbortSignal']) delete window[k];
  window.msCrypto = window.crypto; delete window.crypto; // an own accessor on the window in Chrome
  window.__noCrypto = typeof crypto === 'undefined';
  for (const k of ['imul', 'clz32', 'fround', 'trunc']) delete Math[k];
  delete Object.entries; delete String.prototype.startsWith; delete String.prototype.includes;
  delete Element.prototype.remove; delete CharacterData.prototype.remove; delete DocumentType.prototype.remove;
  const ta = Object.getPrototypeOf(Uint8Array.prototype); delete ta.copyWithin; delete ta.fill;
  for (const t of ['pointerdown', 'pointerup', 'pointermove', 'pointerover', 'pointerout', 'pointercancel',
    'pointerenter', 'pointerleave', 'gotpointercapture', 'lostpointercapture'])
    window.addEventListener(t, (e) => { if (e.isTrusted) e.stopImmediatePropagation(); }, true);
  const P = EventTarget.prototype, add = P.addEventListener, rem = P.removeEventListener;
  window.__mime = /mime/.test(location.search) && typeof XMLHttpRequest.prototype.overrideMimeType === 'function';
  if (!/mime/.test(location.search)) delete XMLHttpRequest.prototype.overrideMimeType;
  const adds = window.__adds = [];
  P.addEventListener = function (t, f, c) { adds.push([t, typeof c, !!c]); return add.call(this, t, f, !!c); };
  P.removeEventListener = function (t, f, c) { return rem.call(this, t, f, !!c); };${GAPS ? IE9_GAPS : ''}${TIER >= 3 || BELOW ? IE9_WALLS : ''}${TIER >= 4 || BELOW ? IE6_OLD : ''}${TIER === 5 ? FRAMES_OLD : ''}
})();`;
// The scripts a page fetched, by file name, to prove which path it took.
// A plain loop: under LEGACY=below no polyfill puts Array.prototype.map back.
export const FETCHED = "(function () { var e = performance.getEntriesByType('resource'), o = [], i; for (i = 0; i < e.length; i++) o.push(e[i].name.split('/').pop().split('?')[0]); return o; })()";

// Every Chrome this process started and that has not exited. Node does not take its children with it,
// so a run that ended without close() (a throw, a process.exit on a failure, a SIGTERM from a harness's
// timeout, a Ctrl-C) left its headless Chrome running, orphaned, with its profile in the temp dir. Now
// they are killed, and their profiles removed, when this process exits, however it exits short of a
// SIGKILL (whose senders kill the process group: tools/mutate.py). tools/cdpself.mjs checks each way.
const live = new Map(); // proc -> profile dir
const killAll = () => {
  for (const [p, dir] of live) {
    try { p.kill('SIGKILL'); } catch {}
    try { fs.rmSync(dir, { recursive: true, force: true }); } catch {}
  }
  live.clear();
};
let hooked = false;
const hook = () => {
  if (hooked) return;
  hooked = true;
  process.on('exit', killAll); // a normal end, process.exit and an uncaught throw
  for (const [sig, n] of [['SIGINT', 2], ['SIGTERM', 15], ['SIGHUP', 1]])
    process.on(sig, () => { killAll(); process.exit(128 + n); }); // a signal ends the process without 'exit'
};

export async function launch(args = []) { // args: Chrome flags of the caller's own
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-chrome-'));
  // Port 0: the OS gives this Chrome a free port, which it writes to DevToolsActivePort in its own
  // profile, so a run can only ever reach the Chrome it started. A picked port (a range, a guess) lets two
  // parallel runs collide: the second Chrome fails to bind, and its run drives the first one's browser,
  // flags and all (a unit run then saw no window.gc). CDP_PORT still fixes one, for a debugger.
  const want = +(process.env.CDP_PORT || 0);
  const errFile = process.env.CHROME_LOG ? process.env.CHROME_LOG + '.err' : path.join(dir, 'chrome.err');
  // --use-mock-keychain and --password-store=basic: under a temporary HOME, Chrome would ask macOS for a keychain in a dialog.
  const proc = spawn(process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    ['--headless=new', `--remote-debugging-port=${want}`, `--user-data-dir=${dir}`, '--no-first-run', '--use-mock-keychain', '--password-store=basic',
     '--no-default-browser-check', '--disable-extensions', '--mute-audio', '--window-size=1280,800', ...args, ...(process.env.CHROME_ARGS ? process.env.CHROME_ARGS.split(' ') : []), 'about:blank'],
    // Chrome's stderr goes to the profile by default (CHROME_LOG names another place), so a launch that
    // fails can say why: the error below quotes its tail.
    { stdio: ['ignore', process.env.CHROME_LOG ? fs.openSync(process.env.CHROME_LOG, 'w') : 'ignore', fs.openSync(errFile, 'w')] });
  let exited = false, spawnErr = '';
  live.set(proc, dir);
  hook();
  proc.on('exit', () => { exited = true; live.delete(proc); });
  proc.on('error', (e) => { exited = true; spawnErr = e.message; }); // no such binary: say so, don't crash the run
  const portFile = path.join(dir, 'DevToolsActivePort');
  // One deadline for the whole start, not a loop count: a cold CI runner's first Chrome (a new profile,
  // the shader and font caches) can take well over 10 s. CDP_START_MS overrides it. Chrome exiting
  // stops the wait at once.
  const deadline = performance.now() + +(process.env.CDP_START_MS || 60000);
  const why = () => { if (spawnErr) return spawnErr; try { return fs.readFileSync(errFile, 'utf8').trim().split('\n').slice(-8).join('\n'); } catch { return ''; } };
  let dbg = 0;
  while (!dbg && !exited && performance.now() < deadline) {
    try { dbg = +fs.readFileSync(portFile, 'utf8').split('\n')[0]; } catch { await sleep(100); }
  }
  const fail = (msg) => { proc.kill('SIGKILL'); return new Error(msg); }; // a launch that fails leaves no Chrome
  if (!dbg) throw fail(`cdp: Chrome ${exited ? 'exited' : 'wrote no DevToolsActivePort'} (profile ${dir})\n${why()}`);
  let ver;
  while (!ver && !exited && performance.now() < deadline) {
    try { ver = await (await fetch(`http://127.0.0.1:${dbg}/json/version`)).json(); } catch { await sleep(200); }
  }
  if (!ver) throw fail(`cdp: no answer from Chrome on port ${dbg}${exited ? ' (it exited)' : ''}\n${why()}`);
  const ws = new WebSocket(ver.webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = new Map();
  const api = {};
  // exceptions: every uncaught page exception since the last navigation, as "file:line message".
  const exceptions = [];
  const thrown = (d) => {
    if (d.method === 'Page.frameNavigated' && !d.params.frame.parentId) exceptions.length = 0;
    if (d.method !== 'Runtime.exceptionThrown') return;
    const x = d.params.exceptionDetails;
    exceptions.push(`${(x.url || '').split('/').pop()}:${x.lineNumber + 1} ${x.exception?.description?.split('\n')[0] || x.text}`);
  };
  ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); } else if (d.method) { thrown(d); if (api.onEvent) api.onEvent(d); } };
  const send = (method, params = {}, sessionId) => new Promise(r => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params, sessionId })); });
  const { result: { targetId } } = await send('Target.createTarget', { url: 'about:blank', newWindow: true });
  const { result: { sessionId } } = await send('Target.attachToTarget', { targetId, flatten: true });
  const s = (m, p) => send(m, p, sessionId);
  await s('Page.enable'); await s('Runtime.enable');
  // The page behaves as focused whatever the OS window does: with several Chromes at once (tools/mutate.py -j N)
  // a window can lose focus, and the browser then fires no focus or blur events (a removed input's focusout).
  await s('Emulation.setFocusEmulationEnabled', { enabled: true });
  if (LEGACY) await s('Page.addScriptToEvaluateOnNewDocument', { source: OLD });
  const evaluate = async (expr) => {
    const r = await s('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.result?.exceptionDetails) throw new Error(JSON.stringify(r.result.exceptionDetails));
    return r.result?.result?.value;
  };
  const close = async () => {
    try { await send('Target.closeTarget', { targetId }); ws.close(); } catch {}
    proc.kill('SIGTERM');
    await sleep(300);
    try { fs.rmSync(dir, { recursive: true, force: true }); } catch {}
  };
  return Object.assign(api, { pid: proc.pid, version: ver.Browser, send: s, sendBrowser: send, targetId, evaluate, close, exceptions });
}

// Under LEGACY=below the boot must refuse the browser cleanly (see shim/boot.js): one error naming each missing
// thing, neither fallback's files nor the wasm fetched, and nothing drawn in mount (a selector). Returns the
// checks as [pass, message] pairs. The shim the page inlines fails first in Chrome, on TextDecoder; a real
// old browser fails that script with a SyntaxError instead, LEGACY=1 sees the same one, and it is not counted.
export const FLOOR = ['addEventListener or attachEvent'];
export const REFUSAL = `cdom: this browser cannot run cdom: it has no ${FLOOR.join(', ')}`;
export async function refused(b, mount) {
  for (let i = 0; i < 100 && (await b.evaluate('document.readyState')) !== 'complete'; i++) await sleep(50);
  await sleep(500); // a fallback file still loading would have run by now
  const errs = b.exceptions.filter(e => !/^[\w.-]+\.html:\d+ ReferenceError: TextDecoder is not defined$/.test(e));
  const got = (await b.evaluate(FETCHED)).filter(f => /\.(js|wasm)$/.test(f) && f !== 'cdom.js');
  return [
    [errs.length === 1 && errs[0].endsWith(`Error: ${REFUSAL}`), `one error, the boot's, naming all ${FLOOR.length} missing: ${JSON.stringify(errs)}`],
    [!got.length, `no fallback file or wasm fetched: ${got.join(' ')}`],
    [await b.evaluate(`document.querySelector(${JSON.stringify(mount)}).childNodes.length === 0`), `nothing drawn in ${mount}`],
  ];
}
