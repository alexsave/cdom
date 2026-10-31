#!/usr/bin/env python3
"""Mutation check of the port-fix tests: each mutant breaks one thing, the test written for it must
fail for that reason (its message is matched), and the source is restored after every mutant, even
on an error. Usage: python3 tools/mutate.py [name ...]"""
import subprocess, sys, pathlib

top = pathlib.Path(__file__).resolve().parent.parent
UNIT = ['node', 'tools/unit.mjs']
FUZZ = ['node', 'tools/fuzz.mjs']
LINK = ['tests/link/run.sh']

# name, file, old, new, test command, text the failure must contain
M = [
  ('clone-detached', 'shim/cdom.js', '    push(templates[templateId].cloneNode(true));',
   '    const element = templates[templateId].cloneNode(true);\n    stack[depth++] = element;\n    last = element;', UNIT, 'FAIL clone inside'),
  ('svg-off', 'src/cdom.c', 'if (svg_depth || tag == svg_tag) {', 'if (0) {', UNIT, 'FAIL svg and its'),
  ('svg-no-pop', 'src/cdom.c', 'void cd_close(void) { if (svg_depth) svg_depth--;', 'void cd_close(void) {', UNIT, 'FAIL svg and its'),
  ('svg-clone-depth', 'src/cdom.c', 'void cd_clone(uint32_t t) { if (svg_depth) svg_depth++;', 'void cd_clone(uint32_t t) {', UNIT, 'FAIL svg and its'),
  ('timer-no-flush', 'src/cdom.c', '  cd_on_timer(token);\n  cd_flush();', '  cd_on_timer(token);', UNIT, 'FAIL the timer and frame exports flush'),
  ('raf-no-flush', 'src/cdom.c', '  cd_on_raf(token, t);\n  cd_flush();', '  cd_on_raf(token, t);', UNIT, 'FAIL the timer and frame exports flush'),
  ('ws-open-no-flush', 'src/cdom_net.c', '{ cd_on_ws_open(id); cd_flush(); }', '{ cd_on_ws_open(id); }', UNIT, 'FAIL net'),
  ('raf-no-time', 'shim/cdom.js', 'requestAnimationFrame((time) => exports.cd_raf(token, time))', 'requestAnimationFrame(() => exports.cd_raf(token, 0))', UNIT, 'FAIL a timer and a frame'),
  ('timer-token', 'shim/cdom.js', 'setTimeout(() => exports.cd_timer(token), ms)', 'setTimeout(() => exports.cd_timer(token + 1), ms)', UNIT, 'FAIL a timer and a frame'),
  ('event-no-flush', 'src/cdom.c', '  cd_on_event(&inbox);\n  cd_flush();', '  cd_on_event(&inbox);', UNIT, 'FAIL resize'),
  ('resize-size', 'shim/cdom.js', 'clientX: innerWidth, clientY: innerHeight', 'clientX: innerHeight, clientY: innerWidth', UNIT, 'FAIL resize'),
  ('visibility-target', 'shim/cdom.js', '    return type === RESIZE ? window : doc;', '    return window;', UNIT, 'FAIL visibilitychange'),
  ('viewport-dpr', 'shim/cdom.js', 'Math.round(devicePixelRatio * 100)', '100', UNIT, 'FAIL the device pixel ratio'),
  ('viewport-order', 'shim/cdom.js', 'i32.set([innerWidth, innerHeight, +doc.hidden,', 'i32.set([innerHeight, innerWidth, +doc.hidden,', UNIT, 'FAIL the viewport'),
  ('ws-bytes', 'shim/cdom.js', 'bytes(ptr, incoming.length).set(incoming);', 'bytes(ptr, incoming.length).set(incoming.reverse());', UNIT, 'FAIL net'),
  ('http-no-body', 'shim/cdom.js', 'body: bodyLen ? copyBytes(bodyPtr, bodyLen) : undefined,', 'body: undefined,', UNIT, 'FAIL net'),
  ('http-no-headers', 'shim/cdom.js', 'headers: headerList(headersPtr, headersLen),', 'headers: [],', UNIT, 'FAIL headers'),
  ('http-status', 'shim/cdom.js', 'status = response.status;', 'status = 200;', UNIT, 'FAIL headers'),
  ('http-no-deadline', 'shim/cdom.js', 'const timer = deadlineMs ? setTimeout(() => controller.abort(), deadlineMs) : 0;', 'const timer = 0;', UNIT, 'FAIL deadline'),
  ('http-deadline-dropped', 'src/cdom_net.c', '  j_http(token, method, mlen, url, ulen, headers, hlen, body, blen, deadline_ms);', '  j_http(token, method, mlen, url, ulen, headers, hlen, body, blen, 0 * deadline_ms);', UNIT, 'FAIL deadline'),
  ('http-abort-is-failure', 'shim/cdom.js', 'controller.signal.aborted ? 1 : 0', '0', UNIT, 'FAIL deadline'),
  ('http-failure-is-abort', 'shim/cdom.js', 'controller.signal.aborted ? 1 : 0', '1', UNIT, 'FAIL never arrived'),
  ('ws-ids-grow', 'shim/cdom.js', 'const id = freeSockets.length ? freeSockets.pop() : sockets.length;', 'const id = sockets.length;', UNIT, 'FAIL sockets'),
  ('ws-buffered-zero', 'shim/cdom.js', '    return sockets[id].bufferedAmount;', '    return 0;', UNIT, 'FAIL buffered'),
  ('ws-send-late', 'shim/cdom.js', '    sockets[id].send(bytes(bytesPtr, length));', '    setTimeout(() => sockets[id].send(bytes(bytesPtr, length)));', UNIT, 'FAIL net'),
  ('epoch-page-clock', 'shim/cdom.js', '    return Date.now();', '    return performance.now();', UNIT, 'FAIL cd_epoch_ms'),
  ('net-no-flush', 'src/cdom_net.c', '  cd_on_http(token, status, p, len);\n  cd_flush();', '  cd_on_http(token, status, p, len);', UNIT, 'FAIL net'),
  ('no-end-anchor', 'src/cdom.c', 'cd_id end = L->end; /*', 'cd_id end = 0; /*', UNIT, 'FAIL a keyed list with a fixed sibling'),
  ('no-end-anchor-fuzz', 'src/cdom.c', 'cd_id end = L->end; /*', 'cd_id end = 0; /*', FUZZ, 'the anchor is not last'),
  ('end-anchor-moves', 'src/cdom.c', 'cd_move(old[os].node, ne < n ? nw[ne].node : end);\n      nw[--ne]', 'cd_move(old[os].node, ne < n ? nw[ne].node : 0);\n      nw[--ne]', FUZZ, 'the anchor is not last'),
  ('forget-keeps-ids', 'src/cdom.c', '    cd_id_free(L->items[i].node);\n    f->destroy', '    f->destroy', UNIT, 'FAIL lists and regions'),
  ('region-keeps-ids', 'src/cdom.c', '      for (uint32_t i = 1; i < r->n; i++) cd_id_free(r->ids[i]);', '', UNIT, 'FAIL lists and regions'),
  ('region-records-list', 'src/cdom.c', '  recording = 0;\n  cd_id end = L->end;', '  cd_id end = L->end;', UNIT, 'FAIL lists and regions'),
  ('double-free-silent', 'src/cdom.c', '  if (id >= next_id || !alive[id]) __builtin_trap();', '', UNIT, 'FAIL freeing a free id traps'),
  ('name-open-is-last', 'shim/cdom.js', '    nodes[id] = stack[depth - 1];', '    nodes[id] = last;', UNIT, 'FAIL cd_name_open'),
  ('style-unit', 'src/cdom.c', 'u = ref_name(unit_ids[unit]);', 'u = ref_name(unit_ids[unit ? 1 : 0]);', UNIT, 'FAIL cd_set_style'),
  ('tmp-interned', 'src/cdom.c', '  uint32_t dest = TRANSIENT + ntmp++;\n  push_raw((const uint8_t *)s, len, utf16_len(s, len), dest);\n  return dest;',
   '  return cd_intern(s, len);', UNIT, 'FAIL cd_set_style'),
  ('tmp-unchecked', 'src/cdom.c', '  if (s - TRANSIENT >= ntmp) __builtin_trap();', '', UNIT, 'FAIL a transient used after'),
  ('lit-one-slot', 'src/cdom.h', '#define CD_LIT(lit) ({ static cd_str cd_lit_slot_;', '__attribute__((unused)) static cd_str cd_lit_slot_;\n#define CD_LIT(lit) ({', UNIT, 'FAIL CD_LIT'),
  ('lit-any-expr', 'src/cdom.h', 'cd_lit_(&cd_lit_slot_, "" lit, sizeof(lit) - 1)', 'cd_lit_(&cd_lit_slot_, lit, sizeof(lit) - 1)', LINK, 'FAIL CD_LIT accepted a variable'),
  ('fmt-zeros', 'src/cdom.c', '  while (lo < dec && t[lo] == \'0\') lo++;', '', UNIT, 'FAIL cd_fmt'),
  ('fmt-round', 'src/cdom.c', '(v < 0 ? -0.5 : 0.5)', '0.5', UNIT, 'FAIL cd_fmt'),
  ('two-heaps', 'src/cdom.c', 'void *cd_alloc(uint32_t n) {\n  void *p = malloc(n);', 'void *cd_alloc(uint32_t n) {\n  void *p = heap_alloc(n);', LINK, "FAIL the app's heap"),
  ('foreign-free', 'src/cdom.c', '  if ((uintptr_t)p < heap_lo + 8 || (uintptr_t)p >= heap_ptr) __builtin_trap(); /* not from this heap */', '', LINK, 'FAIL an app that replaces malloc alone'),
  ('image-pixels', 'shim/cdom.js', 'new Uint8ClampedArray(memory.buffer, pixelsPtr, width * height * channels)', 'new Uint8ClampedArray(memory.buffer, pixelsPtr + 4, width * height * channels)', UNIT, 'FAIL cd_encode_image'),
  ('image-token', 'shim/cdom.js', 'deliver(exports.cd_encoded, token, await blob.arrayBuffer())', 'deliver(exports.cd_encoded, token + 1, await blob.arrayBuffer())', UNIT, 'FAIL cd_encode_image'),
  ('image-rgb', 'shim/cdom.js', 'rgba[to + 1] = pixels[from + 1];', 'rgba[to + 1] = pixels[from + 2];', UNIT, 'FAIL cd_encode_image_in with RGB'),
  ('image-place', 'shim/cdom.js', 'putImageData(new ImageData(pixels, width, height), x, y)', 'putImageData(new ImageData(pixels, width, height), 0, 0)', UNIT, 'FAIL cd_encode_image_in with RGB'),
  ('image-bytes', 'shim/cdom.js', 'new Blob([copyBytes(bytesPtr, length)]', 'new Blob([copyBytes(bytesPtr + 1, length)]', UNIT, 'FAIL cd_encode_image'),
  ('image-set-early', 'shim/cdom.js', '    image.src = url;\n', '    image.src = url;\n    node.style.setProperty(name, `url(${url})`);\n', UNIT, 'FAIL cd_set_image_bytes sets the url once'),
  ('image-unanswered', 'shim/cdom.js', '      exports.cd_image(token, 1);', '', UNIT, 'FAIL cd_set_image_bytes sets the url once'),
  ('store-put-bytes', 'shim/cdom.js', 'const data = copyBytes(bytesPtr, length);', 'const data = copyBytes(bytesPtr, length).reverse();', UNIT, 'FAIL cd_store: a put'),
  ('store-not-early', 'shim/cdom.js', '    if (options && options.openStore) {', '    if (false) {', UNIT, 'FAIL options.openStore'),
  ('store-put-unreported', 'shim/cdom.js', '() => exports.cd_store_put_done(token, 0));', '() => exports.cd_store_put_done(token, 1));', UNIT, 'FAIL cd_store: a put answers'),
  ('store-error-is-bytes', 'shim/cdom.js', '.then(answer, () => answer());', '.then(answer, () => answer(new Uint8Array(1)));', UNIT, 'FAIL cd_store: a get that fails'),
  ('store-key-late', 'shim/cdom.js', '(store) => store.put(data, key))', '(store) => store.put(data, utf8(keyPtr, keyLen)))', UNIT, 'FAIL cd_store: the key is read'),
  ('store-del-noop', 'shim/cdom.js', '(store) => store.delete(key))', '(store) => store.get(key))', UNIT, 'FAIL cd_store_del: the key misses'),
  ('store-del-unreported', 'shim/cdom.js', '() => exports.cd_store_del_done(token, 0));', '() => exports.cd_store_del_done(token, 1));', UNIT, 'FAIL cd_store_del: a delete that fails'),
  ('store-miss', 'shim/cdom.js', 'exports.cd_store_got(token, 0, -1)', 'exports.cd_store_got(token, 0, 0)', UNIT, 'FAIL cd_store: a missing'),
  ('store-no-deadline', 'shim/cdom.js', 'setTimeout(answer, ms);', '', UNIT, 'FAIL cd_store: a get not answered'),
  ('store-no-flush', 'src/cdom_store.c', '  cd_on_store(token, p, len);\n  cd_flush();', '  cd_on_store(token, p, len);', UNIT, 'FAIL cd_store: a put'),
  ('rect-order', 'shim/cdom.js', '.set([box.left, box.top, box.width, box.height])', '.set([box.top, box.left, box.width, box.height])', UNIT, 'FAIL cd_rect'),
  ('rect-untransformed', 'shim/cdom.js', 'const box = nodes[id].getBoundingClientRect();', 'const node = nodes[id];\n    const box = { left: node.offsetLeft, top: node.offsetTop, width: node.offsetWidth, height: node.offsetHeight };', UNIT, 'FAIL cd_rect'),
  ('now-zero', 'shim/cdom.js', '    return performance.now();', '    return 0;', UNIT, 'FAIL cd_now'),
  ('input-no-prevent', 'shim/cdom.js', '    if (i32[record + 10] && event.preventDefault) {', '    if (false) {', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-touch-point', 'shim/cdom.js', '    return event.changedTouches ? event.changedTouches[0] : event;', '    return event;', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-mask-16', 'shim/cdom.js', '  let listenMask = new Uint32Array(1024);', '  let listenMask = new Uint16Array(1024);', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-win-viewport', 'shim/cdom.js', '    const viewportEvent = type === RESIZE || type === VISIBILITY;', '    const viewportEvent = true;', UNIT, 'FAIL the document'),
  ('input-unwin-noop', 'shim/cdom.js', '    windowTarget(type).removeEventListener(EVENTS[type], listener);', '', UNIT, 'FAIL the document'),
  ('input-win-twice', 'shim/cdom.js', '    if (windowListeners[type]) {\n      return;\n    }\n    const viewportEvent', '    const viewportEvent', UNIT, 'FAIL the document'),
  ('input-touchmove-passive', 'shim/cdom.js', 'listener, { passive: false });', 'listener);', UNIT, 'FAIL a touchmove on the document'),
  ('input-no-char', 'shim/cdom.js', 'key.length === 1 ? key.charCodeAt(0) : 0;', 'key.length === 1 ? 0 : 0;', UNIT, 'FAIL a keydown on the document'),
  ('input-no-editing', 'shim/cdom.js', '      | (editing(event.target) << 4);', ';', UNIT, 'FAIL a keydown on the document'),
  ('input-prevent-c', 'src/cdom.c', 'void cd_prevent_default(void) { inbox.prevent = 1; }', 'void cd_prevent_default(void) { }', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-no-time', 'shim/cdom.js', '    i32[record + 11] = Math.round(event.timeStamp || 0);', '    i32[record + 11] = Math.round(performance.now());', UNIT, 'FAIL a mousedown and a touchstart'),
  ('hit-bottom-first', 'shim/cdom.js', '    for (const node of doc.elementsFromPoint(x, y)) {', '    for (const node of [...doc.elementsFromPoint(x, y)].reverse()) {', UNIT, 'FAIL cd_hit'),
  ('hit-not-listening', 'shim/cdom.js', '      if (node.$i !== undefined && listenMask[node.$i]) {\n        return node.$i;', '      if (node.$i !== undefined) {\n        return node.$i;', UNIT, 'FAIL cd_hit'),
  ('hit-topmost-only', 'shim/cdom.js', '    for (const node of doc.elementsFromPoint(x, y)) {', '    for (const node of [doc.elementFromPoint(x, y)]) {', UNIT, 'FAIL cd_hit: through a layer'),
  ('mini-strong', 'src/mini.c', '__attribute__((weak, no_builtin)) int memcmp', '__attribute__((no_builtin)) int memcmp', LINK, "FAIL an app's own libc"),
]

def run(cmd):
  b = subprocess.run(['tests/build.sh'], cwd=top, capture_output=True, text=True)
  if b.returncode: return 1, 'BUILD FAILED ' + b.stderr[-400:]
  r = subprocess.run(cmd, cwd=top, capture_output=True, text=True)
  return r.returncode, r.stdout + r.stderr

only = set(sys.argv[1:])
bad = 0
for name, f, old, new, cmd, want in M:
  if only and name not in only: continue
  p = top / f
  src = p.read_text()
  if src.count(old) != 1:
    print(f'SKIP {name}: the text to mutate is not in {f} once'); bad += 1; continue
  try:
    p.write_text(src.replace(old, new))
    code, out = run(cmd)
  finally:
    p.write_text(src)
  caught = code != 0 and want in out
  print(('caught ' if caught else 'MISSED ') + name + ('' if caught else '\n  ' + out[-600:].replace('\n', '\n  ')))
  bad += not caught
code, out = run(UNIT)
print('restored: unit ' + ('ok' if code == 0 else 'FAIL'))
sys.exit(1 if bad or code else 0)
