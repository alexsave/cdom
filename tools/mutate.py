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
  ('clone-detached', 'shim/cdom.js', 'function clone(t) { push(tpl[t].cloneNode(true)); }',
   'function clone(t) { const el = tpl[t].cloneNode(true); stack[sp++] = el; last = el; }', UNIT, 'FAIL clone inside'),
  ('svg-off', 'src/cdom.c', 'if (svg_depth || tag == svg_tag) {', 'if (0) {', UNIT, 'FAIL svg and its'),
  ('svg-no-pop', 'src/cdom.c', 'void cd_close(void) { if (svg_depth) svg_depth--;', 'void cd_close(void) {', UNIT, 'FAIL svg and its'),
  ('svg-clone-depth', 'src/cdom.c', 'void cd_clone(uint32_t t) { if (svg_depth) svg_depth++;', 'void cd_clone(uint32_t t) {', UNIT, 'FAIL svg and its'),
  ('timer-no-flush', 'src/cdom.c', '  cd_on_timer(token);\n  cd_flush();', '  cd_on_timer(token);', UNIT, 'FAIL the timer and frame exports flush'),
  ('raf-no-flush', 'src/cdom.c', '  cd_on_raf(token, t);\n  cd_flush();', '  cd_on_raf(token, t);', UNIT, 'FAIL the timer and frame exports flush'),
  ('ws-open-no-flush', 'src/cdom_net.c', '{ cd_on_ws_open(id); cd_flush(); }', '{ cd_on_ws_open(id); }', UNIT, 'FAIL net'),
  ('raf-no-time', 'shim/cdom.js', 'requestAnimationFrame((ts) => x.cd_raf(t, ts))', 'requestAnimationFrame(() => x.cd_raf(t, 0))', UNIT, 'FAIL a timer and a frame'),
  ('timer-token', 'shim/cdom.js', 'setTimeout(() => x.cd_timer(t), ms)', 'setTimeout(() => x.cd_timer(t + 1), ms)', UNIT, 'FAIL a timer and a frame'),
  ('event-no-flush', 'src/cdom.c', '  cd_on_event(&inbox);\n  cd_flush();', '  cd_on_event(&inbox);', UNIT, 'FAIL resize'),
  ('resize-size', 'shim/cdom.js', 'clientX: innerWidth, clientY: innerHeight', 'clientX: innerHeight, clientY: innerWidth', UNIT, 'FAIL resize'),
  ('visibility-target', 'shim/cdom.js', '(EVENTS[ev] === "resize" ? window : doc)', 'window', UNIT, 'FAIL visibilitychange'),
  ('viewport-order', 'shim/cdom.js', 'i32.set([innerWidth, innerHeight, +doc.hidden], p >> 2)', 'i32.set([innerHeight, innerWidth, +doc.hidden], p >> 2)', UNIT, 'FAIL the viewport'),
  ('ws-bytes', 'shim/cdom.js', 'new Uint8Array(mem.buffer, p, b.length).set(b); f(a, p, b.length);', 'new Uint8Array(mem.buffer, p, b.length).set(b.reverse()); f(a, p, b.length);', UNIT, 'FAIL net'),
  ('http-no-body', 'shim/cdom.js', 'body: bn ? new Uint8Array(mem.buffer, bp, bn).slice() : undefined', 'body: undefined', UNIT, 'FAIL net'),
  ('net-no-flush', 'src/cdom_net.c', '  cd_on_http(token, status, p, len);\n  cd_flush();', '  cd_on_http(token, status, p, len);', UNIT, 'FAIL net'),
  ('no-end-anchor', 'src/cdom.c', 'cd_id end = L->end; /*', 'cd_id end = 0; /*', UNIT, 'FAIL a keyed list with a fixed sibling'),
  ('no-end-anchor-fuzz', 'src/cdom.c', 'cd_id end = L->end; /*', 'cd_id end = 0; /*', FUZZ, 'the anchor is not last'),
  ('end-anchor-moves', 'src/cdom.c', 'cd_move(old[os].node, ne < n ? nw[ne].node : end);\n      nw[--ne]', 'cd_move(old[os].node, ne < n ? nw[ne].node : 0);\n      nw[--ne]', FUZZ, 'the anchor is not last'),
  ('forget-keeps-ids', 'src/cdom.c', '    cd_id_free(L->items[i].node);\n    f->destroy', '    f->destroy', UNIT, 'FAIL lists and regions'),
  ('region-keeps-ids', 'src/cdom.c', '      for (uint32_t i = 1; i < r->n; i++) cd_id_free(r->ids[i]);', '', UNIT, 'FAIL lists and regions'),
  ('region-records-list', 'src/cdom.c', '  recording = 0;\n  cd_id end = L->end;', '  cd_id end = L->end;', UNIT, 'FAIL lists and regions'),
  ('double-free-silent', 'src/cdom.c', '  if (id >= next_id || !alive[id]) __builtin_trap();', '', UNIT, 'FAIL freeing a free id traps'),
  ('name-open-is-last', 'shim/cdom.js', 'function name_open(n) { nodes[n] = stack[sp - 1]; }', 'function name_open(n) { nodes[n] = last; }', UNIT, 'FAIL cd_name_open'),
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
  ('image-pixels', 'shim/cdom.js', 'new ImageData(new Uint8ClampedArray(mem.buffer, ptr, w * h * 4), w, h)', 'new ImageData(new Uint8ClampedArray(mem.buffer, ptr + 4, w * h * 4), w, h)', UNIT, 'FAIL cd_set_image'),
  ('image-token', 'shim/cdom.js', 'post(14, id, { keyCode: t }, 0, 0)', 'post(14, id, { keyCode: 0 }, 0, 0)', UNIT, 'FAIL cd_set_image'),
  ('now-zero', 'shim/cdom.js', 'const now = () => performance.now();', 'const now = () => 0;', UNIT, 'FAIL cd_now'),
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
