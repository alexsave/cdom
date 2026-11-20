// cdom shim: C calls one of these functions per DOM operation, and the shim posts events back.
// Fixed per app. Wasm passes i32, so ids and numbers that may pass 2^31 are made unsigned (>>> 0).
// Written to be read: the page ships it minified (terser), so names and lines cost nothing there.
// Strict mode is declared inside the closure, so it holds when the legacy build puts polyfills in front.
var cdom = (() => {
  "use strict";
  const EVENTS = ["click", "dblclick", "input", "change", "keydown", "keyup", "focusin",
    "focusout", "submit", "pointerdown", "pointerup", "pointermove", "resize", "visibilitychange",
    "mousedown", "mousemove", "mouseup", "touchstart", "touchmove", "touchend", "touchcancel",
    "contextmenu", "wheel", "pointerover", "pointerout", "pointercancel", "dragover", "drop",
    "focus", "blur", "scroll", "load", "error", "loadedmetadata", "timeupdate", "ended", "play", "pause",
    "fullscreenchange", "cancel", "close"];
  const RESIZE = 12;            // EVENTS index of the window's resize
  const VISIBILITY = 13;        // and of visibilitychange
  const WHEEL = 22;
  const FOCUS = 28;             // FOCUS to PAUSE do not bubble: the mount hears them in the capture phase
  const PAUSE = 37;
  const CANCEL = 39;            // a <dialog>'s cancel and close do not bubble either
  const CLOSE = 40;
  const POINTER_TYPES = [9, 10, 11, 22, 23, 24, 25]; // the types whose record has pointerId and buttons
  const fillsPointer = [];      // by type: 1 for those
  for (const type of POINTER_TYPES) {
    fillsPointer[type] = 1;
  }
  const WINDOW = 1;             // the reserved ids (cdom.h): host objects bound at start
  const DOCUMENT = 2;
  const BODY = 3;
  const EVENT = 4;              // the event being delivered
  const RESULT = 5;             // what the last call returned
  const DETACHED = 0xffffffff;  // "no insertion parent" for at()
  const ASCII_MAX = 32;         // a runtime string up to this many bytes is tested for ASCII and read without TextDecoder
  const SVG = "http://www.w3.org/2000/svg";
  // The CSS units, in the order of cdom.h's CD_UNIT_* and cdom.c's UNIT_STR (tests/unit.c checks all three).
  const UNITS = ["", "px", "%", "deg", "em", "rem", "vw", "vh", "ms", "s", "rad"];
  const doc = document;
  const textDecoder = new TextDecoder();
  const textEncoder = new TextEncoder();
  const nodes = [];         // node by id
  const templates = [];     // template root by template id
  const stack = [];         // the open elements
  const delegated = [];     // event types with a listener on the mount
  const delegatedListener = []; // and that listener, by type
  const templateSlots = []; // slot text nodes by template id
  let slots = [];           // slot text nodes of the template being built
  const hostListeners = [];  // the listener per (host object, event type), while listened
  let listenMask = newWords(2048); // listened event types per node id: two words, a bit per type
  const queue = [];         // events the browser dispatched while C was running, delivered after
  let busy = 0;             // how many calls into C are on the stack
  let exports;              // the wasm module's exports
  let memory;
  let aheadText = null;     // the cd_tmp_ahead range decoded as one string, while a range is alive
  let aheadAt = 0;          // its first byte's address
  let aheadEnd = 0;         // and the address just past its last byte
  let aheadIndex = null;    // the UTF-16 offset of each of its bytes, or null when it is all ASCII
  let checkAhead = false;   // options.checkAhead: every read from the range is compared with the bytes
  let lits;                 // the page's string table (cdomLits), one entry per literal slot
  let litBase;              // the address of slot 0: a literal's id is litBase + 4 * its index
  let litEnd;               // the address just past the last slot (0 when the page has no slots)
  // Strings of read-only data by address (cd_tmpz): the module's const bytes never change, so one address is
  // one string for the life of the page. A 256-entry direct-mapped cache sits in front of the table; the
  // table is a Map, or a plain object on the ES5 tier, which has no Map.
  const roKey = newWords(256);
  const roVal = [];
  const roMap = typeof Map === "function" ? new Map() : null;
  const roObj = roMap ? null : {};
  let onRoRead = null;      // options.onRoRead: a test hook, called with an address each time one is read from memory
  let buffer;               // memory.buffer when the views below were made (compared on the wasm2js tier only)
  let copiedOnGrow = false; // the module is wasm2js, whose grow leaves the old buffer attached
  let u8;
  let u32;
  let i32;
  let f32;
  let f64;
  // Where C's records are and how they are laid out, read once at start from cd_layout (src/cdom.c,
  // CD_LAYOUT, filled with offsetof and sizeof): the shim hardcodes no offset into C memory
  // (tools/offsets.mjs). Field offsets are in words (4 bytes), as u32, i32 and f32 index them.
  let tmpSerial0;           // C's runtime-string block (cd_tmp): the serial of its first record, as an index into u32
  let tmpCount;             // how many records are alive
  let tmpRecords;           // where the records start, as a byte address
  let recWords;             // a record's size
  let recP;                 // and its fields: the string's address and its length in bytes
  let recLen;
  let evType;               // the event record's fields, as indexes into i32 and f32 (the record stays put)
  let evId;
  let evX;
  let evY;
  let evButton;
  let evKey;
  let evMods;
  let evPath;
  let evDepth;
  let evCh;
  let evMs;
  let evPointer;
  let evButtons;
  let evDx;
  let evDy;
  let viewportW;            // cd_viewport_rec's fields
  let viewportH;
  let viewportHidden;
  let viewportDpr;
  let valWords;             // a cd_val's size
  let valTag;               // and its fields; num is a double, so it is read through f64 at half the word index
  let valNum;
  let valU;
  let depth = 0;            // how many elements are open
  let svgDepth = 0;         // how many of them are the <svg> or inside it
  let last = null;          // the node made or walked to last
  let lastElement = null;   // the element opened or cloned last (cd_name_el)
  let lastText = null;      // the text node made last (cd_name_text)
  let root = null;          // the outermost element closed last
  let base = DETACHED;      // the parent new top-level nodes go into
  let before = 0;           // the node they go before (0: at the end)

  // C's memory. Every read and write of it goes through the functions from here to "end of C's memory", over
  // typed-array views. The build for a browser without typed arrays (cdom.legacy-ie9.js, tools/legacy/build.mjs)
  // puts tools/legacy/memory-words.js's functions of the same names in their place, over app.wasm.ie9.js's words,
  // and refuses to write the file if a typed array or a view is left anywhere else.
  // Typed views over wasm memory, made again after it grows.
  function views() {
    buffer = memory.buffer;
    u8 = new Uint8Array(buffer);
    u32 = new Uint32Array(buffer);
    i32 = new Int32Array(buffer);
    f32 = new Float32Array(buffer);
    f64 = new Float64Array(buffer);
  }
  // True when the views are of a memory that has grown. WebAssembly detaches the old buffer on a grow,
  // so a stale view has length 0 and the test reads no memory.buffer (a getter that costs a call).
  // wasm2js has no detaching: it copies into a bigger ArrayBuffer and the old one keeps its length, so
  // that tier compares the buffer, as the shim always did.
  function stale() {
    return u8.length === 0 || (copiedOnGrow && buffer !== memory.buffer);
  }
  // A view of length bytes of C memory at ptr, good until the memory grows.
  function bytes(ptr, length) {
    return new Uint8Array(memory.buffer, ptr, length);
  }
  // A copy of them, which outlives the call and any growth.
  function copyBytes(ptr, length) {
    return bytes(ptr, length).slice();
  }
  // The same bytes through the views, which the caller has made fresh (stale, views): no memory.buffer read.
  function viewBytes(ptr, length) {
    return u8.subarray(ptr, ptr + length);
  }
  // A byte at ptr; a word, unsigned, at word index; a word stored, an f32 stored at word index; an f64 at
  // f64 index read and stored. Through the views, which the caller has made fresh.
  function load8(ptr) {
    return u8[ptr];
  }
  function load32(index) {
    return u32[index];
  }
  function store32(index, value) {
    i32[index] = value;
  }
  function storeF32(index, value) {
    f32[index] = value;
  }
  function loadF64(index) {
    return f64[index];
  }
  function storeF64(index, value) {
    f64[index] = value;
  }
  // The first length bytes of data (bytes, as bytesOf gives them) into C memory at ptr.
  function writeBytes(ptr, data, length) {
    bytes(ptr, length).set(length === data.length ? data : data.subarray(0, length));
  }
  // Bytes to write: an ArrayBuffer's, or a Uint8Array as it is.
  function bytesOf(data) {
    return new Uint8Array(data);
  }
  // Whether data is bytes: what the store gives back for what store_put kept.
  function isBytes(data) {
    return data instanceof Uint8Array;
  }
  // As many whole characters of text as fit in capacity bytes at ptr, as UTF-8: encodeInto's { read, written }.
  function encodeTo(ptr, capacity, text) {
    return textEncoder.encodeInto(text, bytes(ptr, capacity));
  }
  // length bytes at ptr from the browser's cryptographic generator (at most 65536).
  function randomBytes(ptr, length) {
    crypto.getRandomValues(bytes(ptr, length));
  }
  // The pixels at ptr (channels bytes each: RGBA, or RGB and opaque) as an ImageData for context.
  function pixels(ptr, width, height, channels, context) {
    let data = new Uint8ClampedArray(memory.buffer, ptr, width * height * channels);
    if (channels === 3) {
      const rgba = new Uint8ClampedArray(width * height * 4);
      for (let from = 0, to = 0; to < rgba.length; from += 3, to += 4) {
        rgba[to] = data[from];
        rgba[to + 1] = data[from + 1];
        rgba[to + 2] = data[from + 2];
        rgba[to + 3] = 255;
      }
      data = rgba;
    }
    return new ImageData(data, width, height);
  }
  // length unsigned 32-bit words, all zero, with old's in front when it grows (listenMask, roKey, aheadIndex:
  // the shim's own tables, which have no typed arrays either where C's memory has none).
  function newWords(length, old) {
    const words = new Uint32Array(length);
    if (old) {
      words.set(old);
    }
    return words;
  }
  const fromCode = String.fromCharCode;
  // n ASCII bytes at p as a string: fromCharCode with the bytes as arguments, 8 at a time. Off the views
  // themselves, the runtime strings' hot path; memory-words.js has its own.
  function ascii(p, n) {
    const b = u8;
    let v = "";
    for (; n >= 8; p += 8, n -= 8) {
      v += fromCode(b[p], b[p + 1], b[p + 2], b[p + 3], b[p + 4], b[p + 5], b[p + 6], b[p + 7]);
    }
    switch (n) {
      case 0: return v;
      case 1: return v + fromCode(b[p]);
      case 2: return v + fromCode(b[p], b[p + 1]);
      case 3: return v + fromCode(b[p], b[p + 1], b[p + 2]);
      case 4: return v + fromCode(b[p], b[p + 1], b[p + 2], b[p + 3]);
      case 5: return v + fromCode(b[p], b[p + 1], b[p + 2], b[p + 3], b[p + 4]);
      case 6: return v + fromCode(b[p], b[p + 1], b[p + 2], b[p + 3], b[p + 4], b[p + 5]);
      default: return v + fromCode(b[p], b[p + 1], b[p + 2], b[p + 3], b[p + 4], b[p + 5], b[p + 6]);
    }
  }
  // (end of C's memory)
  // Puts node into parent before node next (null: at the end).
  function place(parent, node, next) {
    parent.insertBefore(node, next);
  }
  // The n bytes of UTF-8 at p as a string. Short ones that are ASCII skip TextDecoder, whose fixed cost
  // is 100 to 140 ns in Chrome whatever the length.
  function readString(p, n) {
    if (n <= ASCII_MAX) {
      let high = 0;
      for (let k = 0; k < n; k++) {
        high |= load8(p + k);
      }
      if (high < 128) {
        return ascii(p, n);
      }
    }
    return textDecoder.decode(viewBytes(p, n));
  }
  // The string a cd_str stands for (s is an i32 from an import): a literal's slot address is positive and
  // indexes the page's table; a runtime string's serial has the top bit set (negative), and its record
  // in C's block says where its bytes are right now. CD_NONE (0) is no string and never reaches here.
  // Kept small so V8 inlines it into every import: everything but the literal lookup is in strRest.
  function str(s) {
    if (s >= litBase && s < litEnd) {
      return lits[(s - litBase) >> 2];
    }
    return strRest(s);
  }
  // Not a literal: the read-only cache, then the runtime record and the ahead range.
  function strRest(s) {
    if (s > 0) {
      // A pointer into read-only data is its own id. Any other positive id is not a slot, so it is one of these.
      const h = s & 255;
      if (roKey[h] === s) {
        return roVal[h];
      }
      let v = roMap ? roMap.get(s) : roObj[s];
      if (v === undefined) {
        if (stale()) {
          views();
        }
        let end = s;
        while (load8(end)) {
          end++;
        }
        v = readString(s, end - s);
        if (roMap) {
          roMap.set(s, v);
        } else {
          roObj[s] = v;
        }
        if (onRoRead) {
          onRoRead(s);
        }
      }
      roKey[h] = s;
      roVal[h] = v;
      return v;
    }
    if (stale()) {
      views();
    }
    const k = ((s & 0x7fffffff) - load32(tmpSerial0)) & 0x7fffffff;
    if (k >= load32(tmpCount)) {
      throw new Error("cdom: a cd_tmp string used after the call it was made for");
    }
    const at = (tmpRecords >> 2) + k * recWords;
    const q = load32(at + recP);
    const n = load32(at + recLen);
    if (aheadText !== null && q >= aheadAt && q + n <= aheadEnd) {
      const a = q - aheadAt;
      const text = aheadIndex === null ? aheadText.substring(a, a + n) : aheadText.substring(aheadIndex[a], aheadIndex[a + n]);
      if (checkAhead && text !== readString(q, n)) {
        throw new Error("cdom: bytes inside a cd_tmp_ahead range changed");
      }
      return text;
    }
    return readString(q, n);
  }
  // cd_tmp_ahead: decodes [p, p + len) once, so the runtime strings inside it are cut from the result (str).
  // The range holds until the outermost call into C ends (callEnded), the next ahead, or ahead(0, 0).
  function ahead(p, len) {
    if (!len) {
      aheadText = null;
      return;
    }
    p >>>= 0;
    if (stale()) {
      views();
    }
    aheadText = textDecoder.decode(viewBytes(p, len));
    aheadAt = p;
    aheadEnd = p + len;
    if (aheadText.length === len) {
      aheadIndex = null; // all ASCII: a byte offset is a character offset
      return;
    }
    aheadIndex = newWords(len + 1);
    let u = 0;
    for (let i = 0; i < len; i++) {
      aheadIndex[i] = u;
      const c = load8(p + i);
      u += (c & 0xc0) !== 0x80; // a lead byte or an ASCII byte: one unit
      u += c >= 0xf0;           // an astral lead byte: a surrogate pair
    }
    aheadIndex[len] = u;
  }
  // The outermost call into C has returned or thrown: its range and its runtime strings are dead. One rule
  // for both, so a cd_str made before a throw is no longer readable by the next call.
  function callEnded() {
    aheadText = null;
    if (stale()) {
      views();
    }
    store32(tmpSerial0, load32(tmpSerial0) + load32(tmpCount));
    store32(tmpCount, 0);
  }

  // Puts a new top-level node where at() said, or into the open element.
  function placeNew(node) {
    if (depth) {
      stack[depth - 1].appendChild(node);
    } else if (base !== DETACHED) {
      place(nodes[base], node, before ? nodes[before] : null);
    }
  }

  // Where an event happened: a touch event's first changed touch (the finger that pressed, moved or
  // lifted), any other event's own clientX and clientY.
  function pointOf(event) {
    return event.changedTouches ? event.changedTouches[0] : event;
  }
  // 1 when the event is aimed at a field the user types into.
  function editing(target) {
    if (!target || !target.tagName) {
      return 0;
    }
    const tag = target.tagName;
    return +(target.isContentEditable || tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT");
  }
  // Writes the event record into C memory and calls C; C flushes its answer on the way out, and
  // returns the flags its handler set. An event dispatched while C runs (focus() called from C fires
  // focus events) waits for a microtask, so it never lands in the middle of a handler. It keeps the
  // node its id names now: see drain.
  function post(type, id, event, path, pathDepth) {
    if (busy) {
      if (queue.push([type, id, event, path, pathDepth, nodes[id]]) === 1) {
        queueMicrotask(drain);
      }
      return;
    }
    if (stale()) {
      views();
    }
    const key = event.key;
    const viewport = type === RESIZE || type === VISIBILITY;
    const point = pointOf(event);
    const pointer = fillsPointer[type] === 1; // other types skip the getters and read 0
    const wheel = type === WHEEL;
    store32(evType, type);
    store32(evId, id);
    store32(evX, viewport ? innerWidth : point.clientX | 0);
    store32(evY, viewport ? innerHeight : point.clientY | 0);
    store32(evButton, event.button | 0);
    store32(evKey, viewport ? +doc.hidden : event.keyCode | 0);
    store32(evMods, event.shiftKey | (event.ctrlKey << 1) | (event.altKey << 2) | (event.metaKey << 3)
      | (editing(event.target) << 4));
    store32(evPath, path);
    store32(evDepth, pathDepth);
    store32(evCh, typeof key === "string" && key.length === 1 ? key.charCodeAt(0) : 0);
    store32(evMs, Math.round(event.timeStamp || 0));
    store32(evPointer, pointer ? event.pointerId : 0);
    store32(evButtons, pointer ? event.buttons : 0);
    storeF32(evDx, wheel ? event.deltaX : 0);
    storeF32(evDy, wheel ? event.deltaY : 0);
    nodes[EVENT] = event;
    const flags = exports.cd_event();
    nodes[EVENT] = undefined;
    if (flags & 1) {
      event.preventDefault();
    }
    if (flags & 2) {
      event.stopPropagation();
    }
  }
  // A waiting event goes out only if its id still names the node it named when it was queued. The batch
  // that fired it can remove that node (removing a focused input fires its focusout) and free the id, and
  // cd_id_new hands a freed id out first, so the id may name a new node by now that must not hear the old
  // one's event; a node removed and not replaced is gone too. Host ids (window, document, body) are never
  // reused, so theirs always go out.
  function drain() {
    for (const [type, id, event, path, pathDepth, node] of queue.splice(0)) {
      if (nodes[id] === node) {
        post(type, id, event, path, pathDepth);
      }
    }
  }

  // One listener on the mount per event type: the event goes to the nearest listening ancestor of
  // its target, with the path from that node down (8 bits of child index per level). Not passive,
  // so C can prevent a wheel or a touchmove.
  function delegate(type) {
    delegated[type] = 1;
    const word = type >> 5;
    const bit = 1 << (type & 31);
    const mount = nodes[0];
    const listener = (event) => {
      let path = 0;
      let pathDepth = 0;
      for (let node = event.target; node && node !== mount; node = node.parentNode) {
        if (node.$i !== undefined && listenMask[node.$i * 2 + word] & bit) {
          post(type, node.$i, event, path, pathDepth);
          return;
        }
        let index = 0;
        for (let sibling = node.previousElementSibling; sibling; sibling = sibling.previousElementSibling) {
          index++;
        }
        path = (path << 8) | (index & 255);
        pathDepth++;
      }
    };
    delegatedListener[type] = listener;
    mount.addEventListener(EVENTS[type], listener, { capture: (type >= FOCUS && type <= PAUSE) || type === CANCEL || type === CLOSE, passive: false });
  }
  // A browser with no capture phase (the fourth tier, IE 6 to 8) never brings a type that does not bubble up to the
  // mount's listener, so there the node C listens on hears it itself: tools/legacy/shim-old.js puts its reach in place
  // of this one. Where the capture phase exists the mount hears every type, and there is nothing to do.
  function reach(node, type) {}

  // Where new top-level nodes go: into parent, before node beforeId (0: at the end).
  function at(parent, beforeId) {
    base = parent >>> 0;
    before = beforeId;
  }
  // An opened element or clone is a child of the open element, if any.
  function push(element) {
    if (depth) {
      stack[depth - 1].appendChild(element);
    }
    stack[depth++] = element;
    last = lastElement = element;
  }
  // Opens an element. An <svg> and everything opened inside it is made in the SVG namespace, so the
  // shim counts the open elements that are SVG: one counter and one compare, no read of the DOM.
  function open(tag) {
    const name = str(tag);
    if (svgDepth || name === "svg") {
      svgDepth++;
      push(doc.createElementNS(SVG, name));
    } else {
      push(doc.createElement(name));
    }
  }
  // Closes the open element; the outermost one is placed.
  function close() {
    if (svgDepth) {
      svgDepth--;
    }
    const element = stack[--depth];
    if (!depth) {
      root = element;
      placeNew(element);
    }
  }
  function text(id) {
    last = lastText = doc.createTextNode(str(id));
    placeNew(last);
  }
  function text_num(number) {
    last = lastText = doc.createTextNode(number >>> 0);
    placeNew(last);
  }
  function attr(name, value) {
    last.setAttribute(str(name), str(value));
  }
  function setClass(value) {
    last.className = str(value);
  }
  // Names the element opened last, the text node made last, or the open element, with a node id.
  function name_el(id) {
    nodes[id] = lastElement;
  }
  function name_text(id) {
    nodes[id] = lastText;
  }
  function name_open(id) {
    nodes[id] = stack[depth - 1];
  }
  // Names the last node and listens for event type on it (through the mount's listener), or, for a
  // host object, listens on the object itself.
  function listen(id, type) {
    if (id && id < EVENT) {
      listenHost(id, type);
      return;
    }
    nodes[id] = last;
    last.$i = id;
    if (id * 2 + 1 >= listenMask.length) {
      listenMask = newWords(id * 4 + 2, listenMask);
    }
    listenMask[id * 2 + (type >> 5)] |= 1 << (type & 31);
    if (!delegated[type]) {
      delegate(type);
    }
    reach(last, type);
  }
  function unlisten(id, type) {
    if (id && id < EVENT) {
      unlistenHost(id, type);
    } else if (id * 2 + 1 < listenMask.length) {
      listenMask[id * 2 + (type >> 5)] &= ~(1 << (type & 31));
    }
  }
  function value(id) {
    last.nodeValue = str(id);
  }
  function value_num(number) {
    last.nodeValue = number >>> 0;
  }
  // The element just built becomes template templateId, with the slots marked in it.
  function tpl_save(templateId) {
    templates[templateId] = root;
    templateSlots[templateId] = slots;
    slots = [];
    root.remove();
  }
  function clone(templateId) {
    if (svgDepth) {
      svgDepth++;
    }
    push(templates[templateId].cloneNode(true));
  }
  function slot(index) {
    slots[index] = last;
  }
  function tpl_text(templateId, index, id) {
    templateSlots[templateId][index].nodeValue = str(id);
  }
  function tpl_text_num(templateId, index, number) {
    templateSlots[templateId][index].nodeValue = number >>> 0;
  }
  // A walk: count steps of 2 bits (1 firstChild, 2 nextSibling, 3 parentNode) from a named node,
  // or from the last node when from is -1.
  function walk(from, steps, count) {
    let node = from === -1 ? last : nodes[from];
    for (; count; count--, steps >>>= 2) {
      const step = steps & 3;
      if (step === 1) {
        node = node.firstChild;
      } else if (step === 2) {
        node = node.nextSibling;
      } else {
        node = node.parentNode;
      }
    }
    last = node;
  }
  function walk_value(from, steps, count, id) {
    walk(from, steps, count);
    last.nodeValue = str(id);
  }
  function walk_value_num(from, steps, count, number) {
    walk(from, steps, count);
    last.nodeValue = number >>> 0;
  }
  // cd_set_text_node and cd_set_text_node_num set a text node's value. An element's nodeValue is null and
  // setting it does nothing, so an element id (the trap: the element whose text it is) throws here,
  // out of the C call, instead of leaving the page as it was.
  function textNode(id, call) {
    const node = nodes[id];
    if (node && node.nodeType === 3) {
      return node;
    }
    throw new Error(`cdom: ${call}(${id}, ...): ${node ? 'not a text node' : 'no node has this id'}`);
  }
  function set_text(id, textId) {
    textNode(id, 'cd_set_text_node').nodeValue = str(textId);
  }
  function set_text_num(id, number) {
    textNode(id, 'cd_set_text_node_num').nodeValue = number >>> 0;
  }
  function set_attr(id, name, value) {
    nodes[id].setAttribute(str(name), str(value));
  }
  function set_class(id, value) {
    nodes[id].className = str(value);
  }
  // One style property.
  function style(id, property, value) {
    nodes[id].style.setProperty(str(property), str(value));
  }
  function style_num(id, property, number, unit) {
    nodes[id].style.setProperty(str(property), number + UNITS[unit]);
  }
  // Pixels in C memory (channels bytes each: RGBA, or RGB and opaque), at (x, y) in an image of
  // fullWidth x fullHeight, to an image file of mime at quality (0 to 100): copied now, encoded off the
  // call, the file's bytes back into C (cd_encoded) with token.
  function encode(pixelsPtr, width, height, channels, fullWidth, fullHeight, x, y, mime, quality, token) {
    const canvas = doc.createElement("canvas");
    canvas.width = fullWidth;
    canvas.height = fullHeight;
    const context = canvas.getContext("2d");
    context.putImageData(pixels(pixelsPtr, width, height, channels, context), x, y);
    canvas.toBlob(async (blob) => deliver(exports.cd_encoded, token, await blob.arrayBuffer()), str(mime), quality / 100);
  }
  // An image file's bytes in C memory as a url(blob:) on a style property, set once the image has
  // decoded (so no frame paints the property with nothing behind it), then answered with
  // cd_image(token, 1); bytes that do not decode set nothing and answer 0.
  function image_bytes(id, property, bytesPtr, length, mime, token) {
    const node = nodes[id];
    const name = str(property);
    const url = URL.createObjectURL(new Blob([copyBytes(bytesPtr, length)], { type: str(mime) }));
    const image = new Image();
    image.src = url;
    image.decode().then(() => {
      node.style.setProperty(name, `url(${url})`);
      exports.cd_image(token, 1);
    }, () => {
      URL.revokeObjectURL(url);
      exports.cd_image(token, 0);
    });
  }
  // The id's node no longer answers to it: a node left in place after its id was freed must not be
  // delivered under the id once it is handed out again.
  function forget(id) {
    const node = nodes[id];
    if (node && node.$i === id) {
      node.$i = undefined;
    }
    nodes[id] = undefined;
    listenMask[id * 2] = 0;
    listenMask[id * 2 + 1] = 0;
  }
  function remove(id) {
    nodes[id].remove();
    forget(id);
  }
  function free(id) {
    forget(id);
  }
  // Moves a node before node beforeId (0: to the end of its parent).
  function move(id, beforeId) {
    const node = nodes[id];
    place(node.parentNode, node, beforeId ? nodes[beforeId] : null);
  }
  function clear(id) {
    nodes[id].textContent = "";
  }
  // Takes a node out of the document, remembering where it was, and puts it back.
  function detach(id) {
    const node = nodes[id];
    node.$p = node.parentNode;
    node.$s = node.nextSibling;
    node.remove();
  }
  function attach(id) {
    const node = nodes[id];
    place(node.$p, node, node.$s);
    node.$p = null;
    node.$s = null;
  }

  // A host object (the window, the document, the body) hears event type with a listener of its own,
  // once however often it is asked; the record names the object. Not passive, so C can stop a
  // touchmove scrolling the page.
  function listenHost(id, type) {
    const slot = id * EVENTS.length + type;
    if (hostListeners[slot]) {
      return;
    }
    const listener = (event) => post(type, id, event, 0, 0);
    hostListeners[slot] = listener;
    nodes[id].addEventListener(EVENTS[type], listener, { passive: false });
  }
  function unlistenHost(id, type) {
    const slot = id * EVENTS.length + type;
    const listener = hostListeners[slot];
    if (!listener) {
      return;
    }
    nodes[id].removeEventListener(EVENTS[type], listener);
    hostListeners[slot] = undefined;
  }
  // The listening node under (x, y): the first that listens among every element there that takes
  // pointer events, topmost first (each one's ancestors come after it), as a drop's target is read
  // through whatever lies over it; 0 for none.
  function hit(x, y) {
    const mount = nodes[0];
    for (const node of doc.elementsFromPoint(x, y)) {
      if (node === mount) {
        break;
      }
      if (node.$i !== undefined && (listenMask[node.$i * 2] | listenMask[node.$i * 2 + 1])) {
        return node.$i;
      }
    }
    return 0;
  }
  // The viewport into C memory: width, height, hidden, device pixel ratio times 100.
  function viewport(recordPtr) {
    if (stale()) {
      views();
    }
    const record = recordPtr >> 2;
    store32(record + viewportW, innerWidth);
    store32(record + viewportH, innerHeight);
    store32(record + viewportHidden, +doc.hidden);
    store32(record + viewportDpr, Math.round(devicePixelRatio * 100));
  }
  // A node's box on screen into C memory as four doubles: left, top, width, height; 0 when id names
  // nothing with a box.
  function rect(id, outPtr) {
    const node = nodes[id];
    if (!node || !node.getBoundingClientRect) {
      return 0;
    }
    const box = node.getBoundingClientRect();
    if (stale()) {
      views();
    }
    const edges = [box.left, box.top, box.width, box.height]; // cd_rect's double[4], in its order
    for (let k = 0; k < edges.length; k++) {
      storeF64((outPtr >> 3) + k, edges[k]);
    }
    return 1;
  }
  // A timer: C keeps the handle (by token) to stop it, and is told which one fired. A 0 ms timer is the next
  // task (postTask), never a setTimeout: browsers clamp a nested 0 ms timeout to 4 ms and throttle timeouts in a
  // window that is not in front (Safari by seconds), and a 0 ms timer is how C yields and goes on (the steps of
  // cd_image_file_cb). Its handle is from its own range, past any setTimeout id.
  const soon = {};
  let soonNext = 0;
  function timer(ms, token) {
    if (!ms) {
      const handle = 0x40000000 + (soonNext = (soonNext + 1) & 0x3fffffff);
      soon[handle] = 1;
      postTask(() => {
        if (soon[handle]) {
          delete soon[handle];
          exports.cd_timer(token, handle);
        }
      });
      return handle;
    }
    const handle = setTimeout(() => exports.cd_timer(token, handle), ms);
    return handle;
  }
  function timer_stop(handle) {
    if (soon[handle]) {
      delete soon[handle];
      return;
    }
    clearTimeout(handle);
  }
  function raf(token) {
    requestAnimationFrame((time) => exports.cd_raf(token, time));
  }
  function now() {
    return performance.now();
  }

  // UTF-8 bytes in C memory as a string.
  function utf8(ptr, length) {
    return textDecoder.decode(bytes(ptr, length));
  }
  // A string into C memory as UTF-8, at most capacity bytes; returns its whole length in bytes,
  // so C can tell it was cut and ask again with more room.
  function writeString(text, destPtr, capacity) {
    const encoded = textEncoder.encode(text);
    writeBytes(destPtr, encoded, Math.min(capacity, encoded.length));
    return encoded.length;
  }
  // Bytes into C memory, in room C makes with cd_bytes_buf, then answer(arg, ptr, length).
  function deliver(answer, arg, data) {
    const incoming = bytesOf(data);
    const ptr = exports.cd_bytes_buf(incoming.length);
    writeBytes(ptr, incoming, incoming.length);
    answer(arg, ptr, incoming.length);
  }

  // The network (src/cdom_net.c).
  const sockets = [];     // the open sockets by id
  const freeSockets = []; // ids whose sockets closed, handed out again first
  // A refused socket's stand-in until its close: a send is dropped, a close does nothing.
  const refusedSocket = { send() {}, close() {}, bufferedAmount: 0 };
  // Opens a WebSocket to the url in C memory; its events come back by socket id. The id is free
  // once the socket closed (before C hears it), so the table holds only the sockets open at once.
  // A constructor that throws (the browser refuses the URL) throws out of the call, or, refusable,
  // gets an id all the same, and from the event loop C hears the refusal, then the close.
  function ws_open(urlPtr, urlLen, refusable) {
    let socket;
    try {
      socket = new WebSocket(utf8(urlPtr, urlLen));
    } catch (error) {
      if (!refusable) {
        throw error;
      }
    }
    const id = freeSockets.length ? freeSockets.pop() : sockets.length;
    if (!socket) {
      sockets[id] = refusedSocket;
      setTimeout(() => {
        exports.cd_net_refused(id);
        sockets[id] = undefined;
        freeSockets.push(id);
        exports.cd_net_close(id, 1006); // CD_WS_REFUSED_CODE (src/cdom_net.h)
      });
      return id;
    }
    sockets[id] = socket;
    socket.binaryType = "arraybuffer";
    socket.onopen = () => exports.cd_net_open(id);
    socket.onmessage = (event) => {
      const data = typeof event.data === "string" ? textEncoder.encode(event.data) : event.data;
      deliver(exports.cd_net_msg, id, data);
    };
    socket.onclose = (event) => {
      sockets[id] = undefined;
      freeSockets.push(id);
      exports.cd_net_close(id, event.code);
    };
    return id;
  }
  // send copies the bytes it is given, so a view of C memory is enough.
  function ws_send(id, bytesPtr, length) {
    sockets[id].send(bytes(bytesPtr, length));
  }
  function ws_close(id) {
    sockets[id].close();
  }
  function ws_buffered(id) {
    return sockets[id].bufferedAmount;
  }
  // Request headers from one block of "Name: value\n" lines.
  function headerList(headersPtr, headersLen) {
    const list = [];
    for (const line of utf8(headersPtr, headersLen).split("\n")) {
      const colon = line.indexOf(":");
      if (colon > 0) {
        list.push([line.slice(0, colon), line.slice(colon + 1).trim()]);
      }
    }
    return list;
  }
  // One request; the status and the body come back with token: status 0 when it never arrived,
  // 1 when deadlineMs (0: none) passed first and the request was aborted.
  function http(token, methodPtr, methodLen, urlPtr, urlLen, headersPtr, headersLen, bodyPtr, bodyLen, deadlineMs) {
    const controller = new AbortController();
    const init = {
      method: utf8(methodPtr, methodLen),
      headers: headerList(headersPtr, headersLen),
      body: bodyLen ? copyBytes(bodyPtr, bodyLen) : undefined,
      signal: controller.signal,
    };
    const timer = deadlineMs ? setTimeout(() => controller.abort(), deadlineMs) : 0;
    let status = 0;
    fetch(utf8(urlPtr, urlLen), init)
      .then((response) => {
        status = response.status;
        return response.arrayBuffer();
      })
      .then(
        (data) => {
          clearTimeout(timer);
          deliver((arg, ptr, length) => exports.cd_net_http(arg, status, ptr, length), token, data);
        },
        () => {
          clearTimeout(timer);
          exports.cd_net_http(token, controller.signal.aborted ? 1 : 0, 0, 0);
        });
  }
  function epoch() {
    return Date.now();
  }
  // The page's scheme and host into C memory (cd_page_origin).
  function origin(destPtr, capacity) {
    return writeString(location.protocol + "//" + location.host, destPtr, capacity);
  }

  // A picked file into C (src/cdom_file.c). Every answer comes from the event loop, even one known at once;
  // the statuses are cdom_file.h's CD_FILE_* (0 OK, 1 NO_FILE, 2 TOO_BIG, 3 UNREADABLE, 4 UNSUPPORTED,
  // 5 NO_ROOM).
  // The file at index of node id's files list (an input's, or a drop's dataTransfer), or null.
  function pickedFile(id, index) {
    const files = nodes[id].files;
    return (files && files[index >>> 0]) || null;
  }
  // Bytes into a block of cd_file_room's, then answer(OK, ptr, length); NO_ROOM, with no block, when C
  // has no room for them. The export that answers frees the block.
  function handOver(data, answer) {
    const ptr = exports.cd_file_room(data.length);
    if (!ptr) {
      answer(5, 0, data.length);
      return;
    }
    writeBytes(ptr, data, data.length);
    answer(0, ptr, data.length);
  }
  // A file's bytes (cd_file_read): one over maxBytes is TOO_BIG from its size, never read.
  function file_read(token, id, index, maxBytes) {
    const answer = (status, ptr, length) => exports.cd_file_got(token, status, ptr, length);
    if (typeof FileReader === "undefined") {
      setTimeout(() => answer(4, 0, 0));
      return;
    }
    const file = pickedFile(id, index);
    if (!file) {
      setTimeout(() => answer(1, 0, 0));
      return;
    }
    if (file.size > maxBytes >>> 0) {
      setTimeout(() => answer(2, 0, Math.min(file.size, 4294967295)));
      return;
    }
    const reader = new FileReader();
    reader.onload = () => handOver(bytesOf(reader.result), answer);
    reader.onerror = () => answer(3, 0, 0);
    reader.readAsArrayBuffer(file);
  }

  // A picked file read in order (cd_file_open, cd_file_next_cb, cd_file_close). readers[r] holds the File itself (so
  // another pick leaves an open reader as it was), its stream reader, the rest of a stream chunk not yet given, and the
  // piece pending: C's dst and max, and the token that answers it. A piece is answered once max bytes are at dst or the
  // file is at its end, each answer a task of its own (a message posted to the page itself: a stream's read resolves
  // from its queue as a microtask, so even its completion may be in the task of the answer before), never a microtask
  // after the piece before. One read is kept in flight ahead of C. Where Blob.stream is missing, FileReader reads
  // slices of the size asked.
  const readers = [null];
  const posted = [];
  let postPort;
  function postTask(f) {
    if (typeof MessageChannel === "undefined") {
      setTimeout(f);
      return;
    }
    if (!postPort) {
      const channel = new MessageChannel();
      channel.port1.onmessage = () => posted.shift()();
      postPort = channel.port2;
    }
    posted.push(f);
    postPort.postMessage(0);
  }
  function file_open(id, index, readerPtr, sizePtr) {
    if (typeof FileReader === "undefined") {
      return 4;
    }
    const file = pickedFile(id, index);
    if (!file) {
      return 1;
    }
    if (file.size > 4294967295) {
      return 2;
    }
    let r = 1;
    while (readers[r]) {
      r++;
    }
    readers[r] = { file, size: file.size, pos: 0, token: 0, due: false, dst: 0, max: 0, got: 0, chunk: null, off: 0, reading: false, end: false, failed: false, stream: null };
    if (stale()) {
      views();
    }
    store32(readerPtr >>> 2, r);
    store32(sizePtr >>> 2, file.size);
    return 0;
  }
  // The rest of the chunk in hand into the piece, as far as both go (never past the file's size).
  function fillPiece(rd) {
    while (rd.chunk && rd.got < rd.max && rd.pos < rd.size) {
      const k = Math.min(rd.chunk.length - rd.off, rd.max - rd.got, rd.size - rd.pos);
      writeBytes(rd.dst + rd.got, rd.chunk.subarray(rd.off, rd.off + k), k);
      rd.off += k;
      rd.got += k;
      rd.pos += k;
      if (rd.off === rd.chunk.length) {
        rd.chunk = null;
      }
    }
  }
  // The next chunk: from the stream, or a slice of the size the piece still wants. Then the piece goes on, in this task.
  function readAhead(r, rd) {
    if (rd.reading || rd.chunk || rd.end || rd.failed || rd.pos >= rd.size) {
      return;
    }
    rd.reading = true;
    const got = (chunk) => {
      if (readers[r] !== rd) {
        return; // closed meanwhile
      }
      rd.reading = false;
      if (chunk) {
        rd.chunk = chunk;
        rd.off = 0;
      } else {
        rd.end = true;
      }
      if (rd.token && !rd.due) {
        piece(r, rd);
      }
    };
    const failed = () => {
      if (readers[r] === rd) {
        rd.reading = false;
        rd.failed = true;
        if (rd.token && !rd.due) {
          piece(r, rd);
        }
      }
    };
    if (typeof rd.file.stream === "function") {
      rd.stream = rd.stream || rd.file.stream().getReader();
      rd.stream.read().then((c) => got(c.done || !c.value.length ? null : c.value), failed);
    } else {
      const reader = new FileReader();
      const want = Math.max(rd.max - rd.got, 1);
      reader.onload = () => got(reader.result.byteLength ? bytesOf(reader.result) : null);
      reader.onerror = failed;
      reader.readAsArrayBuffer(rd.file.slice(rd.pos, Math.min(rd.pos + want, rd.size)));
    }
  }
  // The piece pending: answered when it is full or the file is at its end (or failed); else a read is asked for.
  function piece(r, rd) {
    fillPiece(rd);
    const done = rd.pos >= rd.size;
    if (rd.got < rd.max && !done && !rd.end && !rd.failed) {
      readAhead(r, rd);
      return;
    }
    const status = rd.got ? 0 : rd.failed || !done ? 3 : 0;
    const n = status ? 0 : rd.got;
    const token = rd.token;
    rd.due = true; // answered once: a chunk that comes before the posted answer waits for the next piece
    const answer = () => {
      if (readers[r] !== rd || rd.token !== token) {
        return; // closed before the answer was delivered
      }
      rd.token = 0;
      rd.due = false;
      readAhead(r, rd); // the next read is in flight while C uses this piece
      exports.cd_file_piece(token, status, n);
    };
    postTask(answer);
  }
  function file_next(r, dst, max, token) {
    const rd = readers[r];
    if (!rd || rd.token) {
      return 1; // C traps
    }
    rd.token = token;
    rd.dst = dst;
    rd.max = max >>> 0;
    rd.got = 0;
    piece(r, rd);
    return 0;
  }
  // Ends the reader: the File dropped, its stream cancelled; the pending piece's token goes back to C, which frees it.
  function file_close(r) {
    const rd = readers[r];
    if (!rd) {
      return 0;
    }
    readers[r] = null;
    if (rd.stream) {
      rd.stream.cancel().catch(() => {});
    }
    return rd.token;
  }

  // A throwaway job (src/cdom_job.c): the job module (a URL, or a name in cdomJobs, the page's bundled ones,
  // which tools/cdpage writes beside the string table) compiled once, posted to a fresh worker made from a blob:
  // URL (so a bundled page works), the worker ended when it answers. Statuses are cdom_job.h's CD_JOB_*
  // (0 OK, 1 NO_ROOM, 2 FAILED, 3 UNSUPPORTED, 4 NO_FILE). The worker's code, a function of its own that names
  // nothing outside it: it loads the module, puts the args and the input (an ArrayBuffer, or a File read
  // straight into the job's memory) in room the job makes with job_room, calls run, and answers [status,
  // output]; run's 0 is OK, 1 NO_ROOM and any other value or a trap FAILED. cd_job_answer's bytes are copied
  // in the call (cdom_job_mod.h: the job may free or reuse the block after it), and the last answer counts.
  // A pieces job (cd_job_pieces_cb, mode 2) is fed the file's stream instead: pieces_begin, then pieces_max bytes into
  // pieces_room and pieces_put, until it answers other than CD_JOB_PIECES_MORE (0x100), with no yielding (nothing
  // else runs in the worker); FileReaderSync on slices where Blob.stream is missing.
  function jobWorker() {
    onmessage = (event) => {
      const [module, input, args, mode] = event.data;
      let heard;
      let status = 2;
      let exports;
      const room = (n) => {
        const p = exports.job_room(n);
        if (!p) {
          throw 1;
        }
        return p;
      };
      const pieces = () => {
        const size = input.size;
        const argsAt = room(args.byteLength);
        new Uint8Array(exports.memory.buffer, argsAt, args.byteLength).set(new Uint8Array(args));
        const code = exports.pieces_begin(size, argsAt, args.byteLength) >>> 0;
        if (code) {
          status = code > 1 ? 2 : code;
          return undefined;
        }
        const stream = input.stream ? input.stream().getReader() : null;
        const sync = stream ? null : new FileReaderSync();
        let chunk = null;
        let off = 0;
        let at = 0;
        const next = (want) => stream ? stream.read().then((c) => c.done ? null : c.value)
          : Promise.resolve(new Uint8Array(sync.readAsArrayBuffer(input.slice(at, Math.min(size, at + want)))));
        // The chunks into the room, as far as both go (never past the file's size).
        const copy = (dst, max, got) => {
          while (chunk && got < max && at < size) {
            const k = Math.min(chunk.length - off, max - got, size - at);
            new Uint8Array(exports.memory.buffer, dst + got, k).set(chunk.subarray(off, off + k));
            off += k;
            got += k;
            at += k;
            if (off === chunk.length) {
              chunk = null;
            }
          }
          return got;
        };
        // Pieces from here on: max bytes (fewer only at the file's end) into the room, then the put; a chunk read
        // when the piece wants one; until the job's end.
        const from = (dst, max, got) => {
          for (;;) {
            got = copy(dst, max, got);
            if (got < max && at < size) {
              return next(max - got).then((c) => {
                if (!c || !c.length) {
                  throw 2; // the stream ended before the file's size
                }
                chunk = c;
                off = 0;
                return from(dst, max, got);
              });
            }
            const r = exports.pieces_put(got) >>> 0;
            if (r !== 0x100) {
              status = r > 1 ? 2 : r;
              return status ? undefined : heard || new ArrayBuffer(0);
            }
            max = exports.pieces_max() >>> 0;
            dst = exports.pieces_room() >>> 0;
            got = 0;
          }
        };
        return from(exports.pieces_room() >>> 0, exports.pieces_max() >>> 0, 0);
      };
      // A job that links the image module (APP_JOB_<name>_MODULES, cdom.mk) leaves a libjpeg error the js way
      // (src/cdom_image_sjlj.c), as the page does: these two are the page's image_call and image_throw.
      const thrown = {};
      const image = {
        image_call: (body, ctx) => {
          try {
            exports.cd_image_run(body, ctx);
            return 0;
          } catch (e) {
            if (e !== thrown) {
              throw e;
            }
            return 1;
          }
        },
        image_throw: () => {
          throw thrown;
        },
      };
      WebAssembly.instantiate(module, { cdjob: { answer: (p, n) => { heard = exports.memory.buffer.slice(p, p + n); } }, cdom: image })
        .then((instance) => {
          exports = instance.exports;
          if (mode === 2) {
            return pieces();
          }
          const isFile = input.size !== undefined;
          const length = isFile ? input.size : input.byteLength;
          const argsAt = room(args.byteLength);
          new Uint8Array(exports.memory.buffer, argsAt, args.byteLength).set(new Uint8Array(args));
          const inAt = room(length);
          const run = () => {
            const code = exports.run(inAt, length, argsAt, args.byteLength) >>> 0;
            status = code > 1 ? 2 : code;
            return status ? undefined : heard || new ArrayBuffer(0);
          };
          if (!isFile) {
            new Uint8Array(exports.memory.buffer, inAt, length).set(new Uint8Array(input));
            return run();
          }
          const reader = input.stream().getReader();
          let at = inAt;
          const pump = () => reader.read().then((chunk) => {
            if (chunk.done) {
              return run();
            }
            if (at + chunk.value.length > inAt + length) {
              throw 2; // the file grew while it was read: never past the room it was given
            }
            new Uint8Array(exports.memory.buffer, at, chunk.value.length).set(chunk.value);
            at += chunk.value.length;
            return pump();
          });
          return pump();
        })
        .then((output) => postMessage([status, output], output ? [output] : []),
          (error) => postMessage([error === 1 ? 1 : 2]));
    };
  }
  const jobModules = {}; // job name -> promise of its compiled module
  let jobScript;         // the blob: URL of jobWorker, made at the first job
  // Answers the job's callback: the output into room C makes (cd_job_room), NO_ROOM when it has none.
  function jobAnswer(token, status, output) {
    let ptr = 0;
    const length = output ? output.byteLength : 0;
    if (length) {
      ptr = exports.cd_job_room(length);
      if (ptr) {
        writeBytes(ptr, bytesOf(output), length);
      } else {
        status = 1;
      }
    }
    exports.cd_job_got(token, status, ptr, length);
  }
  // cd_job_cb (file 0: the input is length b at C address a) and cd_job_file_cb (file 1: file b of node a's
  // files). The name, the args and the bytes are copied out of C memory in the call. Every answer comes from the
  // event loop.
  const jobLive = {}; // token -> { worker } of each job not yet answered, for job_stop
  function job_stop(token) {
    const job = jobLive[token];
    if (job) {
      delete jobLive[token];
      job.stopped = true;
      if (job.worker) {
        job.worker.terminate();
      }
    }
  }
  function job_run(token, namePtr, nameLen, argsPtr, argsLen, a, b, file) {
    if (typeof Worker === "undefined" || typeof WebAssembly === "undefined") {
      setTimeout(() => jobAnswer(token, 3));
      return;
    }
    const name = utf8(namePtr, nameLen);
    const args = bytes(argsPtr, argsLen).slice().buffer;
    const input = file ? pickedFile(a, b) : bytes(a, b).slice().buffer;
    if (!input) {
      setTimeout(() => jobAnswer(token, 4));
      return;
    }
    const module = jobModules[name] || (jobModules[name] = fetch((globalThis.cdomJobs && cdomJobs[name]) || name)
      .then((response) => response.arrayBuffer()).then((wasm) => WebAssembly.compile(wasm)));
    const job = jobLive[token] = { worker: null, stopped: false };
    module.then((compiled) => {
      if (job.stopped) {
        return;
      }
      jobScript = jobScript || URL.createObjectURL(new Blob(["(" + jobWorker + ")()"], { type: "text/javascript" }));
      let worker;
      try {
        worker = new Worker(jobScript);
      } catch (error) {
        jobAnswer(token, 2);
        return;
      }
      job.worker = worker;
      const end = (status, output) => {
        worker.terminate();
        delete jobLive[token];
        jobAnswer(token, status, output);
      };
      worker.onmessage = (event) => end(event.data[0], event.data[1]);
      worker.onerror = () => end(2);
      worker.postMessage([compiled, input, args, file], file ? [args] : [input, args]);
    }, () => {
      delete jobModules[name];
      delete jobLive[token];
      jobAnswer(token, 2);
    });
  }

  // The image module's way out of a fatal libjpeg error (src/cdom_image_sjlj.c, CDOM_IMAGE_SJLJ := js):
  // image_call runs C's body(ctx) (the export cd_image_run) in a try, and answers 1 when image_throw ended
  // it, 0 when it returned; anything else thrown (a trap) goes on as it came.
  const IMAGE_THROW = {};
  function image_call(body, ctx) {
    try {
      exports.cd_image_run(body, ctx);
      return 0;
    } catch (e) {
      if (e !== IMAGE_THROW) throw e;
      return 1;
    }
  }
  function image_throw() {
    throw IMAGE_THROW;
  }

  // A key/value store over IndexedDB (src/cdom_store.c): bytes in, bytes out, under keys C chooses.
  // A promise of an IndexedDB request's result.
  function promised(request) {
    return new Promise((resolve, reject) => {
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
  }
  let database; // a promise of the open database, made on first use
  function openDatabase() {
    if (!database) {
      const request = indexedDB.open("cdom", 1);
      request.onupgradeneeded = () => request.result.createObjectStore("kv");
      database = promised(request);
    }
    return database;
  }
  // One read on the store in a transaction of its own; a promise of its result.
  async function storeRequest(makeRequest) {
    const db = await openDatabase();
    return promised(makeRequest(db.transaction("kv", "readonly").objectStore("kv")));
  }
  // One write in a transaction of its own; a promise that resolves when the transaction commits (its
  // complete event) and rejects when it aborts. The request's success is not the write
  // kept: the transaction can still abort after it, and a reload before complete loses the write.
  async function storeWrite(makeRequest) {
    const db = await openDatabase();
    return new Promise((resolve, reject) => {
      const transaction = db.transaction("kv", "readwrite");
      transaction.oncomplete = () => resolve();
      // A request that fails fires error, which bubbles to the transaction and aborts it, so abort
      // is the one failure event (abort() and a forced close fire it too).
      transaction.onabort = () => reject(transaction.error);
      makeRequest(transaction.objectStore("kv"));
    });
  }
  // Keeps a copy of the bytes under the key, then answers with token: 1 kept (committed), 0
  // failed. The key and the bytes are read now: C's memory may hold something else once the
  // database is open.
  function store_put(token, keyPtr, keyLen, bytesPtr, length) {
    const key = utf8(keyPtr, keyLen);
    const data = copyBytes(bytesPtr, length);
    storeWrite((store) => store.put(data, key)).then(
      () => exports.cd_store_put_done(token, 1),
      () => exports.cd_store_put_done(token, 0));
  }
  // Forgets the key, then answers with token: 1 done (committed), 0 failed.
  function store_del(token, keyPtr, keyLen) {
    const key = utf8(keyPtr, keyLen);
    storeWrite((store) => store.delete(key)).then(
      () => exports.cd_store_del_done(token, 1),
      () => exports.cd_store_del_done(token, 0));
  }
  // Answers once with token: the bytes under the key, or length -1 for a miss, an error, or no
  // answer within ms (indexedDB.open can hang). Anything not bytes is a miss.
  function store_get(token, keyPtr, keyLen, ms) {
    const key = utf8(keyPtr, keyLen);
    let answered = false;
    const answer = (data) => {
      if (answered) {
        return;
      }
      answered = true;
      if (isBytes(data)) {
        deliver(exports.cd_store_got, token, data);
      } else {
        exports.cd_store_got(token, 0, -1);
      }
    };
    setTimeout(answer, ms);
    // the error must not reach answer: only a miss may follow a failure
    storeRequest((store) => store.get(key)).then(answer, () => answer());
  }

  // Sound over Web Audio (src/cdom_audio.c): C chooses what plays, when and how loud, and hands
  // out the clip and voice ids; the shim keeps a decoded buffer per clip and two nodes per voice.
  const AUDIO_LOCKED = 0;       // the context has never run: the browser waits for a gesture
  const AUDIO_RUNNING = 1;
  const AUDIO_SUSPENDED = 2;    // it ran, and stopped: the page is hidden, or the system took it
  const GESTURES = ["pointerdown", "pointerup", "touchend", "keydown", "click"]; // any may unlock
  const GAIN_EASE = 0.005;      // seconds: a gain change eases in over a few of these, so it does not click
  const clips = [];             // the decoded AudioBuffer by clip id
  const voiceSources = [];      // the playing AudioBufferSourceNode by voice id
  const voiceGains = [];        // and its GainNode
  let audioContext;             // made on first use
  let masterGain;               // every voice goes through it to the speakers
  let audioRan = false;         // the context has run once: a gesture unlocked it
  // The audio context, made on first use. The browser keeps it suspended until a gesture, so every
  // gesture, caught on the window before anything can stop it, resumes it when it is not running:
  // the first unlocks it, a later one brings it back after the system took it. Hidden, the page
  // makes no sound (the context suspends); shown again, it resumes.
  function audio() {
    if (audioContext) {
      return audioContext;
    }
    audioContext = new AudioContext();
    masterGain = audioContext.createGain();
    masterGain.connect(audioContext.destination);
    audioContext.onstatechange = audioStateChanged;
    for (const type of GESTURES) {
      addEventListener(type, unlockAudio, { capture: true, passive: true });
    }
    doc.addEventListener(EVENTS[VISIBILITY], audioFollowsPage);
    return audioContext;
  }
  function unlockAudio() {
    if (audioContext.state !== "running") {
      audioContext.resume().catch(() => {});
    }
  }
  function audioFollowsPage() {
    if (doc.hidden) {
      audioContext.suspend();
    } else {
      audioContext.resume().catch(() => {});
    }
  }
  function audioStateChanged() {
    if (audioContext.state === "running") {
      audioRan = true;
    }
    exports.cd_audio_state_changed(audio_state());
  }
  function audio_state() {
    if (audio().state === "running") {
      return AUDIO_RUNNING;
    }
    return audioRan ? AUDIO_SUSPENDED : AUDIO_LOCKED;
  }
  // Decodes the clip from bytes in C memory, copied now (decoding takes the buffer it is given),
  // then answers with token: the buffer kept as clip, 1, or 0 for bytes that are no sound.
  function audio_load(token, clip, bytesPtr, length) {
    const data = copyBytes(bytesPtr, length).buffer;
    audio().decodeAudioData(data).then(
      (buffer) => {
        clips[clip] = buffer;
        exports.cd_audio_loaded(token, clip, 1);
      },
      () => exports.cd_audio_loaded(token, clip, 0));
  }
  function audio_free(clip) {
    clips[clip] = undefined;
  }
  // Plays clip as voice, through a gain of its own, at playback rate, from atMs on cd_now()'s clock
  // (a time already past: now). Its ended answer comes when it played out or was stopped.
  function audio_play(clip, voice, gain, rate, atMs) {
    const context = audio();
    const source = context.createBufferSource();
    const voiceGain = context.createGain();
    source.buffer = clips[clip];
    source.playbackRate.value = rate;
    voiceGain.gain.value = gain;
    source.connect(voiceGain);
    voiceGain.connect(masterGain);
    source.$v = voice;
    source.onended = voiceEnded;
    voiceSources[voice] = source;
    voiceGains[voice] = voiceGain;
    source.start(Math.max(0, context.currentTime + (atMs - performance.now()) / 1000));
  }
  // One handler for every voice (no function made per play): the shim lets go of the voice's
  // nodes, then C frees its id.
  function voiceEnded(event) {
    const voice = event.target.$v;
    voiceGains[voice].disconnect();
    voiceSources[voice] = undefined;
    voiceGains[voice] = undefined;
    exports.cd_audio_ended(voice);
  }
  function audio_stop(voice) {
    voiceSources[voice].stop();
  }
  function easeGain(node, gain) {
    node.gain.setTargetAtTime(gain, audioContext.currentTime, GAIN_EASE);
  }
  function audio_gain(voice, gain) {
    easeGain(voiceGains[voice], gain);
  }
  function audio_master(gain) {
    audio();
    easeGain(masterGain, gain);
  }

  // Random bytes (src/cdom_random.c): one chunk of C memory filled by the browser's cryptographic
  // generator, C asking chunk by chunk (getRandomValues takes at most 65536 bytes). 1 written, 0
  // when the page has no crypto.getRandomValues (nothing written).
  function random(bufferPtr, length) {
    if (!globalThis.crypto || !crypto.getRandomValues) {
      return 0;
    }
    randomBytes(bufferPtr, length);
    return 1;
  }

  // The address (src/cdom_url.c): C routes; the shim reads the address, changes it as C asks
  // (the History API, so nothing loads) and reports back and forward.
  const URL_PARTS = ["pathname", "search", "hash"]; // cd_url_read's parts; past them, all three
  function urlOf(part) {
    if (part < URL_PARTS.length) {
      return location[URL_PARTS[part]];
    }
    return location.pathname + location.search + location.hash;
  }
  // One part of the address into C memory as UTF-8, at most capacity bytes; returns its length.
  function url_read(part, destPtr, capacity) {
    return writeString(urlOf(part), destPtr, capacity);
  }
  // Shows the path in C memory in the address bar, as a new history entry or in place of the
  // current one: 1 when the address is now that path, 0 when the browser threw (another origin,
  // too many changes too fast, no History API) or let the change go (Chrome's own throttle).
  function url_go(pathPtr, pathLen, replace) {
    const path = utf8(pathPtr, pathLen);
    let wanted;
    try {
      wanted = new URL(path, doc.baseURI).href;
      if (replace) {
        history.replaceState(null, "", path);
      } else {
        history.pushState(null, "", path);
      }
    } catch {
      return 0;
    }
    return +(location.href === wanted);
  }
  // Back or forward (or a link to a #fragment): the new path into C. Listened for from start, by an
  // app that links src/cdom_url.c (its export is there).
  function urlChanged() {
    const path = textEncoder.encode(urlOf(URL_PARTS.length));
    deliver((arg, ptr, length) => exports.cd_url_changed(ptr, length), 0, path);
  }
  // Back (-1) or forward (1) through the history; popstate brings the new path to C.
  function url_step(delta) {
    history.go(delta);
  }
  function title(textPtr, textLen) {
    doc.title = utf8(textPtr, textLen);
  }

  // The user's languages and display preferences (src/cdom_locale.c): read here, used by C.
  const PREFS_DARK = 1;
  const PREFS_REDUCED_MOTION = 2;
  const DARK_QUERY = "(prefers-color-scheme: dark)";
  const REDUCED_MOTION_QUERY = "(prefers-reduced-motion: reduce)";
  const prefQueries = []; // the media query lists listened to, held so they live as long as the page
  // The languages in the user's order: navigator.languages, else navigator.language alone.
  function languageList() {
    if (navigator.languages && navigator.languages.length) {
      return navigator.languages;
    }
    return navigator.language ? [navigator.language] : [];
  }
  // The index-th language's tag into C memory; returns its length, 0 past the last (no tag).
  function language(index, destPtr, capacity) {
    return writeString(languageList()[index] ?? "", destPtr, capacity);
  }
  // A media query list, or null in a browser without matchMedia.
  function mediaQuery(query) {
    return window.matchMedia ? matchMedia(query) : null;
  }
  function matches(query) {
    const list = mediaQuery(query);
    return list ? list.matches : false;
  }
  // The preferences as C's bits: dark, reduced motion.
  function prefs() {
    const dark = matches(DARK_QUERY) ? PREFS_DARK : 0;
    const reducedMotion = matches(REDUCED_MOTION_QUERY) ? PREFS_REDUCED_MOTION : 0;
    return dark | reducedMotion;
  }
  function prefsChanged() {
    exports.cd_prefs_changed(prefs());
  }
  // Listened for from start, by an app that links src/cdom_locale.c (its export is there).
  function listenPrefs() {
    for (const query of [DARK_QUERY, REDUCED_MOTION_QUERY]) {
      const list = mediaQuery(query);
      if (list) {
        list.addEventListener("change", prefsChanged);
        prefQueries.push(list);
      }
    }
  }

  // The clipboard and the share sheet (src/cdom_share.c): C asks with a token, and the answer is
  // one of C's status codes, always from the event loop. The strings are read during the call.
  const SHARE_FAILED = 0;
  const SHARE_DONE = 1;
  const SHARE_UNAVAILABLE = 2;
  const SHARE_CANCELLED = 3;
  // What a rejection means: the user closed the share sheet (an AbortError), or anything else.
  function shareFailure(error) {
    return error && error.name === "AbortError" ? SHARE_CANCELLED : SHARE_FAILED;
  }
  // Answers answer(token, status) once the browser's promise settles; no promise: the browser has
  // no such API, answered unavailable all the same from the event loop.
  function shareAnswer(answer, token, promise) {
    let status = Promise.resolve(SHARE_UNAVAILABLE);
    if (promise) {
      status = promise.then(() => SHARE_DONE, shareFailure);
    }
    status.then((settled) => answer(token, settled));
  }
  function clipboard_write(token, textPtr, textLen) {
    const text = utf8(textPtr, textLen);
    const clipboard = navigator.clipboard;
    const writing = clipboard && clipboard.writeText ? clipboard.writeText(text) : null;
    shareAnswer(exports.cd_clipboard_done, token, writing);
  }
  // A title and a url, each left out when C gives none.
  function share(token, titlePtr, titleLen, urlPtr, urlLen) {
    const data = {};
    if (titleLen) {
      data.title = utf8(titlePtr, titleLen);
    }
    if (urlLen) {
      data.url = utf8(urlPtr, urlLen);
    }
    const sharing = navigator.share ? navigator.share(data) : null;
    shareAnswer(exports.cd_share_done, token, sharing);
  }

  // The page's fonts (src/cdom_fonts.c): C asks to hear when they are ready, with a deadline, and
  // hears when a load is done.
  const FONTS_TIMEOUT = 0;
  const FONTS_READY = 1;
  const FONTS_UNAVAILABLE = 2;
  // Answers once with token, from the event loop: ready, unavailable without document.fonts, or
  // timeout when deadlineMs passes first (0: no deadline).
  function fonts_wait(token, deadlineMs) {
    let answered = false;
    const answer = (status) => {
      if (answered) {
        return;
      }
      answered = true;
      exports.cd_fonts_done(token, status);
    };
    if (!doc.fonts) {
      Promise.resolve().then(() => answer(FONTS_UNAVAILABLE));
      return;
    }
    if (deadlineMs) {
      setTimeout(() => answer(FONTS_TIMEOUT), deadlineMs);
    }
    doc.fonts.ready.then(() => answer(FONTS_READY));
  }
  function fontsChanged() {
    exports.cd_fonts_changed();
  }

  // Pixels in C memory onto a canvas at 0,0, read now.
  function blit(id, pixelsPtr, width, height) {
    const canvas = nodes[id];
    canvas.$g ??= canvas.getContext("2d");
    canvas.$g.putImageData(pixels(pixelsPtr, width, height, 4, canvas.$g), 0, 0);
  }

  // Host access along dotted paths (cdom.h); a "--" segment is a custom property of the style object
  // before it. A literal path is split once and kept by its slot; any other path is split each time.
  const segments = []; // a literal path's segments by slot index
  function segmentsOf(path) {
    if (!path) {
      return [];
    }
    if (path < litBase || path >= litEnd) {
      // A runtime string or a read-only pointer (a path through a helper): split each time.
      return str(path).split(".");
    }
    return segments[(path - litBase) >> 2] ??= str(path).split(".");
  }
  // Walks count segments from object; a null step stops the walk, so a missing value costs no
  // exception (get and the setters still catch, for getters and setters that throw).
  function walkPath(object, keys, count) {
    for (let i = 0; i < count && object != null; i++) {
      const key = keys[i];
      object = key.startsWith("--") ? object.getPropertyValue(key) : object[key];
    }
    return object;
  }
  // One cd_val at entry index of the array at ptr (a tag and a number or a payload, where cd_layout
  // says); an object's pairs follow it. Returns [value, index of the next entry].
  function valueAt(ptr, index) {
    const at = (ptr >> 2) + index * valWords;
    const tag = load32(at + valTag);
    const payload = load32(at + valU);
    switch (tag) {
      case 0: return [loadF64((at + valNum) >> 1), index + 1];
      case 1: return [str(payload | 0), index + 1];
      case 2: return [payload === 1, index + 1];
      case 3: return [nodes[payload], index + 1];
      case 5: {
        const object = {};
        let next = index + 1;
        for (let pair = 0; pair < payload; pair++) {
          const [key, afterKey] = valueAt(ptr, next);
          const [value, afterValue] = valueAt(ptr, afterKey);
          object[key] = value;
          next = afterValue;
        }
        return [object, next];
      }
      default: return [null, index + 1];
    }
  }
  // Sets the value at valuePtr on the path's last segment. A failure is reported, not thrown, so C
  // goes on.
  function set(id, path, valuePtr) {
    if (stale()) {
      views();
    }
    const value = valueAt(valuePtr, 0)[0];
    try {
      const keys = segmentsOf(path);
      const object = walkPath(nodes[id], keys, keys.length - 1);
      const property = keys[keys.length - 1];
      if (property.startsWith("--")) {
        object.setProperty(property, value);
      } else {
        object[property] = value;
      }
    } catch (error) {
      reportError(error);
    }
  }
  // Calls the method at the path's last segment with count values from argsPtr; the result is kept
  // as CD_RESULT, a returned promise's rejection is swallowed and a thrown error is reported.
  function call(id, method, count, argsPtr) {
    if (stale()) {
      views();
    }
    const args = [];
    for (let i = 0, next = 0; i < count; i++) {
      const [value, after] = valueAt(argsPtr, next);
      args.push(value);
      next = after;
    }
    try {
      const keys = segmentsOf(method);
      const object = walkPath(nodes[id], keys, keys.length - 1);
      const result = object[keys[keys.length - 1]](...args);
      nodes[RESULT] = result;
      if (result && typeof result.catch === "function") {
        result.catch(() => {});
      }
    } catch (error) {
      nodes[RESULT] = undefined;
      reportError(error);
    }
  }
  // The import behind cd_get_* and cd_input_whole: kind 0 a number, 1 a string into C memory (its
  // bytes written), 2 a node id, 3 a string into C memory (its whole length, cut or not). Never throws.
  function get(id, path, kind, destPtr, capacity) {
    let value;
    try {
      const keys = segmentsOf(path);
      value = walkPath(nodes[id], keys, keys.length);
    } catch {
      // a throwing getter reads as missing
    }
    if (kind === 0) {
      return typeof value === "boolean" ? +value : typeof value === "number" ? value : NaN;
    }
    if (kind === 1 || kind === 3) {
      const text = value == null ? "" : String(value);
      const { read, written } = encodeTo(destPtr, capacity, text);
      return kind === 1 || read === text.length ? written : written + textEncoder.encode(text.slice(read)).length;
    }
    if (value == null) {
      return -1;
    }
    const index = nodes.indexOf(value);
    return index === RESULT ? nodes.indexOf(value, RESULT + 1) : index; // CD_RESULT is no name
  }

  // cd_seed for the app: options.seed when the page fixes one (a test, a replay: one line makes a
  // run deterministic), else 32 bits from the browser's cryptographic generator.
  function seed(options) {
    if (options && options.seed !== undefined) {
      return options.seed >>> 0;
    }
    if (globalThis.crypto && crypto.getRandomValues) {
      return crypto.getRandomValues(new Uint32Array(1))[0];
    }
    // The one exception to cdom's seed rule (no unseedable generator, no clock seeds), approved for IE 9 and 10:
    // they have no cryptographic generator, and a static page has no server to send a seed, so every source
    // left is one the rule bans. options.seed still overrides it, so a run stays one line from deterministic.
    // Below IE 11, cd_seed is therefore neither secret nor unpredictable (cdom.h: never use it for secrets).
    // tools/seeds.mjs and tests/unit.html allow exactly this one draw from the engine's generator, here, and no other.
    return (Math.random() * 4294967296 ^ Date.now()) >>> 0;
  }

  // The probe's bytes. On the WebAssembly path alone, which every browser without typed arrays lacks, so the IE 9
  // build allows its typed array here (tools/legacy/memory.mjs).
  function simdProbe() {
    return new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 1, 5, 1, 96, 0, 1, 123, 3, 2, 1, 0, 10, 8, 1, 6, 0, 65, 0, 253, 15, 11]);
  }
  // Starts the app, once per page (README.md, "Starting an app"): url is the wasm's, a pair of them
  // [scalar, SIMD] (cdom.mk's APP_SIMD), or the wasm2js build of the same app (a function of the imports
  // that returns the exports, for a browser without WebAssembly; shim/boot.js picks one). env: the module's own "env" imports.
  // mount is an element, or the id of one that may not be parsed yet: then the
  // wasm loads while the parser and the stylesheets run, and the app starts once the DOM is ready.
  // options.openStore: an app that reads its store as it starts opens it now, while the wasm is
  // fetched and compiled, so its first get finds the database open. options.seed: cd_seed (seed()).
  async function start(url, mount, env, options) {
    // The page's string table, written before the shim by the build (tools/cdpage).
    const table = globalThis.cdomLits;
    if (!table) {
      throw new Error("cdom: this page has no string table (build it with cdom.mk or tools/cdpage, which write it before the shim)");
    }
    if (table[2] !== "\u2603") throw new Error("cdom: this page was not read as UTF-8");
    if (options && options.openStore) {
      openDatabase().catch(() => {}); // a failure shows again, as a miss, at the first get
    }
    const imports = {
      cdom: {
        ahead, at, open, close, text, text_num, attr, class: setClass, name_el, name_text, name_open,
        listen, value, value_num, tpl_save, clone, slot, tpl_text, tpl_text_num, walk, walk_value, walk_value_num,
        set_text, set_text_num, set_attr, set_class, style, style_num, encode, image_bytes, remove, free, move, clear,
        detach, attach, unlisten, hit, viewport, rect, blit, set, call, get, timer, timer_stop, raf, now, ws_open, ws_send, ws_close, ws_buffered, http, epoch, origin,
        file_read, file_open, file_next, file_close, job_run, job_stop, image_call, image_throw,
        store_put, store_get, store_del,
        audio_load, audio_free, audio_play, audio_stop, audio_gain, audio_master, audio_state,
        random,
        url_read, url_go, url_step, title,
        language, prefs,
        clipboard_write, share,
        fonts_wait,
      },
      env: env || {},
    };
    // A pair [scalar, SIMD] (cdom.mk's APP_SIMD): the SIMD module where this browser validates a wasm SIMD instruction
    // (tools/simd-probe.wat, these bytes), else the scalar one; only the one picked is fetched and compiled.
    if (Array.isArray(url)) {
      url = url[+WebAssembly.validate(simdProbe())];
    }
    let instance;
    if (typeof url === "function") {
      copiedOnGrow = true;
      instance = { exports: url(imports) };
    } else {
      const response = fetch(url);
      if (WebAssembly.instantiateStreaming) {
        instance = (await WebAssembly.instantiateStreaming(response, imports)).instance;
      } else {
        instance = (await WebAssembly.instantiate(await (await response).arrayBuffer(), imports)).instance;
      }
    }
    if (typeof mount === "string") {
      if (doc.readyState === "loading") {
        await new Promise((resolve) => doc.addEventListener("DOMContentLoaded", resolve, { once: true }));
      }
      mount = doc.getElementById(mount);
    }
    nodes[0] = mount;
    nodes[WINDOW] = window;
    nodes[DOCUMENT] = doc;
    nodes[BODY] = doc.body;
    // Every call into C is counted, so an event the browser dispatches during one waits (post).
    exports = {};
    for (const [key, raw] of Object.entries(instance.exports)) {
      exports[key] = typeof raw !== "function" ? raw : (...args) => {
        busy++;
        try {
          return raw(...args);
        } finally {
          if (--busy === 0) {
            callEnded();
          }
        }
      };
    }
    memory = exports.memory;
    if (!exports.cd_lits || instance.exports.cd_lits() >>> 0 !== table[1] >>> 0) {
      throw new Error("cdom: the page's string table is not this module's (the page and the module are from different builds, or the module did not go through cdlits)");
    }
    checkAhead = !!(options && options.checkAhead);
    lits = table[3].split("\0");
    litBase = table[0];
    litEnd = litBase ? litBase + 4 * lits.length : 0;
    onRoRead = (options && options.onRoRead) || null;
    views();
    readLayout(instance.exports.cd_layout() >> 2);
    if (exports.cd_url_changed) {
      addEventListener("popstate", urlChanged);
    }
    if (exports.cd_prefs_changed) {
      listenPrefs();
    }
    if (exports.cd_fonts_changed && doc.fonts) {
      doc.fonts.addEventListener("loadingdone", fontsChanged);
    }
    exports.cd_start(seed(options));
    return exports;
  }
  // Reads cd_layout's block at word at: its entry count, then the entries in CD_LAYOUT's order (src/cdom.c).
  // Byte offsets become word offsets, and the event record's fields indexes of their own, since the
  // record never moves. A count that is not the number read here is a shim from another cdom: it throws.
  function readLayout(at) {
    const next = () => load32(at++);
    const word = () => load32(at++) >> 2;
    const count = next();
    const end = at + count;
    const block = next();
    const tmpField = () => (block + load32(at++)) >> 2;
    tmpSerial0 = tmpField();
    tmpCount = tmpField();
    tmpRecords = block + load32(at++);
    recWords = word();
    recP = word();
    recLen = word();
    const record = next();
    const field = () => (record + load32(at++)) >> 2;
    evType = field();
    evId = field();
    evX = field();
    evY = field();
    evButton = field();
    evKey = field();
    evMods = field();
    evPath = field();
    evDepth = field();
    evCh = field();
    evMs = field();
    evPointer = field();
    evButtons = field();
    evDx = field();
    evDy = field();
    viewportW = word();
    viewportH = word();
    viewportHidden = word();
    viewportDpr = word();
    valWords = word();
    valTag = word();
    valNum = word();
    valU = word();
    if (at !== end) {
      throw new Error("cdom: this shim and the module's cdom.c lay out C memory differently");
    }
  }
  // node(id): the DOM node (or host object) named id, for page glue around the app.
  return { start, node: (id) => nodes[id] };
})();
