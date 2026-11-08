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
    "fullscreenchange"];
  const RESIZE = 12;            // EVENTS index of the window's resize
  const VISIBILITY = 13;        // and of visibilitychange
  const WHEEL = 22;
  const FOCUS = 28;             // FOCUS to PAUSE do not bubble: the mount hears them in the capture phase
  const PAUSE = 37;
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
  const TRANSIENT = 0x40000000; // string ids from here up are one-off strings (cd_tmp)
  const DETACHED = 0xffffffff;  // "no insertion parent" for at()
  const SMALL = 64;             // string batches up to this many bytes are decoded char by char
  const SVG = "http://www.w3.org/2000/svg";
  const doc = document;
  const textDecoder = new TextDecoder();
  const textEncoder = new TextEncoder();
  const nodes = [];         // node by id
  const interned = [];      // interned strings by id
  const transient = [];     // one-off strings by id - TRANSIENT
  const templates = [];     // template root by template id
  const stack = [];         // the open elements
  const delegated = [];     // event types with a listener on the mount
  const templateSlots = []; // slot text nodes by template id
  let slots = [];           // slot text nodes of the template being built
  const hostListeners = [];  // the listener per (host object, event type), while listened
  let listenMask = new Uint32Array(2048); // listened event types per node id: two words, a bit per type
  const queue = [];         // events the browser dispatched while C was running, delivered after
  let busy = 0;             // how many calls into C are on the stack
  let exports;              // the wasm module's exports
  let memory;
  let buffer;               // memory.buffer when the views below were made
  let u8;
  let u32;
  let i32;
  let f32;
  let f64;
  let stringBytes;          // where C writes a batch of string bytes
  let stringMeta;           // where C writes (id, bytes, chars) per string in the batch
  let inbox;                // where the event record goes
  let depth = 0;            // how many elements are open
  let last = null;          // the node made or walked to last
  let root = null;          // the outermost element closed last
  let base = DETACHED;      // the parent new top-level nodes go into
  let before = 0;           // the node they go before (0: at the end)

  // Typed views over wasm memory, made again after it grows.
  function views() {
    buffer = memory.buffer;
    u8 = new Uint8Array(buffer);
    u32 = new Uint32Array(buffer);
    i32 = new Int32Array(buffer);
    f32 = new Float32Array(buffer);
    f64 = new Float64Array(buffer);
  }
  // A view of length bytes of C memory at ptr, good until the memory grows.
  function bytes(ptr, length) {
    return new Uint8Array(memory.buffer, ptr, length);
  }
  // A copy of them, which outlives the call and any growth.
  function copyBytes(ptr, length) {
    return bytes(ptr, length).slice();
  }
  // Puts node into parent before node next (null: at the end).
  function place(parent, node, next) {
    parent.insertBefore(node, next);
  }
  // A string by id, interned or one-off.
  function stringOf(id) {
    return id < TRANSIENT ? interned[id] : transient[id - TRANSIENT];
  }
  // Keeps string value under id.
  function keep(id, value) {
    if (id < TRANSIENT) {
      interned[id] = value;
    } else {
      transient[id - TRANSIENT] = value;
    }
  }

  // Decodes a batch of count strings (byteCount bytes in all): one TextDecoder call for the
  // batch, or a char loop when it is small.
  function decode(count, byteCount) {
    let meta = stringMeta >> 2;
    if (byteCount > SMALL) {
      const all = textDecoder.decode(u8.subarray(stringBytes, stringBytes + byteCount));
      let offset = 0;
      for (let i = 0; i < count; i++, meta += 3) {
        const chars = u32[meta + 2];
        keep(u32[meta], all.substring(offset, offset + chars));
        offset += chars;
      }
      return;
    }
    let at = stringBytes;
    for (let i = 0; i < count; i++, meta += 3) {
      const bytes = u32[meta + 1];
      let value;
      if (bytes === u32[meta + 2]) { // ASCII: one char per byte
        value = "";
        for (let k = 0; k < bytes; k++) {
          value += String.fromCharCode(u8[at + k]);
        }
      } else {
        value = textDecoder.decode(u8.subarray(at, at + bytes));
      }
      keep(u32[meta], value);
      at += bytes;
    }
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
  // focus events) waits for a microtask, so it never lands in the middle of a handler.
  function post(type, id, event, path, pathDepth) {
    if (busy) {
      if (queue.push([type, id, event, path, pathDepth]) === 1) {
        queueMicrotask(drain);
      }
      return;
    }
    if (buffer !== memory.buffer) {
      views();
    }
    const record = inbox >> 2;
    const key = event.key;
    const viewport = type === RESIZE || type === VISIBILITY;
    const point = pointOf(event);
    const pointer = fillsPointer[type] === 1; // other types skip the getters and read 0
    const wheel = type === WHEEL;
    i32[record] = type;
    i32[record + 1] = id;
    i32[record + 2] = viewport ? innerWidth : point.clientX | 0;
    i32[record + 3] = viewport ? innerHeight : point.clientY | 0;
    i32[record + 4] = event.button | 0;
    i32[record + 5] = viewport ? +doc.hidden : event.keyCode | 0;
    i32[record + 6] = event.shiftKey | (event.ctrlKey << 1) | (event.altKey << 2) | (event.metaKey << 3)
      | (editing(event.target) << 4);
    i32[record + 7] = path;
    i32[record + 8] = pathDepth;
    i32[record + 9] = typeof key === "string" && key.length === 1 ? key.charCodeAt(0) : 0;
    i32[record + 11] = Math.round(event.timeStamp || 0);
    i32[record + 12] = pointer ? event.pointerId : 0;
    i32[record + 13] = pointer ? event.buttons : 0;
    f32[record + 14] = wheel ? event.deltaX : 0;
    f32[record + 15] = wheel ? event.deltaY : 0;
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
  function drain() {
    for (const args of queue.splice(0)) {
      post(...args);
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
    mount.addEventListener(EVENTS[type], (event) => {
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
    }, { capture: type >= FOCUS && type <= PAUSE, passive: false });
  }

  // C sends a batch of new strings.
  function strings(count, byteCount) {
    if (buffer !== memory.buffer) {
      views();
    }
    decode(count, byteCount);
  }
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
    last = element;
  }
  // Opens an element; C says which tags are SVG.
  function open(tag) {
    push(doc.createElement(stringOf(tag)));
  }
  function open_svg(tag) {
    push(doc.createElementNS(SVG, stringOf(tag)));
  }
  // Closes the open element; the outermost one is placed.
  function close() {
    const element = stack[--depth];
    if (!depth) {
      root = element;
      placeNew(element);
    }
  }
  function text(id) {
    last = doc.createTextNode(stringOf(id));
    placeNew(last);
  }
  function text_num(number) {
    last = doc.createTextNode(number >>> 0);
    placeNew(last);
  }
  function attr(name, value) {
    last.setAttribute(stringOf(name), stringOf(value));
  }
  function setClass(value) {
    last.className = stringOf(value);
  }
  // Names the last node, or the open element, with a node id.
  function name(id) {
    nodes[id] = last;
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
      const grown = new Uint32Array(id * 4 + 2);
      grown.set(listenMask);
      listenMask = grown;
    }
    listenMask[id * 2 + (type >> 5)] |= 1 << (type & 31);
    if (!delegated[type]) {
      delegate(type);
    }
  }
  function unlisten(id, type) {
    if (id && id < EVENT) {
      unlistenHost(id, type);
    } else if (id * 2 + 1 < listenMask.length) {
      listenMask[id * 2 + (type >> 5)] &= ~(1 << (type & 31));
    }
  }
  function value(id) {
    last.nodeValue = stringOf(id);
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
    push(templates[templateId].cloneNode(true));
  }
  function slot(index) {
    slots[index] = last;
  }
  function tpl_text(templateId, index, id) {
    templateSlots[templateId][index].nodeValue = stringOf(id);
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
    last.nodeValue = stringOf(id);
  }
  function walk_value_num(from, steps, count, number) {
    walk(from, steps, count);
    last.nodeValue = number >>> 0;
  }
  function set_text(id, textId) {
    nodes[id].nodeValue = stringOf(textId);
  }
  function set_text_num(id, number) {
    nodes[id].nodeValue = number >>> 0;
  }
  function set_attr(id, name, value) {
    nodes[id].setAttribute(stringOf(name), stringOf(value));
  }
  function set_class(id, value) {
    nodes[id].className = stringOf(value);
  }
  // One style property.
  function style(id, property, value) {
    nodes[id].style.setProperty(stringOf(property), stringOf(value));
  }
  function style_num(id, property, number, unit) {
    nodes[id].style.setProperty(stringOf(property), number + stringOf(unit));
  }
  // Pixels in C memory (channels bytes each: RGBA, or RGB and opaque), at (x, y) in an image of
  // fullWidth x fullHeight, to an image file: copied now, encoded off the call, the file's bytes
  // back into C (cd_encoded) with token.
  function encode(pixelsPtr, width, height, channels, fullWidth, fullHeight, x, y, mime, token) {
    const canvas = doc.createElement("canvas");
    canvas.width = fullWidth;
    canvas.height = fullHeight;
    let pixels = new Uint8ClampedArray(memory.buffer, pixelsPtr, width * height * channels);
    if (channels === 3) {
      const rgba = new Uint8ClampedArray(width * height * 4);
      for (let from = 0, to = 0; to < rgba.length; from += 3, to += 4) {
        rgba[to] = pixels[from];
        rgba[to + 1] = pixels[from + 1];
        rgba[to + 2] = pixels[from + 2];
        rgba[to + 3] = 255;
      }
      pixels = rgba;
    }
    canvas.getContext("2d").putImageData(new ImageData(pixels, width, height), x, y);
    canvas.toBlob(async (blob) => deliver(exports.cd_encoded, token, await blob.arrayBuffer()), stringOf(mime), 0.92);
  }
  // An image file's bytes in C memory as a url(blob:) on a style property, set once the image has
  // decoded (so no frame paints the property with nothing behind it), then answered with
  // cd_image(token, 1); bytes that do not decode set nothing and answer 0.
  function image_bytes(id, property, bytesPtr, length, mime, token) {
    const node = nodes[id];
    const name = stringOf(property);
    const url = URL.createObjectURL(new Blob([copyBytes(bytesPtr, length)], { type: stringOf(mime) }));
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
  function forget(id) {
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
    if (buffer !== memory.buffer) {
      views();
    }
    i32.set([innerWidth, innerHeight, +doc.hidden, Math.round(devicePixelRatio * 100)], recordPtr >> 2);
  }
  // A node's box on screen into C memory as four doubles: left, top, width, height; 0 when id names
  // nothing with a box.
  function rect(id, outPtr) {
    const node = nodes[id];
    if (!node || !node.getBoundingClientRect) {
      return 0;
    }
    const box = node.getBoundingClientRect();
    new Float64Array(memory.buffer, outPtr, 4).set([box.left, box.top, box.width, box.height]);
    return 1;
  }
  function timer(ms, token) {
    setTimeout(() => exports.cd_timer(token), ms);
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
    bytes(destPtr, capacity).set(encoded.subarray(0, capacity));
    return encoded.length;
  }
  // Bytes into C memory, in room C makes with cd_bytes_buf, then answer(arg, ptr, length).
  function deliver(answer, arg, data) {
    const incoming = new Uint8Array(data);
    const ptr = exports.cd_bytes_buf(incoming.length);
    bytes(ptr, incoming.length).set(incoming);
    answer(arg, ptr, incoming.length);
  }

  // The network (src/cdom_net.c).
  const sockets = [];     // the open sockets by id
  const freeSockets = []; // ids whose sockets closed, handed out again first
  // Opens a WebSocket to the url in C memory; its events come back by socket id. The id is free
  // once the socket closed (before C hears it), so the table holds only the sockets open at once.
  function ws_open(urlPtr, urlLen) {
    const id = freeSockets.length ? freeSockets.pop() : sockets.length;
    const socket = new WebSocket(utf8(urlPtr, urlLen));
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
  // One request on the store in a transaction of its own; a promise of its result.
  async function storeRequest(mode, makeRequest) {
    const db = await openDatabase();
    return promised(makeRequest(db.transaction("kv", mode).objectStore("kv")));
  }
  // Keeps a copy of the bytes under the key, then answers with token: 1 kept, 0 failed. The key
  // and the bytes are read now: C's memory may hold something else once the database is open.
  function store_put(token, keyPtr, keyLen, bytesPtr, length) {
    const key = utf8(keyPtr, keyLen);
    const data = copyBytes(bytesPtr, length);
    storeRequest("readwrite", (store) => store.put(data, key)).then(
      () => exports.cd_store_put_done(token, 1),
      () => exports.cd_store_put_done(token, 0));
  }
  // Forgets the key, then answers with token: 1 done, 0 failed.
  function store_del(token, keyPtr, keyLen) {
    const key = utf8(keyPtr, keyLen);
    storeRequest("readwrite", (store) => store.delete(key)).then(
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
      if (data instanceof Uint8Array) {
        deliver(exports.cd_store_got, token, data);
      } else {
        exports.cd_store_got(token, 0, -1);
      }
    };
    setTimeout(answer, ms);
    // the error must not reach answer: only a miss may follow a failure
    storeRequest("readonly", (store) => store.get(key)).then(answer, () => answer());
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
    crypto.getRandomValues(bytes(bufferPtr, length));
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
    const pixels = new Uint8ClampedArray(memory.buffer, pixelsPtr, width * height * 4);
    canvas.$g.putImageData(new ImageData(pixels, width, height), 0, 0);
  }

  // Host access along dotted paths (cdom.h); a "--" segment is a custom property of the style object
  // before it. A path is an interned string, split once; a one-off string is split each time, since
  // its id is reused.
  const segments = []; // a path's segments by string id
  function segmentsOf(path) {
    if (!path) {
      return [];
    }
    if (path >= TRANSIENT) {
      return stringOf(path).split(".");
    }
    return segments[path] ??= stringOf(path).split(".");
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
  // One cd_val at entry index of the array at ptr (16 bytes each: tag at +0, payload at +8); an
  // object's pairs follow it. Returns [value, index of the next entry].
  function valueAt(ptr, index) {
    const at = (ptr >> 2) + index * 4;
    const tag = u32[at];
    const payload = u32[at + 2];
    switch (tag) {
      case 0: return [f64[(at + 2) >> 1], index + 1];
      case 1: return [stringOf(payload), index + 1];
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
    if (buffer !== memory.buffer) {
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
    if (buffer !== memory.buffer) {
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
  // The import behind cd_get_*: kind 0 a number, 1 a string into C memory, 2 a node id. Never throws.
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
    if (kind === 1) {
      return textEncoder.encodeInto(value == null ? "" : String(value), bytes(destPtr, capacity)).written;
    }
    if (value == null) {
      return -1;
    }
    const index = nodes.indexOf(value);
    return index === RESULT ? nodes.indexOf(value, RESULT + 1) : index; // CD_RESULT is no name
  }

  // cd_seed for the app: options.seed when the page fixes one (a test, a replay: one line makes a
  // run deterministic), else 32 bits from the browser's cryptographic generator, never from a
  // generator nothing can seed.
  function seed(options) {
    if (options && options.seed !== undefined) {
      return options.seed >>> 0;
    }
    return crypto.getRandomValues(new Uint32Array(1))[0];
  }

  // Starts the app: url is the wasm's, or the wasm2js build of the same app (a function of the imports
  // that returns the exports, for a browser without WebAssembly; shim/boot.js picks one).
  // mount is an element, or the id of one that may not be parsed yet: then the
  // wasm loads while the parser and the stylesheets run, and the app starts once the DOM is ready.
  // options.openStore: an app that reads its store as it starts opens it now, while the wasm is
  // fetched and compiled, so its first get finds the database open. options.seed: cd_seed (seed()).
  async function start(url, mount, env, options) {
    if (options && options.openStore) {
      openDatabase().catch(() => {}); // a failure shows again, as a miss, at the first get
    }
    const imports = {
      cdom: {
        strings, at, open, open_svg, close, text, text_num, attr, class: setClass, name, name_open,
        listen, value, value_num, tpl_save, clone, slot, tpl_text, tpl_text_num, walk, walk_value, walk_value_num,
        set_text, set_text_num, set_attr, set_class, style, style_num, encode, image_bytes, remove, free, move, clear,
        detach, attach, unlisten, hit, viewport, rect, blit, set, call, get, timer, raf, now, ws_open, ws_send, ws_close, ws_buffered, http, epoch,
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
    let instance;
    if (typeof url === "function") {
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
          busy--;
        }
      };
    }
    memory = exports.memory;
    views();
    const layout = exports.cd_layout() >> 2;
    stringBytes = u32[layout];
    stringMeta = u32[layout + 1];
    inbox = u32[layout + 2];
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
  // node(id): the DOM node (or host object) named id, for page glue around the app.
  return { start, node: (id) => nodes[id] };
})();
