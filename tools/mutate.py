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
  ('visibility-target', 'shim/cdom.js', 'const target = EVENTS[type] === "resize" ? window : doc;', 'const target = window;', UNIT, 'FAIL visibilitychange'),
  ('viewport-dpr', 'shim/cdom.js', 'Math.round(devicePixelRatio * 100)', '100', UNIT, 'FAIL the device pixel ratio'),
  ('viewport-order', 'shim/cdom.js', 'i32.set([innerWidth, innerHeight, +doc.hidden,', 'i32.set([innerHeight, innerWidth, +doc.hidden,', UNIT, 'FAIL the viewport'),
  ('ws-bytes', 'shim/cdom.js', 'new Uint8Array(memory.buffer, ptr, bytes.length).set(bytes);', 'new Uint8Array(memory.buffer, ptr, bytes.length).set(bytes.reverse());', UNIT, 'FAIL net'),
  ('http-no-body', 'shim/cdom.js', 'const body = bodyLen ? new Uint8Array(memory.buffer, bodyPtr, bodyLen).slice() : undefined;', 'const body = undefined;', UNIT, 'FAIL net'),
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
  ('image-bytes', 'shim/cdom.js', 'new Blob([new Uint8Array(memory.buffer, bytesPtr, length).slice()]', 'new Blob([new Uint8Array(memory.buffer, bytesPtr + 1, length).slice()]', UNIT, 'FAIL cd_encode_image'),
  ('store-put-bytes', 'shim/cdom.js', 'const bytes = new Uint8Array(memory.buffer, bytesPtr, length).slice();', 'const bytes = new Uint8Array(memory.buffer, bytesPtr, length).slice().reverse();', UNIT, 'FAIL cd_store: a put'),
  ('store-not-early', 'shim/cdom.js', '    if (options && options.openStore) {', '    if (false) {', UNIT, 'FAIL options.openStore'),
  ('store-miss', 'shim/cdom.js', 'exports.cd_store_got(token, 0, -1)', 'exports.cd_store_got(token, 0, 0)', UNIT, 'FAIL cd_store: a missing'),
  ('store-no-deadline', 'shim/cdom.js', 'setTimeout(() => answer(null), ms);', '', UNIT, 'FAIL cd_store: a get not answered'),
  ('store-no-flush', 'src/cdom_store.c', '  cd_on_store(token, p, len);\n  cd_flush();', '  cd_on_store(token, p, len);', UNIT, 'FAIL cd_store: a put'),
  ('now-zero', 'shim/cdom.js', '    return performance.now();', '    return 0;', UNIT, 'FAIL cd_now'),
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
