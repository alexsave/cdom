#!/usr/bin/env python3
"""Mutation check of the port-fix tests: each mutant breaks one thing, the test written for it must
fail for that reason (its message is matched), and the source is restored after every mutant, even
on an error. Mutants run in parallel, each worker in a private copy of the tracked tree (so the real
tree is never mutated, and an interrupted run leaves nothing behind); the test drivers serve on free
ports and Chrome takes a free debugging port, so the workers never collide.
A mutant whose text to mutate is not in its file exactly once stops the run before any mutant runs.
A test killed at TEST_TIMEOUT is not a catch, unless the mutant's entry ends in HANGS: the hang is then its
expected symptom (a C loop that never ends), and the timeout or its failure message both count.
Usage: python3 tools/mutate.py [-j N] [name ...]   (N workers, default half the cores)"""
import subprocess, sys, pathlib, os, shutil, tempfile, threading, queue, signal

top = pathlib.Path(__file__).resolve().parent.parent
# s: the test command alone (not the build). unit.mjs takes about 10 on a passing tree and waits at most 60 for a
# broken page; tests/link/run.sh and the wasm image suites take minutes on a loaded machine, and a test cut off
# there was never really run, so a timeout is a miss unless the entry says HANGS.
TEST_TIMEOUT = 600
HUNG = -999
HANGS = 'hangs' # an entry's optional seventh field: the mutant makes the test hang, and that is the catch
UNIT = ['node', 'tools/unit.mjs']
FUZZ = ['node', 'tools/fuzz.mjs']
LINK = ['tests/link/run.sh']
HOST = ['node', 'tools/host.mjs']
CANVAS = ['node', 'tools/canvas.mjs']
FRESH = ['node', 'tools/legacyfresh.mjs']
ALLOC = ['node', 'tools/alloc.mjs']
PAGE = ['tests/page/run.sh']
TUT = ['tutorial/run.sh']
FILE = ['node', 'tools/file.mjs']
LEG = ['node', 'tools/legacy.mjs']
LEG1 = ['env', 'LEGACY=1', 'node', 'tools/legacy.mjs']       # the second tier: wasm2js on typed arrays (IE 10, 11)
LEG9 = ['env', 'LEGACY=ie9', 'node', 'tools/legacy.mjs']     # the third: wasm2js on words, no typed arrays (IE 9)
WORDS = ['node', 'tools/words.mjs']                          # words.js, the words pass and the shim-ie9 gate, in node
LEG6 = ['env', 'LEGACY=ie6', 'node', 'tools/legacy.mjs']     # the fourth: words in ES3 on the old DOM (IE 6 to 8)
LEGBELOW = ['env', 'LEGACY=below', 'node', 'tools/legacy.mjs']  # under the fourth tier's floor: refused
FUZZ6 = ['env', 'LEGACY=ie6', 'node', 'tools/fuzz.mjs']      # the keyed fuzz on the fourth tier
ES3T = ['node', 'tools/es3.mjs']                             # the fourth tier's checks and passes, in node
VMLT = ['node', 'tools/vml.mjs']                             # the fourth tier's SVG path converter (vml.js), in node
JOB = ['node', 'tools/job.mjs']
RESIZE = ['tools/resize_ban.sh']
IMG = ['env', 'IMAGE_BIG=0', 'IMAGE_WASM=0', 'tests/image/run.sh']   # the image module, native, small inputs
IMGBIG = ['env', 'IMAGE_WASM=0', 'tests/image/run.sh']               # with the big inputs (budget, peaks)
IMGALL = ['tests/image/run.sh']                                     # all of it: the big inputs and wasm
IMGW = ['env', 'IMAGE_BIG=0', 'tests/image/run.sh']                  # and in wasm, both ways out (tools/image.mjs)
CHK = ['env', 'IMAGE_BIG=0', 'IMAGE_WASM=0', 'IMAGE_SIMD=0', 'tests/image/run.sh']   # the image module natively, with the fed decoder's tests (test_chunked.c)
SIMD = ['tests/image/simd.sh']                                       # the SIMD loops against their scalar twins, in node
PROG = ['tests/image/prog.sh']                                       # the progressive decoder against libjpeg's own
MEMB = ['env', 'IMAGE_WASM=0', 'IMAGE_SIMD=0', 'IMAGE_PROG=0', 'tests/image/run.sh']   # the big inputs' peaks only
FUZZI = ['env', 'FUZZ_TARGETS=inflate', 'tests/image/fuzz.sh', '60']   # fuzz_inflate alone, 60 s (libFuzzer: FUZZ_CC)
PICK = ['tests/pick/run.sh']                                         # the SIMD pick (APP_SIMD) in Chrome (tools/pick.mjs)
FPT1 = ['env', 'LEGACY=1', 'node', 'tools/fptoint.mjs']              # float to integer casts on the second tier
FPT9 = ['env', 'LEGACY=ie9', 'node', 'tools/fptoint.mjs']            # and on the third (words)
LEGW2J = ['env', 'LEGACY=1', 'node', 'tools/legacy.mjs']             # the legacy page as a browser without WebAssembly
NOSCRIPT = ['tests/noscript/run.sh']                                 # the no-script fallback: Chrome off and on, lynx, the lens, cdpage's refusals
FRAMES = ['env', 'LEGACY=frames', 'node', 'tools/frames.mjs']       # the fifth tier: no DOM mutation, frames written whole
JS11T = ['node', 'tools/js11.mjs']                                   # the frames tier's language gates, in node
UTTT = ['node', 'tools/uttt.mjs']                                    # demo/uttt on the WebAssembly and frames tiers: needs FOOLISH, an absolute path to a foolish checkout (else it skips, and every uttt- mutant is missed)

# name, file, old, new, test command, text the failure must contain
M = [
  ('clone-detached', 'shim/cdom.js', '    push(templates[templateId].cloneNode(true));',
   '    const element = templates[templateId].cloneNode(true);\n    stack[depth++] = element;\n    last = element;', UNIT, 'FAIL clone inside'),
  ('svg-off', 'shim/cdom.js', 'if (svgDepth || name === "svg") {', 'if (svgDepth || name === "svgx") {', UNIT, 'FAIL svg and its'),
  ('svg-no-pop', 'shim/cdom.js', '    if (svgDepth) {\n      svgDepth--;\n    }\n', '', UNIT, 'FAIL svg and its'),
  ('svg-clone-depth', 'shim/cdom.js', '    if (svgDepth) {\n      svgDepth++;\n    }\n    push(templates', '    push(templates', UNIT, 'FAIL svg and its'),
  ('units-swap', 'shim/cdom.js', '"px", "%", "deg"', '"%", "px", "deg"', UNIT, 'FAIL every unit through cd_set_style_num'),
  ('units-offset', 'shim/cdom.js', 'number + UNITS[unit]', 'number + UNITS[unit + 1]', UNIT, 'FAIL every unit through cd_set_style_num'),
  ('stale-viewport', 'shim/cdom.js', 'function viewport(recordPtr) {\n    if (stale()) {', 'function viewport(recordPtr) {\n    if (!stale()) {', UNIT, 'FAIL after the memory grew'),
  ('stale-set', 'shim/cdom.js', 'function set(id, path, valuePtr) {\n    if (stale()) {', 'function set(id, path, valuePtr) {\n    if (!stale()) {', UNIT, 'FAIL after the memory grew'),
  ('stale-call', 'shim/cdom.js', 'function call(id, method, count, argsPtr) {\n    if (stale()) {', 'function call(id, method, count, argsPtr) {\n    if (!stale()) {', UNIT, 'FAIL after the memory grew'),
  ('timer-no-flush', 'src/cdom.c', '((cd_timer_fn)fn)(ctx);\n  cd_flush();', '((cd_timer_fn)fn)(ctx);', UNIT, 'FAIL the timer and frame exports flush'),
  ('raf-no-flush', 'src/cdom.c', 'else cd_on_raf(token, t);\n  cd_flush();', 'else cd_on_raf(token, t);', UNIT, 'FAIL the timer and frame exports flush'),
  ('ws-open-no-flush', 'src/cdom_net.c', '((cd_ws_fn)fn)(ctx, id, CD_WS_OPEN, 0, 0);\n  cd_flush();', '((cd_ws_fn)fn)(ctx, id, CD_WS_OPEN, 0, 0);', UNIT, 'FAIL net'),
  ('raf-no-time', 'shim/cdom.js', 'requestAnimationFrame((time) => exports.cd_raf(token, time))', 'requestAnimationFrame(() => exports.cd_raf(token, 0))', UNIT, 'FAIL a timer and a frame'),
  ('timer-token', 'shim/cdom.js', 'setTimeout(() => exports.cd_timer(token, handle), ms)', 'setTimeout(() => exports.cd_timer(token + 1, handle), ms)', UNIT, 'FAIL a timer and a frame'),
  ('event-no-flush', 'src/cdom.c', '  else cd_on_event(&inbox);\n  cd_flush();', '  else cd_on_event(&inbox);', UNIT, 'FAIL resize'),
  ('resize-size', 'shim/cdom.js', 'store32(evX, viewport ? innerWidth : point.clientX | 0);', 'store32(evX, viewport ? innerHeight : point.clientX | 0);', UNIT, 'FAIL resize'),
  ('visibility-target', 'src/cdom.c', '{ return ev == CD_EV_RESIZE ? CD_WINDOW : CD_DOCUMENT; }', '{ (void)ev; return CD_WINDOW; }', UNIT, 'FAIL visibilitychange'),
  ('viewport-dpr', 'shim/cdom.js', 'Math.round(devicePixelRatio * 100)', '100', UNIT, 'FAIL the device pixel ratio'),
  ('viewport-order', 'shim/cdom.js', '    store32(record + viewportW, innerWidth);\n    store32(record + viewportH, innerHeight);', '    store32(record + viewportW, innerHeight);\n    store32(record + viewportH, innerWidth);', UNIT, 'FAIL the viewport'),
  ('ws-bytes', 'shim/cdom.js', 'writeBytes(ptr, incoming, incoming.length);', 'writeBytes(ptr, incoming.reverse(), incoming.length);', UNIT, 'FAIL net'),
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
  ('net-no-flush', 'src/cdom_net.c', '((cd_http_fn)fn)(ctx, status, p, len);\n  cd_flush();', '((cd_http_fn)fn)(ctx, status, p, len);', UNIT, 'FAIL net'),
  ('no-end-anchor', 'src/cdom.c', 'cd_id end = L->end; /*', 'cd_id end = 0; /*', UNIT, 'FAIL a keyed list with a fixed sibling'),
  ('no-end-anchor-fuzz', 'src/cdom.c', 'cd_id end = L->end; /*', 'cd_id end = 0; /*', FUZZ, 'the anchor is not last'),
  ('end-anchor-moves', 'src/cdom.c', 'cd_move(old[os].node, ne < n ? nw[ne].node : end);\n      nw[--ne]', 'cd_move(old[os].node, ne < n ? nw[ne].node : 0);\n      nw[--ne]', FUZZ, 'the anchor is not last'),
  ('forget-keeps-ids', 'src/cdom.c', '    f->destroy(f->ctx, &L->items[i]);\n    cd_id_free(L->items[i].node);', '    f->destroy(f->ctx, &L->items[i]);', UNIT, 'FAIL lists and regions'),
  ('region-keeps-ids', 'src/cdom.c', '  for (; i < r->n; i++) cd_id_free(r->ids[i]);', '  for (; i < r->n && 0; i++) cd_id_free(r->ids[i]);', UNIT, 'FAIL lists and regions'),
  ('region-records-list', 'src/cdom.c', '  build_push(0); /* the items', '  build_push(recording); /* the items', UNIT, 'FAIL lists and regions'),
  ('double-free-silent', 'src/cdom.c', '  if (id >= next_id || !alive[id]) __builtin_trap();', '', UNIT, 'FAIL freeing a free id traps'),
  ('name-open-is-last', 'shim/cdom.js', '    nodes[id] = stack[depth - 1];', '    nodes[id] = last;', UNIT, 'FAIL cd_name_open'),
  # cd_name_el and cd_name_text (unit.c 10b): each names its own kind, never the last node made
  ('name-el-is-last', 'shim/cdom.js', '    nodes[id] = lastElement;', '    nodes[id] = last;', UNIT, 'FAIL cd_name_el'),
  ('name-text-is-last', 'shim/cdom.js', '    nodes[id] = lastText;', '    nodes[id] = last;', UNIT, 'FAIL cd_name_el'),
  ('name-el-untracked', 'shim/cdom.js', '    last = lastElement = element;', '    last = element;', UNIT, 'FAIL cd_name_el'),
  ('name-text-untracked', 'shim/cdom.js', '    last = lastText = doc.createTextNode(str(id));', '    last = doc.createTextNode(str(id));', UNIT, 'FAIL cd_name_el'),
  ('name-text-num-untracked', 'shim/cdom.js', '    last = lastText = doc.createTextNode(number >>> 0);', '    last = doc.createTextNode(number >>> 0);', UNIT, 'FAIL cd_name_el'),
  ('name-el-calls-text', 'src/cdom.c', 'void cd_name_el(cd_id id) { ready(); j_name_el(id); }', 'void cd_name_el(cd_id id) { ready(); j_name_text(id); }', UNIT, 'FAIL cd_name_el'),
  ('name-text-calls-el', 'src/cdom.c', 'void cd_name_text(cd_id id) { ready(); j_name_text(id); }', 'void cd_name_text(cd_id id) { ready(); j_name_el(id); }', UNIT, 'FAIL cd_name_el'),
  # strings (docs/strings-design.md): the runtime read, the table's index, the life of a cd_tmp, the macros
  ('str-ascii-any', 'shim/cdom.js', '      if (high < 128) {\n        return ascii(p, n);', '      if (high < 256) {\n        return ascii(p, n);', UNIT, 'FAIL a runtime string of 0 to 300 bytes'),
  ('str-limit-33', 'shim/cdom.js', '    if (n <= ASCII_MAX) {', '    if (n <= ASCII_MAX + 1) {', UNIT, 'FAIL a runtime string of 0 to 300 bytes'),
  ('str-ascii-case3', 'shim/cdom.js', 'case 3: return v + fromCode(b[p], b[p + 1], b[p + 2]);', 'case 3: return v + fromCode(b[p], b[p + 1]);', UNIT, 'FAIL a runtime string of 0 to 300 bytes'),
  ('str-ascii-step7', 'shim/cdom.js', 'for (; n >= 8; p += 8, n -= 8) {', 'for (; n >= 8; p += 7, n -= 8) {', UNIT, 'FAIL a runtime string of 0 to 300 bytes'),
  ('str-lits-off-by-one', 'shim/cdom.js', 'return lits[(s - litBase) >> 2];', 'return lits[((s - litBase) >> 2) + 1];', UNIT, 'FAIL'), # the page cannot even start: every slot reads the next entry
  ('str-lifetime-unchecked', 'shim/cdom.js', '    if (k >= load32(tmpCount)) {', '    if (false) {', UNIT, 'FAIL a cd_tmp used after another call'),
  ('str-tmp-done-missing', 'src/cdom.c', 'void cd_set_class(cd_id id, cd_str v) { ready(); j_set_class(id, v.v); tmp_done(); }', 'void cd_set_class(cd_id id, cd_str v) { ready(); j_set_class(id, v.v); }', UNIT, 'FAIL a cd_tmp used after another call'),
  ('str-cap-unchecked', 'src/cdom.c', '  if (tmp.n == CD_TMP_CAP) __builtin_trap();\n', '', UNIT, 'FAIL 32 cd_tmp are alive at once'),
  ('str-litq-always', 'src/cdom.h', '#define CD_LITQ(s) (__builtin_strncmp(CD_SPELL(s), "\\"", 1) == 0 && \\\n                    __builtin_constant_p(__builtin_strlen(CD_CHARP(s))))', '#define CD_LITQ(s) 1', UNIT, 'not a string literal'),
  ('str-evaluated-twice', 'src/cdom.h', 'CD_LIT_(s), cd_tmpz(CD_CHARP(s))))', 'CD_LIT_(s), cd_tmpz(({ const char *a_ = CD_CHARP(s); (void)CD_CHARP(s); a_; }))))', UNIT, 'FAIL a string argument is evaluated once'),
  ('str-macro-dropped', 'src/cdom.h', '#define cd_call_args(id, method, n, a) (cd_call_args)(id, cd_s(method), n, a)', '#define cd_call_args(id, method, n, a) (cd_call_args)(id, method, n, a)', UNIT, 'note: passing argument to parameter'),
  # the batched read and the window's end (docs/strings-design.md, cd_tmp_ahead)
  ('ahead-ignored', 'shim/cdom.js', '    if (aheadText !== null && q >= aheadAt && q + n <= aheadEnd) {', '    if (false) {', UNIT, 'FAIL checkAhead on: a byte changed inside a live range throws'),
  ('ahead-end-by-start', 'shim/cdom.js', 'q + n <= aheadEnd', 'q < aheadEnd', UNIT, 'FAIL cd_tmp_ahead, kind 0: a range from inside string 100'),
  ('ahead-ascii-shortcut', 'shim/cdom.js', 'aheadText.length === len', 'true', UNIT, 'FAIL cd_tmp_ahead, kind 1: each of 1024'),
  ('ahead-astral-unit', 'shim/cdom.js', '      u += c >= 0xf0;           // an astral lead byte: a surrogate pair\n', '', UNIT, 'FAIL cd_tmp_ahead, kind 2: each of 1024'),
  ('ahead-not-reset-at-end', 'shim/cdom.js', '  function callEnded() {\n    aheadText = null;\n', '  function callEnded() {\n', UNIT, 'FAIL cd_tmp_ahead: the range is gone when the call into C returns'),
  ('ahead-zero-keeps', 'shim/cdom.js', '    if (!len) {\n      aheadText = null;\n      return;\n    }', '    if (!len) {\n      return;\n    }', UNIT, 'FAIL cd_tmp_ahead(0, 0) ends the range'),
  ('ahead-check-off', 'shim/cdom.js', 'if (checkAhead && text !== readString(q, n)) {', 'if (false) {', UNIT, 'FAIL checkAhead on: a byte changed inside a live range throws'),
  ('tmp-window-kept-after-throw', 'shim/cdom.js', '    store32(tmpSerial0, load32(tmpSerial0) + load32(tmpCount));\n    store32(tmpCount, 0);\n', '', UNIT, 'FAIL a cd_tmp made before a throw is dead in the next call'),
  # read-only strings: a pointer into read-only data is its own id (docs/strings-plain-c.md, option B)
  ('ro-range-data-end', 'src/cdom.c', 'extern const char __rodata_start[], __rodata_end[];\ncd_str cd_tmpz(const char *s) {\n  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__rodata_end - (uintptr_t)__rodata_start)', 'extern const char __rodata_start[], __data_end[];\ncd_str cd_tmpz(const char *s) {\n  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__data_end - (uintptr_t)__rodata_start)', UNIT, 'FAIL read-only strings: a mutable static'),
  ('ro-range-swapped', 'src/cdom.c', '  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__rodata_end - (uintptr_t)__rodata_start)', '  if ((uintptr_t)s - (uintptr_t)__rodata_end < (uintptr_t)__rodata_start - (uintptr_t)__rodata_end)', UNIT, 'FAIL read-only strings: a mutable static'),
  ('ro-range-empty', 'src/cdom.c', '  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__rodata_end - (uintptr_t)__rodata_start)', '  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__rodata_start - (uintptr_t)__rodata_start)', UNIT, 'FAIL read-only strings: a ternary'),
  ('ro-range-empty-link', 'src/cdom.c', '  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__rodata_end - (uintptr_t)__rodata_start)', '  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__rodata_start - (uintptr_t)__rodata_start)', LINK, "FAIL cd_tmpz of a literal the macro cannot see is the literal's own address"),
  ('ro-cache-key-dropped', 'shim/cdom.js', '      if (roKey[h] === s) {', '      if (roKey[h] !== 0) {', UNIT, 'FAIL read-only strings: two addresses'),
  ('ro-slot-upper-bound-only', 'shim/cdom.js', '    if (s >= litBase && s < litEnd) {', '    if (s < litEnd) {', UNIT, 'FAIL'), # every read-only pointer reads as a slot: the page cannot start
  ('ro-slot-lower-bound-only', 'shim/cdom.js', '    if (s >= litBase && s < litEnd) {', '    if (s >= litBase) {', UNIT, 'FAIL read-only strings: an id above the slot range'),
  ('ro-not-kept', 'shim/cdom.js', '        if (onRoRead) {\n          onRoRead(s);\n        }\n      }\n      roKey[h] = s;\n      roVal[h] = v;\n      return v;', '        if (onRoRead) {\n          onRoRead(s);\n        }\n      }\n      roKey[h] = s;\n      roVal[h] = v;\n      roMap.delete(s);\n      return v;', UNIT, 'FAIL read-only strings: two addresses'),
  ('ro-no-object-fallback', 'shim/cdom.js', 'const roObj = roMap ? null : {};', 'const roObj = null;', ['node', 'tools/legacy.mjs'], 'FAIL'),
  ('ro-path-by-slot-only', 'shim/cdom.js', '    if (path < litBase || path >= litEnd) {', '    if (path < 0) {', HOST, 'FAIL two read-only paths a word apart'),
  ('str-none-is-one', 'src/cdom.h', '#define CD_NONE ((cd_str){0})', '#define CD_NONE ((cd_str){1})', HOST, 'FAIL'),
  ('str-tpl-runtime-kept', 'src/cdom.c', '  if (s.v >> 31) { /* a runtime string: the same text can be new bytes, so it is never skipped as unchanged */\n    if (t >= CD_TPLS || k >= CD_SLOTS) __builtin_trap();\n    slot_kind[t][k] = 0;\n  } else if', '  if (s.v >> 31) {\n    if (t >= CD_TPLS || k >= CD_SLOTS) __builtin_trap();\n  } else if', UNIT, 'FAIL a template slot set from a cd_tmp'),
  ('at-default-missing', 'src/cdom.c', '  cd_at(CD_MOUNT, CD_APPEND); /* so an app starts building at the end of its mount, and calls cd_at only to build somewhere else */\n', '', UNIT, 'FAIL an app that calls no cd_at'),
  ('str-tpl-same-text', 'src/cdom.c', 'if (slot_kind[t][k] == kind && slot_val[t][k] == v) return 1;', 'if (slot_kind[t][k] == kind) return 1;', UNIT, 'FAIL clone inside'),
  ('fmt-zeros', 'src/cdom.c', '  while (lo < dec && t[lo] == \'0\') lo++;', '', UNIT, 'FAIL cd_fmt'),
  ('fmt-round', 'src/cdom.c', '(v < 0 ? -0.5 : 0.5)', '0.5', UNIT, 'FAIL cd_fmt'),
  ('two-heaps', 'src/cdom.c', 'void *cd_alloc(uint32_t n) {\n  void *p = malloc(n);', 'void *cd_alloc(uint32_t n) {\n  void *p = heap_alloc(n);', LINK, "FAIL the app's heap"),
  # the _try allocations (tools/alloc.mjs, tests/link/run.sh's size gate)
  ('try-traps', 'src/cdom.c', '    if (may_fail) return 0;\n', '    (void)may_fail;\n', ALLOC, 'FAIL cd_alloc_try at the cap: blocks'),
  ('malloc-null', 'src/cdom.c', '    __builtin_trap(); /* out of memory */', '    return 0;', ALLOC, 'FAIL malloc still traps'),
  ('realloc-big-null', 'src/cdom.c', '    if (may_fail) return 0;\n    __builtin_trap(); /* out of memory */', '    (void)may_fail;\n    return 0;', ALLOC, 'FAIL realloc still traps'),
  ('realloc-try-frees', 'src/cdom.c', '  void *q = may_fail ? heap_alloc_try((uint32_t)n) : heap_alloc((uint32_t)n);\n', '  void *q = may_fail ? heap_alloc_try((uint32_t)n) : heap_alloc((uint32_t)n);\n  if (p && !q) free(p);\n', ALLOC, 'FAIL cd_realloc_try'),
  # the heap's large blocks (tests/alloc_mem.c): each rule its own case
  ('heap-big-pow2', 'src/cdom.c', '  if (n > HEAP_SMALL_MAX - 8) {\n    c = HEAP_BIG;', '  if (0) {\n    c = HEAP_BIG;', ALLOC, 'FAIL a large block costs its size'),
  ('heap-big-grain-64k', 'src/cdom.c', '#define HEAP_GRAIN 4096u ', '#define HEAP_GRAIN 65536u ', ALLOC, 'FAIL a large block costs its size'),
  ('heap-top-kept', 'src/cdom.c', '  if ((uintptr_t)b + b->size == heap_ptr) { /* the top', '  if (0) { /* the top', ALLOC, 'FAIL a large block freed at the top gives'),
  ('heap-top-run-kept', 'src/cdom.c', '    if (prev && (uintptr_t)prev + prev->size == (uintptr_t)b) b = prev, at = pat;', '    if (0) b = prev, at = pat;', ALLOC, 'FAIL a free run under the top goes back'),
  ('heap-no-merge-next', 'src/cdom.c', '  if (next && (uintptr_t)b + b->size == (uintptr_t)next) b->size += next->size, next = next->next;\n', '', ALLOC, 'FAIL a freed large block merges'),
  ('heap-no-merge-prev', 'src/cdom.c', '  if (prev && (uintptr_t)prev + prev->size == (uintptr_t)b) prev->size += b->size, prev->next = next;\n  else b->next', '  if (0) prev->size += b->size, prev->next = next;\n  else b->next', ALLOC, 'FAIL two freed large blocks'),
  ('heap-split-rest-lost', 'src/cdom.c', '      *at = rest;', '      *at = r->next;', ALLOC, 'FAIL a large block freed below the top'),
  ('heap-split-rest-overlaps', 'src/cdom.c', '      rest->size = r->size - size;', '      rest->size = r->size;', ALLOC, 'FAIL 20,000 seeded steps'),
  ('heap-big-no-header', 'src/cdom.c', '    size = (n + 8 + HEAP_GRAIN - 1) & ~(HEAP_GRAIN - 1);', '    size = (n + HEAP_GRAIN - 1) & ~(HEAP_GRAIN - 1);', ALLOC, 'FAIL 20,000 seeded steps'),
  ('heap-no-grow-top', 'src/cdom.c', '  if (p && n > HEAP_SMALL_MAX - 8 && heap_grow_top(p, (uint32_t)n)) return p;\n', '  if (p && n > HEAP_SMALL_MAX - 8 && 0 && heap_grow_top(p, (uint32_t)n)) return p;\n', ALLOC, 'FAIL a block at the top grows where it is'),
  ('heap-grow-top-anywhere', 'src/cdom.c', '  if ((uintptr_t)h + heap_cap(p) + 8 != heap_ptr || !heap_room', '  if (!heap_room', ALLOC, 'FAIL 20,000 seeded steps'),
  ('bytes-http-kept', 'src/cdom_net.c', '((cd_http_fn)fn)(ctx, status, p, len);\n  cd_flush();\n  cd_free((void *)p);', '((cd_http_fn)fn)(ctx, status, p, len);\n  cd_flush();', ALLOC, 'FAIL an answer\'s bytes are freed'),
  ('bytes-msg-kept', 'src/cdom_net.c', 'CD_WS_MSG, p, len);\n  cd_flush();\n  cd_free((void *)p);', 'CD_WS_MSG, p, len);\n  cd_flush();', ALLOC, 'FAIL cd_net_msg frees'),
  ('bytes-encoded-kept', 'src/cdom.c', '  cd_on_encoded(token, p, len);\n  cd_flush();\n  cd_free((void *)p);', '  cd_on_encoded(token, p, len);\n  cd_flush();', ALLOC, 'FAIL cd_encoded frees'),
  ('bytes-store-kept', 'src/cdom_store.c', '  cd_free((void *)p); /* the block cd_bytes_buf gave', '  /* the block cd_bytes_buf gave', ALLOC, 'FAIL cd_store_got frees'),
  ('bytes-url-kept', 'src/cdom_url.c', '  cd_free((void *)path);', '', ALLOC, 'FAIL cd_url_changed frees'),
  ('heap-cap-loose', 'src/cdom.c', '  if ((uint32_t)__builtin_wasm_memory_size(0) + pages > (uint32_t)(CDOM_HEAP_PAGES_MAX)) return 0;', '  if ((uint32_t)__builtin_wasm_memory_size(0) + pages > (uint32_t)(CDOM_HEAP_PAGES_MAX) + 8) return 0;', ALLOC, 'FAIL cd_alloc_try at the cap: blocks'),
  ('own-alloc-bypassed', 'src/cdom.c', 'static int heap_is_cdoms(void) { return malloc == cdom_heap_malloc; }', 'static int heap_is_cdoms(void) { return 1; }', ALLOC, 'FAIL cd_alloc_try and cd_realloc_try on an app'),
  ('try-path-shared', 'src/cdom.c', 'static inline __attribute__((always_inline)) void *heap_alloc_or(', '__attribute__((noinline)) static void *heap_alloc_or(', LINK, 'FAIL the _try path is in an app that never calls it'),
  ('try-path-heavy', 'src/cdom.c', '  return n >> 31 ? 0 : heap_alloc_try(n);\n}', '  void *p = n >> 31 ? 0 : heap_alloc_try(n);\n  char t[24];\n  if (p) ((uint8_t *)p)[0] = (uint8_t)cd_fmt_len(t, (double)n / 3, CD_UNIT_PX); /* a path that grew: cd_fmt in it */\n  return p;\n}', LINK, 'FAIL calling cd_alloc_try costs'),
  ('grow-refusal-unsigned', 'src/cdom.c', '  return (intptr_t)__builtin_wasm_memory_grow(0, pages) >= 0;', '  return __builtin_wasm_memory_grow(0, pages) >= 0;', ALLOC, 'FAIL cd_alloc_try when memory.grow refuses'),
  ('realloc-alloc-copy', 'src/cdom.c', '  void *q = may_fail ? heap_alloc_try((uint32_t)n) : heap_alloc((uint32_t)n);\n', '  void *q = heap_alloc_or((uint32_t)n, may_fail);\n', LINK, 'FAIL realloc carries a copy of the allocator'),
  ('foreign-free', 'src/cdom.c', '  if ((uintptr_t)p < heap_lo + 8 || (uintptr_t)p >= heap_ptr) __builtin_trap(); /* not from this heap */', '  (void)heap_lo;', LINK, 'FAIL an app that replaces malloc alone'),
  ('image-pixels', 'shim/cdom.js', 'pixels(pixelsPtr, width, height, channels, context)', 'pixels(pixelsPtr + 4, width, height, channels, context)', UNIT, 'FAIL cd_encode_image'),
  ('image-token', 'shim/cdom.js', 'deliver(exports.cd_encoded, token, data)', 'deliver(exports.cd_encoded, token + 1, data)', UNIT, 'FAIL cd_encode_image'),
  ('image-rgb', 'shim/cdom.js', 'rgba[to + 1] = data[from + 1];', 'rgba[to + 1] = data[from + 2];', UNIT, 'FAIL cd_encode_image_in with RGB'),
  ('image-place', 'shim/cdom.js', 'putImageData(pixels(pixelsPtr, width, height, channels, context), x, y)', 'putImageData(pixels(pixelsPtr, width, height, channels, context), 0, 0)', UNIT, 'FAIL cd_encode_image_in with RGB'),
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
  ('store-no-flush', 'src/cdom_store.c', '((cd_store_fn)fn)(ctx, p, len);\n  cd_flush();', '((cd_store_fn)fn)(ctx, p, len);', UNIT, 'FAIL cd_store: a put'),
  ('rect-order', 'shim/cdom.js', 'const edges = [box.left, box.top, box.width, box.height];', 'const edges = [box.top, box.left, box.width, box.height];', UNIT, 'FAIL cd_rect'),
  ('rect-untransformed', 'shim/cdom.js', 'const box = node.getBoundingClientRect();', 'const box = { left: node.offsetLeft, top: node.offsetTop, width: node.offsetWidth, height: node.offsetHeight };', UNIT, 'FAIL cd_rect'),
  ('now-zero', 'shim/cdom.js', '    return performance.now();', '    return 0;', UNIT, 'FAIL cd_now'),
  ('input-no-prevent', 'shim/cdom.js', '    if (flags & 1) {', '    if (false) {', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-touch-point', 'shim/cdom.js', '    return event.changedTouches ? event.changedTouches[0] : event;', '    return event;', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-mask-16', 'shim/cdom.js', '    listenMask[id * 2 + (type >> 5)] |= 1 << (type & 31);', '    listenMask[id * 2 + (type >> 5)] |= (1 << (type & 31)) & 0xffff;', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-win-viewport', 'shim/cdom.js', '    const viewport = type === RESIZE || type === VISIBILITY;', '    const viewport = true;', UNIT, 'FAIL the document'),
  ('input-unwin-noop', 'shim/cdom.js', '    nodes[id].removeEventListener(EVENTS[type], listener);', '', UNIT, 'FAIL the document'),
  ('input-win-twice', 'shim/cdom.js', '    if (hostListeners[slot]) {\n      return;\n    }\n    const listener', '    const listener', UNIT, 'FAIL the document'),
  ('input-touchmove-passive', 'shim/cdom.js', 'listener, { passive: false });', 'listener);', UNIT, 'FAIL a touchmove on the document'),
  ('input-no-char', 'shim/cdom.js', 'key.length === 1 ? key.charCodeAt(0) : 0);', 'key.length === 1 ? 0 : 0);', UNIT, 'FAIL a keydown on the document'),
  ('input-no-editing', 'shim/cdom.js', '      | (editing(event.target) << 4));', ');', UNIT, 'FAIL a keydown on the document'),
  ('input-prevent-c', 'src/cdom.c', 'void cd_prevent_default(void) { inbox.flags |= CD_PREVENT; }', 'void cd_prevent_default(void) { }', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-no-time', 'shim/cdom.js', '    store32(evMs, Math.round(event.timeStamp || 0));', '    store32(evMs, Math.round(performance.now()));', UNIT, 'FAIL a mousedown and a touchstart'),
  ('hit-bottom-first', 'shim/cdom.js', '    for (const node of doc.elementsFromPoint(x, y)) {', '    for (const node of [...doc.elementsFromPoint(x, y)].reverse()) {', UNIT, 'FAIL cd_hit'),
  ('hit-not-listening', 'shim/cdom.js', '      if (node.$i !== undefined && (listenMask[node.$i * 2] | listenMask[node.$i * 2 + 1])) {\n        return node.$i;', '      if (node.$i !== undefined) {\n        return node.$i;', UNIT, 'FAIL cd_hit'),
  ('hit-topmost-only', 'shim/cdom.js', '    for (const node of doc.elementsFromPoint(x, y)) {', '    for (const node of [doc.elementFromPoint(x, y)]) {', UNIT, 'FAIL cd_hit: through a layer'),
  ('audio-load-bytes', 'shim/cdom.js', 'const data = copyBytes(bytesPtr, length).buffer;', 'const data = copyBytes(bytesPtr + 2, length).buffer;', UNIT, 'FAIL cd_audio_load'),
  ('audio-load-late', 'shim/cdom.js', 'audio().decodeAudioData(data).then(', 'Promise.resolve().then(() => audio().decodeAudioData(copyBytes(bytesPtr, length).buffer)).then(', UNIT, 'FAIL cd_audio_load'),
  ('audio-failed-is-clip', 'shim/cdom.js', '() => exports.cd_audio_loaded(token, clip, 0));', '() => exports.cd_audio_loaded(token, clip, 1));', UNIT, 'FAIL cd_audio_load'),
  ('audio-failed-keeps-id', 'src/cdom_audio.c', '    id_give(&clips, clip);\n    clip = CD_AUDIO_FAILED;', '    clip = CD_AUDIO_FAILED;', UNIT, 'FAIL cd_audio_load'),
  ('audio-loaded-no-flush', 'src/cdom_audio.c', '  cd_on_audio_loaded(token, clip);\n  cd_flush();', '  cd_on_audio_loaded(token, clip);', UNIT, 'FAIL cd_audio_load'),
  ('audio-free-keeps-id', 'src/cdom_audio.c', '  id_give(&clips, clip);\n  j_audio_free(clip);', '  j_audio_free(clip);', UNIT, 'FAIL cd_audio_free'),
  ('audio-free-keeps-buffer', 'shim/cdom.js', '    clips[clip] = undefined;', '', UNIT, 'FAIL cd_audio_free'),
  ('audio-play-unloaded', 'src/cdom_audio.c', '  if (!id_live(&clips, clip)) __builtin_trap();\n', '', UNIT, 'FAIL playing a clip that is not loaded'),
  ('audio-double-free-silent', 'src/cdom_audio.c', '  if (!id_live(p, id)) __builtin_trap();\n', '', UNIT, 'FAIL playing a clip that is not loaded'),
  ('audio-locked-is-running', 'shim/cdom.js', '    if (audio().state === "running") {', '    if (audio().state !== "closed") {', UNIT, 'FAIL cd_audio_state is LOCKED'),
  ('audio-no-unlock', 'shim/cdom.js', '      addEventListener(type, unlockAudio, { capture: true, passive: true });', '', UNIT, 'FAIL a real gesture'),
  ('audio-unlock-no-resume', 'shim/cdom.js', '    if (audioContext.state !== "running") {', '    if (false) {', UNIT, 'FAIL a real gesture'),
  ('audio-state-unheard', 'shim/cdom.js', '    exports.cd_audio_state_changed(audio_state());', '', UNIT, 'FAIL a real gesture'),
  ('audio-no-start', 'shim/cdom.js', '    source.start(Math.max(0, context.currentTime + (atMs - performance.now()) / 1000));', '', UNIT, 'FAIL cd_audio_play: clip, source'),
  ('audio-initial-gain', 'shim/cdom.js', '    voiceGain.gain.value = gain;', '', UNIT, 'FAIL cd_audio_play: clip, source'),
  ('audio-no-voice-gain', 'shim/cdom.js', '    voiceGain.connect(masterGain);', '    voiceGain.connect(context.destination);', UNIT, 'FAIL cd_audio_play: clip, source'),
  ('audio-not-let-go', 'shim/cdom.js', '    voiceGains[voice].disconnect();', '', UNIT, 'FAIL cd_audio_play: clip, source'),
  ('audio-ended-no-flush', 'src/cdom_audio.c', '  cd_on_audio_ended(voice);\n  cd_flush();', '  cd_on_audio_ended(voice);', UNIT, 'FAIL cd_audio_play: clip, source'),
  ('audio-voice-kept', 'shim/cdom.js', '    voiceSources[voice] = undefined;', '', UNIT, 'FAIL a voice whose ended answer came'),
  ('audio-stop-dead', 'src/cdom_audio.c', '  if (id_live(&voices, voice)) j_audio_stop(voice);', '  j_audio_stop(voice);', UNIT, 'FAIL a voice whose ended answer came'),
  ('audio-gain-dead', 'src/cdom_audio.c', '  if (id_live(&voices, voice)) j_audio_gain(voice, gain);', '  j_audio_gain(voice, gain);', UNIT, 'FAIL a voice whose ended answer came'),
  ('audio-at-ignored', 'shim/cdom.js', 'source.start(Math.max(0, context.currentTime + (atMs - performance.now()) / 1000));', 'source.start(0);', UNIT, "FAIL cd_audio_play at a time"),
  ('audio-at-dropped', 'src/cdom_audio.c', '  j_audio_play(clip, voice, gain, rate, at_ms);', '  j_audio_play(clip, voice, gain, rate, 0 * at_ms);', UNIT, "FAIL cd_audio_play at a time"),
  ('audio-rate-ignored', 'shim/cdom.js', '    source.playbackRate.value = rate;', '', UNIT, 'FAIL cd_audio_play at rate 4'),
  ('audio-stop-noop', 'shim/cdom.js', '    voiceSources[voice].stop();', '    void voiceSources[voice];', UNIT, 'FAIL cd_audio_stop'),
  ('audio-gain-noop', 'shim/cdom.js', '    easeGain(voiceGains[voice], gain);', '', UNIT, 'FAIL cd_audio_set_gain'),
  ('audio-master-noop', 'shim/cdom.js', '    easeGain(masterGain, gain);', '', UNIT, 'FAIL cd_audio_set_gain'),
  ('audio-mute-ignored', 'src/cdom_audio.c', '  muted = on;\n  j_audio_master(muted ? 0 : master);', '  muted = on;\n  j_audio_master(master);', UNIT, 'FAIL cd_audio_set_gain'),
  ('audio-master-lost-while-muted', 'src/cdom_audio.c', '  master = gain;\n', '  if (!muted) master = gain;\n', UNIT, 'FAIL cd_audio_set_gain'),
  ('audio-ids-grow', 'src/cdom_audio.c', '  if (p->nfreed) {', '  if (0) {', UNIT, 'FAIL 20 voices played one after another reuse one id'),
  ('audio-ended-keeps-id', 'src/cdom_audio.c', '  id_give(&voices, voice);\n  cd_on_audio_ended(voice);', '  cd_on_audio_ended(voice);', UNIT, 'FAIL 20 voices played one after another reuse one id'),
  ('audio-ended-wrong-voice', 'shim/cdom.js', '    source.$v = voice;', '    source.$v = 1;', UNIT, 'FAIL cd_audio_stop'),
  ('audio-never-ran', 'shim/cdom.js', '      audioRan = true;', '', UNIT, 'FAIL hidden, the context suspends'),
  ('audio-no-suspend', 'shim/cdom.js', '      audioContext.suspend();', '', UNIT, 'FAIL hidden, the context suspends'),
  ('audio-no-resume-shown', 'shim/cdom.js', '    } else {\n      audioContext.resume().catch(() => {});', '    } else {', UNIT, 'FAIL hidden, the context suspends'),
  ('demo-silent', 'demo/click.c', '  cd_audio_play(clip, 0.8, rate, 0);', '  (void)rate;', UNIT, 'FAIL demo'),
  ('demo-hint-stuck', 'demo/click.c', 'void cd_on_audio_state(uint32_t) {\n  show_state();', 'void cd_on_audio_state(uint32_t) {', UNIT, 'FAIL demo'),
  ('random-one-call', 'src/cdom_random.c', '    uint32_t n = len < CD_RANDOM_CHUNK ? len : CD_RANDOM_CHUNK;', '    uint32_t n = len;', UNIT, 'FAIL cd_random_bytes: 200,000 bytes'),
  ('random-chunk-small', 'src/cdom_random.h', '#define CD_RANDOM_CHUNK 65536', '#define CD_RANDOM_CHUNK 32768', UNIT, 'FAIL cd_random_bytes: 200,000 bytes'),
  ('random-no-advance', 'src/cdom_random.c', '    p += n;\n', '', UNIT, 'FAIL cd_random_bytes: 200,000 bytes'),
  ('random-offset', 'shim/cdom.js', '    randomBytes(bufferPtr, length);', '    randomBytes(bufferPtr + 1, length);', UNIT, 'FAIL cd_random_bytes: 200,000 bytes'),
  ('random-noop', 'shim/cdom.js', '    randomBytes(bufferPtr, length);', '    bytes(bufferPtr, length);', UNIT, 'FAIL cd_random_bytes: two fills differ'),
  ('random-zero-asks', 'src/cdom_random.c', '  while (len) {', '  for (int once = 1; once || len; once = 0) {', UNIT, 'FAIL cd_random_bytes: two fills differ'),
  ('random-unavailable-ok', 'shim/cdom.js', '    if (!globalThis.crypto || !crypto.getRandomValues) {\n      return 0;', '    if (!globalThis.crypto || !crypto.getRandomValues) {\n      return 1;', UNIT, 'FAIL no crypto.getRandomValues'),
  ('random-unavailable-ignored', 'src/cdom_random.c', '    if (!j_random(p, n)) return CD_RANDOM_UNAVAILABLE;', '    j_random(p, n);', UNIT, 'FAIL no crypto.getRandomValues'),
  ('url-parts-swapped', 'shim/cdom.js', 'const URL_PARTS = ["pathname", "search", "hash"];', 'const URL_PARTS = ["pathname", "hash", "search"];', UNIT, 'FAIL cd_url_read'),
  ('url-all-path-only', 'shim/cdom.js', '    return location.pathname + location.search + location.hash;', '    return location.pathname;', UNIT, 'FAIL cd_url_read'),
  ('url-read-overflow', 'shim/cdom.js', '    writeBytes(destPtr, encoded, Math.min(capacity, encoded.length));', '    writeBytes(destPtr, encoded, encoded.length);', UNIT, 'FAIL cd_url_read'),
  ('url-read-written', 'shim/cdom.js', '    return encoded.length;', '    return Math.min(encoded.length, capacity);', UNIT, 'FAIL cd_url_read'),
  ('url-push-is-replace', 'src/cdom_url.c', '{ return j_url_go(path, len, 0); }', '{ return j_url_go(path, len, 1); }', UNIT, 'FAIL cd_url_push adds'),
  ('url-replace-is-push', 'src/cdom_url.c', '{ return j_url_go(path, len, 1); }', '{ return j_url_go(path, len, 0); }', UNIT, 'FAIL cd_url_push adds'),
  ('url-refused-ok', 'shim/cdom.js', '    } catch {\n      return 0;', '    } catch {\n      return 1;', UNIT, 'FAIL cd_url_push refused'),
  ('url-let-go-ok', 'shim/cdom.js', '    return +(location.href === wanted);', '    return 1;', UNIT, 'FAIL cd_url_push refused'),
  ('url-no-popstate', 'shim/cdom.js', '      addEventListener("popstate", urlChanged);', '', UNIT, 'FAIL back, forward'),
  ('url-popped-path-only', 'shim/cdom.js', '    const path = textEncoder.encode(urlOf(URL_PARTS.length));', '    const path = textEncoder.encode(urlOf(0));', UNIT, 'FAIL back, forward'),
  ('url-changed-no-flush', 'src/cdom_url.c', '  cd_on_url(path, len);\n  cd_flush();', '  cd_on_url(path, len);', UNIT, 'FAIL back, forward'),
  ('url-title-noop', 'shim/cdom.js', '    doc.title = utf8(textPtr, textLen);', '    utf8(textPtr, textLen);', UNIT, 'FAIL cd_set_title'),
  ('locale-languages-ignored', 'shim/cdom.js', '    if (navigator.languages && navigator.languages.length) {\n      return navigator.languages;', '    if (false) {\n      return navigator.languages;', UNIT, 'FAIL cd_language: the user'),
  ('locale-past-last', 'shim/cdom.js', 'languageList()[index] ?? ""', 'languageList()[index] ?? "und"', UNIT, 'FAIL cd_language: the user'),
  ('locale-no-fallback', 'shim/cdom.js', '    return navigator.language ? [navigator.language] : [];', '    return [];', UNIT, 'FAIL no navigator.languages'),
  ('locale-empty-languages', 'shim/cdom.js', '    if (navigator.languages && navigator.languages.length) {', '    if (navigator.languages) {', UNIT, 'FAIL no navigator.languages'),
  ('locale-bits-swapped', 'shim/cdom.js', '  const PREFS_DARK = 1;\n  const PREFS_REDUCED_MOTION = 2;', '  const PREFS_DARK = 2;\n  const PREFS_REDUCED_MOTION = 1;', UNIT, 'FAIL cd_prefs: dark and reduced motion'),
  ('locale-motion-query', 'shim/cdom.js', 'const REDUCED_MOTION_QUERY = "(prefers-reduced-motion: reduce)";', 'const REDUCED_MOTION_QUERY = "(prefers-reduced-motion: no-preference)";', UNIT, 'FAIL cd_prefs: dark and reduced motion'),
  ('locale-no-matchmedia-throws', 'shim/cdom.js', '    return window.matchMedia ? matchMedia(query) : null;', '    return matchMedia(query);', UNIT, 'FAIL cd_prefs: dark and reduced motion'),
  ('locale-no-listen', 'shim/cdom.js', '    if (exports.cd_prefs_changed) {\n      listenPrefs();', '    if (exports.cd_prefs_changed) {', UNIT, 'FAIL cd_on_prefs'),
  ('locale-motion-unheard', 'shim/cdom.js', '    for (const query of [DARK_QUERY, REDUCED_MOTION_QUERY]) {', '    for (const query of [DARK_QUERY]) {', UNIT, 'FAIL cd_on_prefs'),
  ('locale-changed-stale', 'shim/cdom.js', '    exports.cd_prefs_changed(prefs());', '    exports.cd_prefs_changed(0);', UNIT, 'FAIL cd_on_prefs'),
  ('locale-prefs-no-flush', 'src/cdom_locale.c', '  cd_on_prefs(prefs);\n  cd_flush();', '  cd_on_prefs(prefs);', UNIT, 'FAIL cd_on_prefs'),
  ('share-unavailable-sync', 'shim/cdom.js', '    let status = Promise.resolve(SHARE_UNAVAILABLE);\n    if (promise) {', '    if (!promise) {\n      answer(token, SHARE_UNAVAILABLE);\n      return;\n    }\n    let status;\n    if (promise) {', UNIT, 'FAIL no navigator.clipboard'),
  ('share-unavailable-done', 'shim/cdom.js', 'let status = Promise.resolve(SHARE_UNAVAILABLE);', 'let status = Promise.resolve(SHARE_DONE);', UNIT, 'FAIL no navigator.clipboard'),
  ('share-codes-swapped', 'shim/cdom.js', '  const SHARE_UNAVAILABLE = 2;\n  const SHARE_CANCELLED = 3;', '  const SHARE_UNAVAILABLE = 3;\n  const SHARE_CANCELLED = 2;', UNIT, 'FAIL no navigator.share'),
  ('share-no-clipboard-check', 'shim/cdom.js', 'clipboard && clipboard.writeText ? clipboard.writeText(text) : null', 'clipboard.writeText(text)', UNIT, 'FAIL no navigator.clipboard'),
  ('share-no-share-check', 'shim/cdom.js', 'navigator.share ? navigator.share(data) : null', 'navigator.share(data)', UNIT, 'FAIL no navigator.share'),
  ('share-clipboard-token', 'shim/cdom.js', 'shareAnswer(exports.cd_clipboard_done, token, writing);', 'shareAnswer(exports.cd_clipboard_done, token + 1, writing);', UNIT, 'FAIL no navigator.clipboard'),
  ('share-wrong-export', 'shim/cdom.js', 'shareAnswer(exports.cd_share_done, token, sharing);', 'shareAnswer(exports.cd_clipboard_done, token, sharing);', UNIT, 'FAIL no navigator.share'),
  ('share-title-late', 'shim/cdom.js', '      data.title = utf8(titlePtr, titleLen);', '      queueMicrotask(() => data.title = utf8(titlePtr, titleLen));', UNIT, 'FAIL cd_share: the title and the url'),
  ('share-empty-title-kept', 'shim/cdom.js', '    if (titleLen) {', '    if (true) {', UNIT, 'FAIL cd_share: the title and the url'),
  ('share-empty-url-kept', 'shim/cdom.js', '    if (urlLen) {', '    if (true) {', UNIT, 'FAIL cd_share: the title and the url'),
  ('share-url-is-title', 'shim/cdom.js', '      data.url = utf8(urlPtr, urlLen);', '      data.url = utf8(titlePtr, titleLen);', UNIT, 'FAIL cd_share: the title and the url'),
  ('share-lengths-swapped', 'src/cdom_share.c', '  j_share(token, title, tlen, url, ulen);', '  j_share(token, title, ulen, url, tlen);', UNIT, 'FAIL cd_share: the title and the url'),
  ('share-done-no-flush', 'src/cdom_share.c', '  cd_on_share(token, status);\n  cd_flush();', '  cd_on_share(token, status);', UNIT, 'FAIL cd_share: the title and the url'),
  ('share-abort-is-failed', 'shim/cdom.js', 'error && error.name === "AbortError" ? SHARE_CANCELLED : SHARE_FAILED', 'SHARE_FAILED', UNIT, 'FAIL cd_share: the sheet closed'),
  ('share-failed-is-cancelled', 'shim/cdom.js', 'error && error.name === "AbortError" ? SHARE_CANCELLED : SHARE_FAILED', 'SHARE_CANCELLED', UNIT, 'FAIL cd_share: the sheet closed'),
  ('share-rejection-is-done', 'shim/cdom.js', 'promise.then(() => SHARE_DONE, shareFailure)', 'promise.then(() => SHARE_DONE, () => SHARE_DONE)', UNIT, 'FAIL cd_clipboard_write: denied'),
  ('share-clipboard-noop', 'shim/cdom.js', 'clipboard.writeText ? clipboard.writeText(text) : null', 'clipboard.writeText ? Promise.resolve() : null', UNIT, 'FAIL cd_clipboard_write: denied'),
  ('share-clipboard-late', 'shim/cdom.js', 'clipboard.writeText ? clipboard.writeText(text) : null', 'clipboard.writeText ? Promise.resolve().then(() => clipboard.writeText(utf8(textPtr, textLen))) : null', UNIT, 'FAIL cd_clipboard_write: denied'),
  ('share-clipboard-no-flush', 'src/cdom_share.c', '  cd_on_clipboard(token, status);\n  cd_flush();', '  cd_on_clipboard(token, status);', UNIT, 'FAIL no navigator.clipboard'),
  ('fonts-answered-twice', 'shim/cdom.js', '      if (answered) {\n        return;\n      }\n      answered = true;\n      exports.cd_fonts_done', '      exports.cd_fonts_done', UNIT, 'FAIL cd_fonts_wait: a font still loading'),
  ('fonts-no-deadline', 'shim/cdom.js', '    if (deadlineMs) {\n      setTimeout(() => answer(FONTS_TIMEOUT), deadlineMs);\n    }\n', '', UNIT, 'FAIL cd_fonts_wait: a font still loading'),
  ('fonts-zero-deadline-now', 'shim/cdom.js', '    if (deadlineMs) {\n      setTimeout(() => answer(FONTS_TIMEOUT)', '    if (true) {\n      setTimeout(() => answer(FONTS_TIMEOUT)', UNIT, 'FAIL cd_fonts_wait: a font still loading'),
  ('fonts-deadline-dropped', 'src/cdom_fonts.c', '{ j_fonts_wait(token, deadline_ms); }', '{ j_fonts_wait(token, 0 * deadline_ms); }', UNIT, 'FAIL cd_fonts_wait: a font still loading'),
  ('fonts-timeout-is-ready', 'shim/cdom.js', 'answer(FONTS_TIMEOUT)', 'answer(FONTS_READY)', UNIT, 'FAIL cd_fonts_wait: a font still loading'),
  ('fonts-ready-not-waited', 'shim/cdom.js', '    doc.fonts.ready.then(() => answer(FONTS_READY));', '    Promise.resolve().then(() => answer(FONTS_READY));', UNIT, 'FAIL cd_fonts_wait: a font still loading'),
  ('fonts-ready-sync', 'shim/cdom.js', '    doc.fonts.ready.then(() => answer(FONTS_READY));', '    if (doc.fonts.status === "loaded") {\n      answer(FONTS_READY);\n    }\n    doc.fonts.ready.then(() => answer(FONTS_READY));', UNIT, 'FAIL cd_fonts_wait: nothing loading'),
  ('fonts-token', 'shim/cdom.js', '      exports.cd_fonts_done(token, status);', '      exports.cd_fonts_done(token + 1, status);', UNIT, 'FAIL cd_fonts_wait: nothing loading'),
  ('fonts-done-no-flush', 'src/cdom_fonts.c', '  cd_on_fonts(token, status);\n  cd_flush();', '  cd_on_fonts(token, status);', UNIT, 'FAIL cd_fonts_wait: nothing loading'),
  ('fonts-no-listen', 'shim/cdom.js', '      doc.fonts.addEventListener("loadingdone", fontsChanged);', '', UNIT, 'FAIL cd_on_fonts_changed'),
  ('fonts-listen-loading', 'shim/cdom.js', 'doc.fonts.addEventListener("loadingdone", fontsChanged);', 'doc.fonts.addEventListener("loading", fontsChanged);', UNIT, 'FAIL cd_on_fonts_changed'),
  ('fonts-changed-no-flush', 'src/cdom_fonts.c', '  cd_on_fonts_changed();\n  cd_flush();', '  cd_on_fonts_changed();', UNIT, 'FAIL cd_on_fonts_changed'),
  ('fonts-no-check', 'shim/cdom.js', '    if (!doc.fonts) {\n      Promise.resolve().then(() => answer(FONTS_UNAVAILABLE));\n      return;\n    }\n', '', UNIT, 'FAIL no document.fonts'),
  ('fonts-unavailable-sync', 'shim/cdom.js', '      Promise.resolve().then(() => answer(FONTS_UNAVAILABLE));', '      answer(FONTS_UNAVAILABLE);', UNIT, 'FAIL no document.fonts'),
  ('fonts-unavailable-is-ready', 'shim/cdom.js', 'answer(FONTS_UNAVAILABLE)', 'answer(FONTS_READY)', UNIT, 'FAIL no document.fonts'),
  ('fonts-unavailable-falls-through', 'shim/cdom.js', '      Promise.resolve().then(() => answer(FONTS_UNAVAILABLE));\n      return;', '      Promise.resolve().then(() => answer(FONTS_UNAVAILABLE));', UNIT, 'FAIL no document.fonts'),
  # host access, canvas and frames (main's line, ported onto the direct calls)
  ('host-no-queue', 'shim/cdom.js', '    if (busy) {\n      if (queue.push', '    if (false) {\n      if (queue.push', HOST, 'FAIL focus() inside a batch'),
  # a waiting event goes out only while its id names the node it was queued for (drain)
  ('deferred-no-check', 'shim/cdom.js', '      if (nodes[id] === node) {', '      if (true) {', UNIT, 'FAIL a deferred focusout from a removed node does not reach the new node'),
  ('deferred-only-gone', 'shim/cdom.js', '      if (nodes[id] === node) {', '      if (nodes[id] !== undefined) {', UNIT, 'FAIL a deferred focusout from a removed node does not reach the new node'),
  ('deferred-only-reused', 'shim/cdom.js', '      if (nodes[id] === node) {', '      if (nodes[id] === node || nodes[id] === undefined) {', UNIT, 'FAIL a deferred focusout from a removed node whose id stays free'),
  ('deferred-drop-all', 'shim/cdom.js', '      if (nodes[id] === node) {', '      if (false) {', UNIT, 'FAIL a deferred focusout from a node still there'),
  ('deferred-drop-all-host', 'shim/cdom.js', '      if (nodes[id] === node) {', '      if (false) {', HOST, 'FAIL focusin delivered after the batch'),
  ('host-record-id', 'shim/cdom.js', '    const listener = (event) => post(type, id, event, 0, 0);', '    const listener = (event) => post(type, 0, event, 0, 0);', HOST, 'FAIL window blur and focus on CD_WINDOW'),
  ('host-record-id-unit', 'shim/cdom.js', '    const listener = (event) => post(type, id, event, 0, 0);', '    const listener = (event) => post(type, 0, event, 0, 0);', UNIT, 'FAIL the document'),
  ('host-capture-off', 'shim/cdom.js', '{ capture: (type >= FOCUS && type <= PAUSE) || type === CANCEL || type === CLOSE, passive: false }', '{ capture: false, passive: false }', HOST, 'FAIL img load delivered'),
  ('dialog-no-capture', 'shim/cdom.js', ' || type === CANCEL || type === CLOSE, passive: false }', ', passive: false }', UNIT, 'FAIL a <dialog>'),
  ('host-mask-one-word', 'shim/cdom.js', 'listenMask[node.$i * 2 + word] & bit', 'listenMask[node.$i * 2] & bit', HOST, 'FAIL synthetic'),
  ('host-pointer-fields', 'shim/cdom.js', '    store32(evPointer, pointer ? event.pointerId : 0);', '    store32(evPointer, 0);', HOST, 'FAIL pointerdown with pointer id'),
  ('host-wheel-dy', 'shim/cdom.js', '    storeF32(evDy, wheel ? event.deltaY : 0);', '    storeF32(evDy, 0);', HOST, 'FAIL wheel deltaY'),
  ('host-no-stop', 'shim/cdom.js', '      event.stopPropagation();', '', HOST, 'FAIL click stopped at the mount'),
  ('host-stop-c', 'src/cdom.c', 'void cd_stop_propagation(void) { inbox.flags |= CD_STOP; }', 'void cd_stop_propagation(void) { }', HOST, 'FAIL click stopped at the mount'),
  ('host-flags-kept', 'src/cdom.c', '  inbox.flags = 0;\n  uint32_t id = (uint32_t)inbox.id;', '  uint32_t id = (uint32_t)inbox.id;', HOST, 'FAIL the next click is not prevented'),
  ('host-reserved-free', 'src/cdom.c', '  if (id < CD_FIRST_ID) return;\n  walk_done(); j_free(id);', '  walk_done(); j_free(id);', HOST, 'unreachable'),
  ('host-first-id', 'src/cdom.c', 'static cd_id next_id = CD_FIRST_ID;', 'static cd_id next_id = 1;', HOST, 'FAIL node(CD_WINDOW)'),
  ('host-window-unbound', 'shim/cdom.js', '    nodes[WINDOW] = window;\n', '', HOST, 'page did not start'), # cd_main listens on CD_WINDOW, so the page fails at start
  ('host-set-custom', 'shim/cdom.js', '        object.setProperty(property, value);', '        object[property] = value;', HOST, 'FAIL set custom property'),
  ('host-call-result', 'shim/cdom.js', '      nodes[RESULT] = result;\n', '', HOST, 'FAIL contains(node) read back'),
  ('host-val-obj', 'shim/cdom.js', '          object[key] = value;', '', HOST, 'FAIL scrollIntoView'),
  ('host-val-num', 'shim/cdom.js', '      case 0: return [loadF64((at + valNum) >> 1), index + 1];', '      case 0: return [load32(at + valNum), index + 1];', HOST, 'FAIL set number'),
  ('host-get-id-result', 'shim/cdom.js', '    return index === RESULT ? nodes.indexOf(value, RESULT + 1) : index;', '    return index;', HOST, 'FAIL id of a node CD_RESULT also holds'),
  ('host-path-cache-shared', 'shim/cdom.js', 'return segments[(path - litBase) >> 2] ??= str(path).split(".");', 'return segments[0] ??= str(path).split(".");', HOST, 'FAIL'),
  ('host-unlisten-node', 'shim/cdom.js', '      listenMask[id * 2 + (type >> 5)] &= ~(1 << (type & 31));', '', HOST, 'FAIL cd_unlisten: the node no longer hears'),
  ('host-input-value', 'src/cdom.c', 'get(id, cd_s("value"), GET_STR_LEN, dst, cap)', 'get(id, cd_s("valu"), GET_STR_LEN, dst, cap)', HOST, 'FAIL cd_input_whole'),
  ('input-text-written', 'shim/cdom.js', 'return kind === 1 || read === text.length ? written : written + textEncoder.encode(text.slice(read)).length;', 'return written;', UNIT, 'FAIL cd_input_whole: the whole value'),
  ('input-text-kind', 'src/cdom.c', 'get(id, cd_s("value"), GET_STR_LEN, dst, cap)', 'get(id, cd_s("value"), GET_STR, dst, cap)', UNIT, 'FAIL cd_input_whole: the whole value'),
  ('seed-state-literal', 'src/cdom.c', 'static uint32_t rand_state;', 'static uint32_t rand_state = 0x9e3779b9u;', LINK, 'FAIL src/cdom.c:'),
  ('seed-app-generator', 'demo/click.c', 'static void put32(', 'static uint32_t g;\n__attribute__((unused)) static void g1(void) { g ^= g << 13; }\nstatic void put32(', LINK, 'FAIL demo/click.c:13: a generator of its own'),
  ('rand-shift', 'src/cdom.c', '  rand_state ^= rand_state >> 17;', '  rand_state ^= rand_state >> 16;', UNIT, 'FAIL cd_rand: the first 8 draws'),
  ('rand-seed-not-odd', 'src/cdom.c', '  rand_state = seed | 1;', '  rand_state = seed;', UNIT, 'FAIL cd_rand: the first 8 draws under seed 0'),
  ('seed-call-literal', 'tests/listfuzz.c', 'static uint32_t rnd(uint32_t m) { rng ^=', 'static void rng_seed(uint32_t s) { rng = s; }\nstatic uint32_t rnd(uint32_t m) { if (!rng) rng_seed(42); rng ^=', LINK, 'seeds a generator with a literal, not cd_seed: static uint32_t rnd(uint32_t m) { if (!rng) rng_seed(42);'),
  ('seed-allow-stale', 'tests/link/alloc.c', 'uint32_t rng = 99;', 'uint32_t rng = 98;', LINK, 'FAIL tests/link/alloc.c:'),
  ('unit-host-by-attribute', 'shim/cdom.js', '        object[property] = value;', '        object.setAttribute(property, String(value));', UNIT, 'FAIL cd_set_str, cd_set_bool and cd_set_num'),
  ('unit-host-bool-nan', 'shim/cdom.js', 'return typeof value === "boolean" ? +value : typeof value === "number" ? value : NaN;', 'return typeof value === "number" ? value : NaN;', UNIT, 'FAIL cd_get_num and cd_get_str'),
  ('unit-host-set-swallowed', 'shim/cdom.js', '      reportError(error);\n    }\n  }\n  // object\'s method key with args.', '    }\n  }\n  // object\'s method key with args.', UNIT, 'FAIL a set along a missing path'),
  ('unit-host-call-skipped', 'shim/cdom.js', '      const result = invoke(object, keys[keys.length - 1], args);', '      const result = undefined;', UNIT, 'FAIL cd_call: focus and blur'),
  ('seed-option-ignored', 'shim/cdom.js', '    if (options && options.seed !== undefined) {', '    if (false) {', UNIT, 'FAIL options.seed is cd_seed'),
  ('seed-math-random', 'shim/cdom.js', '    return crypto.getRandomValues(new Uint32Array(1))[0];', '    return (Math.random() * 4294967296) >>> 0;', UNIT, 'FAIL the shim draws no stray random'),
  ('url-step-negated', 'shim/cdom.js', 'history.go(delta)', 'history.go(-delta)', UNIT, 'FAIL cd_url_step'),
  ('canvas-blit-offset', 'shim/cdom.js', 'pixels(pixelsPtr, width, height, 4, canvas.$g)', 'pixels(pixelsPtr + 4, width, height, 4, canvas.$g)', CANVAS, 'blit: byte'),
  ('frames-double', 'src/cdom.c', '  if (frames_on && !frames_asked) {', '  if (frames_on && (frames_asked || 1)) {', CANVAS, 'frame loop doubled'),
  ('frames-backwards', 'shim/cdom.js', 'exports.cd_raf(token, time))', 'exports.cd_raf(token, -time))', CANVAS, 'frame times going backwards'),
  ('frames-no-stop', 'src/cdom.c', '  if (!frames_on) return;\n', '', CANVAS, 'frames after a stop from outside a frame'),
  ('frames-token', 'src/cdom.c', '  if (token == CD_FRAMES_TOKEN) frame(t); else', '  if (0) frame(t); else', CANVAS, 'frames: '),
  ('call-noop', 'shim/cdom.js', '      const result = invoke(object, keys[keys.length - 1], args);', '      const result = undefined;', CANVAS, 'call: pinged'),
  ('node-export', 'shim/cdom.js', 'node: (id) => nodes[id]', 'node: () => null', CANVAS, 'node(id) is not'),
  # app helpers: cd_open_id
  ('open-id-before', 'src/cdom.c', '  (cd_open)(tag);\n  cd_id id = cd_id_new();\n  cd_name_open(id);', '  cd_id id = cd_id_new();\n  cd_name_open(id);\n  (cd_open)(tag);', UNIT, 'FAIL cd_open_id'),
  ('open-id-unnamed', 'src/cdom.c', '  cd_name_open(id);\n  return id;', '  return id;', UNIT, 'FAIL cd_open_id'),
  ('open-id-unrecorded', 'src/cdom.c', '  (cd_open)(tag);\n  cd_id id = cd_id_new();', '  (cd_open)(tag);\n  cd_region *r = recording;\n  recording = 0;\n  cd_id id = cd_id_new();\n  recording = r;', UNIT, 'FAIL cd_open_id'),
  # app helpers: cd_on
  ('on-no-drop-on-free', 'src/cdom.c', '  if (id < on_cap && on_ids[id]) on_drop(id);', '  (void)on_drop;', UNIT, 'FAIL cd_on: 300 ids'),
  ('on-forget-keeps-mark', 'shim/cdom.js', '    if (node && node.$i === id) {\n      node.$i = undefined;\n    }\n', '', UNIT, 'FAIL cd_on: 300 ids'),
  ('on-del-no-shift', 'src/cdom.c', '    t->e[i] = t->e[j];\n    i = j;', '    return;', UNIT, 'FAIL cd_on: 300 ids'),
  ('on-always-fallback', 'src/cdom.c', '  if (h) ((cd_event_fn)h->p)(&inbox, h->q);\n  else cd_on_event(&inbox);', '  (void)h;\n  cd_on_event(&inbox);', UNIT, 'FAIL cd_on: a handler'),
  ('on-no-ctx', 'src/cdom.c', '((cd_event_fn)h->p)(&inbox, h->q)', '((cd_event_fn)h->p)(&inbox, 0)', UNIT, 'FAIL cd_on: a handler'),
  ('on-any-type', 'src/cdom.c', 'pair_find(&handlers, id, (uint32_t)inbox.type) : 0;', 'pair_any(&handlers, id) : 0;', UNIT, 'FAIL cd_on: a handler'),
  ('on-no-sel', 'src/cdom.c', '  if (!host) cd_sel(id); /*', '  (void)host; /*', UNIT, 'FAIL cd_on: a handler'),
  ('on-remove-keeps-listen', 'src/cdom.c', '    if (!pair_any(&handlers, id)) on_ids[id] = 0;\n    cd_unlisten(id, ev);', '    if (!pair_any(&handlers, id)) on_ids[id] = 0;', UNIT, 'FAIL cd_on: a handler'),
  ('on-put-appends', 'src/cdom.c', '  pair_ent *e = pair_find(t, a, b);\n  if (e) { e->p = p; e->q = q; return; }\n', '', UNIT, 'FAIL cd_on: a handler'),
  ('on-flags-after', 'src/cdom.c', '  inbox.flags = 0;\n  uint32_t id = (uint32_t)inbox.id;', '  uint32_t id = (uint32_t)inbox.id;\n  if (!(id < on_cap && on_ids[id])) inbox.flags = 0;', UNIT, 'FAIL cd_on: a handler'),
  # tests/build.sh links tests/alloc_try.c, an app with no cd_on_event, before tests/link/run.sh's on_only.c can:
  # either one failing to link is the catch, so the text is the linker's, which both print
  ('on-event-strong', 'src/cdom.c', '__attribute__((weak)) void cd_on_event(const cd_event_rec *) { }\n', '', LINK, 'undefined symbol: cd_on_event'),
  # app helpers: cd_timer_stop and cd_timer_cb
  ('timer-stop-no-clear', 'shim/cdom.js', '    clearTimeout(handle);\n', '', UNIT, 'FAIL cd_timer_stop and cd_timer_cb'),
  ('timer-zero-timeout', 'shim/cdom.js', '    if (!ms) {\n      const handle = 0x40000000', '    if (false) {\n      const handle = 0x40000000', UNIT, 'FAIL a 0 ms timer is the next task'),
  ('timer-zero-stop-ignored', 'shim/cdom.js', '    if (soon[handle]) {\n      delete soon[handle];\n      return;\n    }\n', '', UNIT, 'FAIL a 0 ms timer is the next task'),
  ('timer-zero-fires-stopped', 'shim/cdom.js', '        if (soon[handle]) {\n          delete soon[handle];\n          exports.cd_timer', '        if (true) {\n          delete soon[handle];\n          exports.cd_timer', UNIT, 'FAIL a 0 ms timer is the next task'),
  ('timer-stop-one', 'src/cdom.c', 'for (pair_ent *e; (e = pair_any(&timers, token));) {', 'for (pair_ent *e = pair_any(&timers, token); e; e = 0) {', UNIT, 'FAIL cd_timer_stop and cd_timer_cb'),
  ('timer-stop-keeps-slot', 'src/cdom.c', '  cd_cb_drop(token);\n}', '}', UNIT, 'FAIL cd_timer_stop and cd_timer_cb'),
  ('timer-same-generation', 'src/cdom.c', '  c->gen = (c->gen + 1) & 0x7fffu;\n', '', UNIT, 'FAIL cd_timer_stop and cd_timer_cb'),
  ('timer-cb-no-ctx', 'src/cdom.c', '((cd_timer_fn)fn)(ctx);', '((cd_timer_fn)fn)(0);', UNIT, 'FAIL cd_timer_stop and cd_timer_cb'),
  ('timer-cb-to-app', 'src/cdom.c', '  if (token < CD_TOKEN_CDOM) cd_on_timer(token);', '  if (1) cd_on_timer(token);', UNIT, 'FAIL cd_timer_stop and cd_timer_cb'),
  ('timer-app-unchecked', 'src/cdom.c', '  if (token >= CD_TOKEN_CDOM) __builtin_trap(); /* cdom\'s range */\n', '', UNIT, 'FAIL cd_timer_stop and cd_timer_cb'),
  ('timer-stale-token-live', 'src/cdom.c', '  return c->live && c->gen == ((token >> 16) & 0x7fffu) ? c : 0;', '  return c->live ? c : 0;', UNIT, 'FAIL cd_timer_stop and cd_timer_cb'),
  # app helpers: cd_http_cb
  ('httpcb-to-app', 'src/cdom_net.c', '  if (token < CD_TOKEN_CDOM) cd_on_http(token, status, p, len);', '  if (1) cd_on_http(token, status, p, len);', UNIT, 'FAIL cd_http_cb'),
  ('httpcb-no-ctx', 'src/cdom_net.c', '((cd_http_fn)fn)(ctx, status, p, len);', '((cd_http_fn)fn)(0, status, p, len);', UNIT, 'FAIL cd_http_cb'),
  ('httpcb-status', 'src/cdom_net.c', '((cd_http_fn)fn)(ctx, status, p, len);', '((cd_http_fn)fn)(ctx, 200, p, len);', UNIT, 'FAIL cd_http_cb'),
  ('httpcb-app-token', 'src/cdom_net.c', '  http(cd_cb_new((cd_cb_fn)fn, ctx), method', '  http(cd_cb_new((cd_cb_fn)fn, ctx) & 0x7fffffffu, method', UNIT, 'FAIL cd_http_cb'),
  ('httpcb-unchecked', 'src/cdom_net.c', '  if (token >= CD_TOKEN_CDOM) __builtin_trap(); /* cdom\'s range: its answer', '  /* cdom\'s range: its answer', UNIT, 'FAIL cd_http_cb'),
  # the fallback's rebuild rule (tools/legacy/build.mjs)
  # (a dropped input the build reads, like polyfills.js, fails tests/build.sh itself: write() refuses it)
  ('fresh-input-dropped', 'tools/legacy/build.mjs', "path.join(top, 'node_modules/.package-lock.json')];", '];', FRESH, 'FAIL shim does not name node_modules/.package-lock.json'),
  ('fresh-output-unchecked', 'tools/legacy/build.mjs', 'return got === want && made === sha(fs.readFileSync(out));', 'return got === want;', FRESH, 'FAIL shim: an edited output was not rebuilt'),
  ('fresh-by-path', 'tools/legacy/build.mjs', "try { c = sha(fs.readFileSync(f)); } catch { c = 'missing'; }", "c = 'x';", FRESH, 'FAIL changing tools/legacy/build.mjs rebuilt []'),
  ('mini-strong', 'src/mini.c', '__attribute__((weak, no_builtin)) int memcmp', '__attribute__((no_builtin)) int memcmp', LINK, "FAIL an app's own libc"),
  # a library's I/O: cd_ws_open_cb
  ('wscb-open-to-hooks', 'src/cdom_net.c', '  if (!t) cd_on_ws_open(id);\n', '  if (1) cd_on_ws_open(id);\n', UNIT, 'FAIL cd_ws_open_cb'),
  ('wscb-msg-to-hooks', 'src/cdom_net.c', '  if (!t) cd_on_ws_msg(id, p, len);\n', '  if (1) cd_on_ws_msg(id, p, len);\n', UNIT, 'FAIL cd_ws_open_cb'),
  ('wscb-close-to-hooks', 'src/cdom_net.c', '  if (!t) cd_on_ws_close(id, code);\n', '  if (1) cd_on_ws_close(id, code);\n', UNIT, 'FAIL cd_ws_open_cb'),
  ('wscb-hooks-bypassed', 'src/cdom_net.c', '  if (!t) cd_on_ws_msg(id, p, len);\n', '  if (0) cd_on_ws_msg(id, p, len);\n', UNIT, 'FAIL net'),
  ('wscb-every-socket-cb', 'src/cdom_net.c', 'static uint32_t ws_token(uint32_t id) { return id < ws_cb_n ? ws_cb[id] : 0; }', 'static uint32_t ws_token(uint32_t id) { (void)id; return ws_cb_n ? ws_cb[0] : 0; }', UNIT, 'FAIL cd_ws_open_cb'),
  ('wscb-no-ctx', 'src/cdom_net.c', '((cd_ws_fn)fn)(ctx, id, CD_WS_MSG, p, len);', '((cd_ws_fn)fn)(0, id, CD_WS_MSG, p, len);', UNIT, 'FAIL cd_ws_open_cb'),
  ('wscb-close-code', 'src/cdom_net.c', '((cd_ws_fn)fn)(ctx, id, CD_WS_CLOSE, 0, code);', '((cd_ws_fn)fn)(ctx, id, CD_WS_CLOSE, 0, 0);', UNIT, 'FAIL cd_ws_open_cb'),
  ('wscb-id-kept', 'src/cdom_net.c', '    ws_cb[id] = 0;\n', '', UNIT, 'FAIL cd_ws_open_cb'),
  ('wscb-slot-kept', 'src/cdom_net.c', 'if (cd_cb_take(t, &fn, &ctx)) ((cd_ws_fn)fn)', 'if (cd_cb_get(t, &fn, &ctx)) ((cd_ws_fn)fn)', UNIT, 'FAIL cd_ws_open_cb cycles'),
  ('cb-get-frees', 'src/cdom.c', 'int cd_cb_get(uint32_t token, cd_cb_fn *fn, void **ctx) { return cb_read(token, fn, ctx) != 0; }', 'int cd_cb_get(uint32_t token, cd_cb_fn *fn, void **ctx) { return cd_cb_take(token, fn, ctx); }', UNIT, 'FAIL cd_ws_open_cb'),
  # a library's I/O: the store's _cb forms
  ('storecb-get-to-app', 'src/cdom_store.c', '  if (token < CD_TOKEN_CDOM) cd_on_store(token, p, len);', '  if (1) cd_on_store(token, p, len);', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-put-to-app', 'src/cdom_store.c', '  if (token < CD_TOKEN_CDOM) cd_on_store_put(token, ok);', '  if (1) cd_on_store_put(token, ok);', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-del-to-app', 'src/cdom_store.c', '  if (token < CD_TOKEN_CDOM) cd_on_store_del(token, ok);', '  if (1) cd_on_store_del(token, ok);', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-hooks-bypassed', 'src/cdom_store.c', '  if (token < CD_TOKEN_CDOM) cd_on_store(token, p, len);', '  if (0) cd_on_store(token, p, len);', UNIT, 'FAIL cd_store: a put'),
  ('storecb-get-no-ctx', 'src/cdom_store.c', '((cd_store_fn)fn)(ctx, p, len);', '((cd_store_fn)fn)(0, p, len);', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-put-no-ctx', 'src/cdom_store.c', '((cd_store_done_fn)fn)(ctx, ok);\n  cd_flush();\n}\n__attribute__((weak)) void cd_on_store_del', '((cd_store_done_fn)fn)(0, ok);\n  cd_flush();\n}\n__attribute__((weak)) void cd_on_store_del', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-get-len', 'src/cdom_store.c', '((cd_store_fn)fn)(ctx, p, len);', '((cd_store_fn)fn)(ctx, p, len < 0 ? 0 : len);', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-slot-kept', 'src/cdom_store.c', 'else if (cd_cb_take(token, &fn, &ctx)) ((cd_store_fn)fn)', 'else if (cd_cb_get(token, &fn, &ctx)) ((cd_store_fn)fn)', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-get-unchecked', 'src/cdom_store.c', 'uint32_t deadline_ms) {\n  if (token >= CD_TOKEN_CDOM) __builtin_trap();\n', 'uint32_t deadline_ms) {\n', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-put-unchecked', 'src/cdom_store.c', 'const void *p, uint32_t len) {\n  if (token >= CD_TOKEN_CDOM) __builtin_trap();\n', 'const void *p, uint32_t len) {\n', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-del-unchecked', 'src/cdom_store.c', 'void cd_store_del(uint32_t token, const char *key, uint32_t klen) {\n  if (token >= CD_TOKEN_CDOM) __builtin_trap();\n', 'void cd_store_del(uint32_t token, const char *key, uint32_t klen) {\n', UNIT, 'FAIL cd_store_put_cb'),
  ('storecb-app-token', 'src/cdom_store.c', '  j_store_get(cd_cb_new((cd_cb_fn)fn, ctx), key', '  j_store_get(cd_cb_new((cd_cb_fn)fn, ctx) & 0x7fffffffu, key', UNIT, 'FAIL cd_store_put_cb'),
  # the program's string.h (src/libc) and mini.c's copy and fill
  ('libc-no-builtin', 'src/libc/string.h', '#define memcpy(dst, src, n) __builtin_memcpy((dst), (src), (n))\n', '', LINK, 'FAIL string.h (freestanding flags): undefined memcpy'),
  ('libc-no-real-copy', 'src/mini.c', '__attribute__((weak)) void *memcpy(void *restrict d, const void *restrict s, size_t n) { return __builtin_memcpy(d, s, n); }\n', '', LINK, "FAIL string.h (cdom flags): taking memcpy's address"),
  # offsets from cd_layout, none in the shim: a literal put back (each still right at run time, so only the
  # scan, tools/offsets.mjs, sees it), and the map and the shim out of step (the page sees each field)
  ('offsets-literal', 'shim/cdom.js', '    store32(evButtons, pointer ? event.buttons : 0);', '    store32(evType + 13, pointer ? event.buttons : 0);', UNIT, 'FAIL the shim indexes C memory by a literal offset: cdom.js:'),
  ('offsets-bare-base', 'shim/cdom.js', '    const tag = load32(at + valTag);', '    const tag = load32(at);', UNIT, 'FAIL the shim indexes C memory by a literal offset: cdom.js:'),
  ('offsets-size', 'shim/cdom.js', 'const at = (ptr >> 2) + index * valWords;', 'const at = (ptr >> 2) + index * 4;', UNIT, 'FAIL the shim indexes C memory by a literal offset: cdom.js:'),
  ('offsets-rec-step', 'shim/cdom.js', 'const at = (tmpRecords >> 2) + k * recWords;', 'const at = (tmpRecords >> 2) + k * 2;', UNIT, 'FAIL the shim indexes C memory by a literal offset: cdom.js:'),
  ('offsets-event-order', 'src/cdom.c', 'X(offsetof(cd_event_rec, pointer)) \\\n  X(offsetof(cd_event_rec, buttons))', 'X(offsetof(cd_event_rec, buttons)) \\\n  X(offsetof(cd_event_rec, pointer))', UNIT, "FAIL the shim's view of C's structs"),
  ('offsets-val-order', 'src/cdom.c', 'X(offsetof(cd_val, tag)) X(offsetof(cd_val, num))', 'X(offsetof(cd_val, num)) X(offsetof(cd_val, tag))', UNIT, "FAIL the shim's view of C's structs"),
  ('offsets-count', 'shim/cdom.js', '    valU = word();\n', '', UNIT, 'lay out C memory differently'),
  # a socket the browser refuses to make (unit.c 32): REFUSED then CLOSE from the event loop, the id held till then
  ('wsref-sync', 'shim/cdom.js', '      setTimeout(() => {\n        exports.cd_net_refused(id);', '      ((f) => f())(() => {\n        exports.cd_net_refused(id);', UNIT, 'FAIL cd_ws_open_cb refused'),
  ('wsref-no-event', 'src/cdom_net.c', '  if (t && cd_cb_get(t, &fn, &ctx)) ((cd_ws_fn)fn)(ctx, id, CD_WS_REFUSED, 0, 0);', '  if (0 && t && cd_cb_get(t, &fn, &ctx)) ((cd_ws_fn)fn)(ctx, id, CD_WS_REFUSED, 0, 0);', UNIT, 'FAIL cd_ws_open_cb refused'),
  ('wsref-no-close', 'shim/cdom.js', '        exports.cd_net_close(id, 1006); // CD_WS_REFUSED_CODE', '        // CD_WS_REFUSED_CODE', UNIT, 'FAIL cd_ws_open_cb refused'),
  ('wsref-close-code', 'shim/cdom.js', '        exports.cd_net_close(id, 1006); // CD_WS_REFUSED_CODE', '        exports.cd_net_close(id, 1000); // CD_WS_REFUSED_CODE', UNIT, 'FAIL cd_ws_open_cb refused'),
  ('wsref-thrown', 'shim/cdom.js', '      if (!refusable) {\n        throw error;', '      if (1) {\n        throw error;', UNIT, 'FAIL cd_ws_open_cb refused'),
  ('wsref-cb-not-refusable', 'src/cdom_net.c', 'id = j_ws_open(url, len, 1);', 'id = j_ws_open(url, len, 0);', UNIT, 'FAIL cd_ws_open_cb refused'),
  ('wsref-hook-refusable', 'src/cdom_net.c', '{ return j_ws_open(url, len, 0); }', '{ return j_ws_open(url, len, 1); }', UNIT, 'FAIL cd_ws_open refused'),
  ('wsref-no-stand-in', 'shim/cdom.js', '      sockets[id] = refusedSocket;\n', '      sockets[id] = undefined;\n', UNIT, 'FAIL cd_ws_open_cb refused'),
  ('wsref-id-freed-early', 'shim/cdom.js', '      sockets[id] = refusedSocket;\n', '      sockets[id] = refusedSocket;\n      freeSockets.push(id);\n', UNIT, 'FAIL cd_ws_open_cb refused'),
  ('page-origin-host', 'shim/cdom.js', 'writeString(location.protocol + "//" + location.host,', 'writeString(location.host,', UNIT, 'FAIL cd_page_origin'),
  # the I/O contract (cdom.h, top; tests/link/contract.c). A signature edited in one place is the build's
  # to catch (the definition, the callers and clang's own libc types disagree); contract.c holds what the
  # build does not: a code or a constant given a new value, and a signature edited consistently everywhere
  ('contract-token', 'src/cdom.h', '#define CD_TOKEN_CDOM 0x80000000u', '#define CD_TOKEN_CDOM 0x40000000u', LINK, 'FAIL the I/O contract changed in place'),
  ('contract-ws-close', 'src/cdom_net.h', '#define CD_WS_CLOSE 2', '#define CD_WS_CLOSE 3', LINK, 'FAIL the I/O contract changed in place'),
  ('contract-ws-refused', 'src/cdom_net.h', '#define CD_WS_REFUSED 3', '#define CD_WS_REFUSED 4', LINK, 'FAIL the I/O contract changed in place'),
  ('contract-ws-refused-code', 'src/cdom_net.h', '#define CD_WS_REFUSED_CODE 1006', '#define CD_WS_REFUSED_CODE 1000', LINK, 'FAIL the I/O contract changed in place'),
  ('contract-random-ok', 'src/cdom_random.h', '#define CD_RANDOM_OK 1', '#define CD_RANDOM_OK 2', LINK, 'FAIL the I/O contract changed in place'),
  ('contract-http-timeout', 'src/cdom_net.h', '#define CD_HTTP_TIMEOUT 1 ', '#define CD_HTTP_TIMEOUT 2 ', LINK, 'FAIL the I/O contract changed in place'),
  # the name gate (tools/edges.mjs): the other repo's name, spelled in pieces here so this file does not carry it
  ('edges-name', 'src/cdom.h', '#ifndef CDOM_H\n', '/* built beside ' + 'c' + 'base */\n#ifndef CDOM_H\n', LINK, 'names the other repo: /* built beside'),
  ('edges-old-name', 'tools/fuzz.mjs', 'import http from', '// from C_' + 'BASE\nimport http from', LINK, 'FAIL tools/fuzz.mjs:2: names the other repo'),
  # cdom.mk (tests/link/app.mk): the page rule (tools/cdpage), the module rule, and the flag gate (tools/cdomflags.sh)
  ('page-no-shim', 'tools/cdpage.c', '      put_lines(o, shim.p, shim.n);\n', '', LINK, 'FAIL cdom_page (first): the page is not itself'),
  ('page-mark-kept', 'tools/cdpage.c', '      put_lines(o, shim.p, shim.n);\n      continue;\n', '      put_lines(o, shim.p, shim.n);\n', LINK, 'FAIL cdom_page (first): the page is not itself'),
  ('page-inline-taken', 'tools/cdpage.c', '      if (!alone(l, n, SHIM)) die(', '      if (!alone(l, n, SHIM) && 0) die(', LINK, 'FAIL cdom_page (first) took the inline page'),
  ('page-two-taken', 'tools/cdpage.c', '      if (++marks > 1) die(', '      if (++marks > 2) die(', LINK, 'FAIL cdom_page (first) refused the two page for another reason'),
  ('page-none-taken', 'tools/cdpage.c', '  if (marks != 1)\n', '  if (marks > 1)\n', LINK, 'FAIL cdom_page (first) took the none page'),
  ('page-partial-kept', 'tools/cdpage.c', '  if (marks != 1)\n', '  if (marks != 1 && fopen(a->out, "w"))\n', LINK, 'FAIL cdom_page (first) refused the none page but left a file'),
  ('mk-wasm-no-libc', 'cdom.mk', 'CDOM_CFLAGS := --target=wasm32 -std=c23 -nostdlib -mbulk-memory -isystem $(CDOM)/src/libc -I$(CDOM)/src', 'CDOM_CFLAGS := --target=wasm32 -std=c23 -nostdlib -mbulk-memory -I$(CDOM)/src', LINK, "fatal error: 'stdlib.h' file not found"),  # tests/build.sh takes cdom.mk's flags (tools/cdomflags.sh): its first module stops at cdom.c's include
  # cdlits, which the rule runs on the linked module, refuses a module with no cdom in it before run.sh can look at its exports
  ('mk-wasm-no-cdom', 'cdom.mk', 'cdom_mk_app_src = $(APP_SRC) $(CDOM_SRC)', 'cdom_mk_app_src = $(APP_SRC)', LINK, "cdlits: not a cdom module"),
  ('mk-modules-dropped', 'cdom.mk', ' $(foreach m,$(APP_MODULES),$(CDOM_$(call cdom_mk_up,$(m))_SRC))', '', LINK, "FAIL cdom.mk's APP_WASM rule (first) does not build an app"),
  ('mk-modules-unchecked', 'cdom.mk', 'cdom_mk_known = $(if $(cdom_mk_unknown),', 'cdom_mk_known = $(if ,', LINK, 'FAIL cdom.mk (first) took APP_MODULES naming no module'),  # an unknown module has no sources, so make builds the app without it
  ('mk-goal-taken', 'cdom.mk', '\n.cdom-mk-sources:', '\ncdom-mk-sources:', LINK, 'FAIL cdom.mk (first) took the default goal'),
  ('mk-read-early', 'cdom.mk', 'cdom_mk_app_src = $(APP_SRC)', 'cdom_mk_app_src := $(APP_SRC)', LINK, "FAIL cdom.mk's APP_WASM rule (first) does not build an app"),
  ('mk-target-early', 'cdom.mk', '%.wasm: $$(call cdom_mk_for,$$(APP_WASM) $$(cdom_mk_simd_wasm),', '$(APP_WASM): $$(call cdom_mk_for,$$(APP_WASM) $$(cdom_mk_simd_wasm),', LINK, "FAIL cdom.mk's APP_WASM rule (first) does not build an app"),
  ('mk-page-early', 'cdom.mk', '%.html: $$(call cdom_mk_for,$$(APP_PAGE),', '$(APP_PAGE): $$(call cdom_mk_for,$$(APP_PAGE),', LINK, 'FAIL cdom_page (first) refused a page whose marker is alone'),
  ('mk-any-wasm', 'cdom.mk', 'cdom_mk_for = $(if $(filter $(2),$(1)),$(3),$(2).cdom-mk-none)', 'cdom_mk_for = $(3)', LINK, "FAIL cdom.mk's rules built a .wasm or .html that is not APP_WASM or APP_PAGE"),
  ('mk-missing-unnamed', 'cdom.mk', '\n.cdom-mk-sources: $$(if $$(APP_WASM),$$(APP_WASM) $$(cdom_mk_app_src) $$(call cdom_mk_objs,$$(APP_WASM)) $$(CDOM_LITS)) \\\n  $$(if $$(cdom_mk_simd_wasm),$$(cdom_mk_simd_wasm) $$(call cdom_mk_objs,$$(cdom_mk_simd_wasm))) \\\n  $$(if $$(APP_PAGE),$$(cdom_mk_page_src) $$(CDOM_PAGE_TOOL) $$(APP_PAGE).csslist)\n', '\n', LINK, 'FAIL cdom.mk built an app whose APP_SRC names a file that is not there'),  # the rule no longer applies, and the module built before is kept
  ('mk-rebuilt', 'cdom.mk', '$$(CDOM_LITS) $$(if $$(filter $$@,$$(cdom_mk_simd_wasm)),$$(APP_WASM)))', '$$(CDOM_LITS) $$(if $$(filter $$@,$$(cdom_mk_simd_wasm)),$$(APP_WASM)) .cdom-mk-sources)', LINK, "FAIL cdom.mk's APP_WASM rule (first): a second make builds the module again"),
  # The page's module (cdom.mk's APP_PAGE rule, tools/cdpage): bundled by default, or named (APP_WASM_SEPARATE)
  ('mk-bundle-off', 'cdom.mk', 'cdom_mk_bundled = $(if $(APP_WASM_SEPARATE),,$(APP_WASM))', 'cdom_mk_bundled =', LINK, 'FAIL APP_PAGE (first) bundled into a page that loads its module by URL'),
  ('mk-separate-ignored', 'cdom.mk', 'cdom_mk_bundled = $(if $(APP_WASM_SEPARATE),,$(APP_WASM))', 'cdom_mk_bundled = $(APP_WASM)', LINK, 'FAIL cdom_page (first) refused a page whose marker is alone'),
  ('mk-b64-wrapped', 'tools/cdpage.c', '    bput(o, q, 4);\n  }\n  if (n - i == 1) {', "    bput(o, q, 4);\n    if ((i + 3) % 57 == 0) bbyte(o, '\\n');\n  }\n  if (n - i == 1) {", LINK, 'FAIL the default page (first), bundled, is not shim/page.html'),  # base64 in lines of 76, as GNU base64 writes it, not one line
  ('mk-page-no-wasm-dep', 'cdom.mk', '$$(CDOM_PAGE_TOOL) $$(APP_WASM) $$(cdom_mk_simd_wasm)', '$$(CDOM_PAGE_TOOL) $$(cdom_mk_simd_wasm)', LINK, 'FAIL the default page (first), bundled, is up to date with a newer module'),
  ('mk-no-default-page', 'cdom.mk', 'cdom_mk_page_src = $(or $(APP_PAGE_SRC),$(CDOM_PAGE))', 'cdom_mk_page_src = $(APP_PAGE_SRC)', LINK, 'FAIL the default page (first) does not build'),
  ('mk-title-full-name', 'cdom.mk', '$(or $(APP_TITLE),$(basename $(notdir $(APP_WASM))))', '$(or $(APP_TITLE),$(notdir $(APP_WASM)))', LINK, 'FAIL the default page (first), bundled, is not shim/page.html'),
  ('mk-wasm-intermediate', 'cdom.mk', '$$(if $$(APP_WASM),$$(APP_WASM) $$(cdom_mk_app_src)', '$$(if $$(APP_WASM),$$(cdom_mk_app_src)', LINK, "FAIL the starter's page does not start its bundled module"),  # make deletes the module after the page
  ('mk-toolchain-unchecked', 'cdom.mk', 'cdom_mk_toolchain = printf', 'cdom_mk_toolchain = true || printf', LINK, 'no_wasm32_clang, which cannot build wasm32, did not stop'),  # tests/link/no_wasm32_clang, a compiler with no wasm32 target on every OS
  # the wasm-ld version check (tests/link/stub_clang.sh): LLD 18 and 22 refused with the version and the fix, LLD 23 builds
  ('mk-toolchain-no-rodata', 'cdom.mk', 'const char *f(void) { return __rodata_start; }', 'void f(void) {}', LINK, 'FAIL cdom.mk with wasm-ld 18.1.3, older than LLVM 23, did not stop at the toolchain check naming its version'),
  ('mk-toolchain-no-version', 'cdom.mk', 'links with $$("$$($(WASM_CC) --target=wasm32 -print-prog-name=wasm-ld)" --version 2> /dev/null | head -1), which', 'links with a wasm-ld which', LINK, 'FAIL cdom.mk with wasm-ld 18.1.3, older than LLVM 23, did not stop at the toolchain check naming its version'),
  ('mk-toolchain-no-fix', 'cdom.mk', '  echo "cdom.mk: $(cdom_mk_install)" >&2; \\\n', '', LINK, 'FAIL cdom.mk with wasm-ld 18.1.3, older than LLVM 23, did not stop at the toolchain check naming its version'),
  ('mk-toolchain-refuses-23', 'cdom.mk', 'const char *f(void) { return __rodata_start; }', 'const char *f(void) { return __rodata_start + __rodata_startx[0]; }', LINK, 'FAIL cdom.mk with wasm-ld from LLVM 23.1.6 did not build the app'),
  ('page-url-unchecked', 'tools/cdpage.c', '  if (a->wasm && !wasms) {', '  if (0) {', LINK, 'FAIL APP_PAGE (first) bundled into a page that loads its module by URL'),
  ('page-url-unnamed', 'tools/cdpage.c', '    if (have_loads)\n', '    if (0)\n', LINK, 'FAIL APP_PAGE (first) refused a page that loads its module by URL with another message'),
  ('page-two-wasm-taken', 'tools/cdpage.c', '      if (++wasms > 1 || find(', '      if (++wasms > 9 && find(', LINK, 'FAIL cdom_page (first) took the two_wasm page'),
  ('page-url-mode-off', 'tools/cdpage.c', '  if (url) {\n    js_string(o, url);', '  if (0) {\n    js_string(o, url);', LINK, 'FAIL the default page (first), APP_WASM_SEPARATE, does not build'),
  ('page-url-unescaped', 'tools/cdpage.c', '    if (c == \'\\\\\' || c == \'"\') { bbyte(o, \'\\\\\'); bbyte(o, c); }', "    if (c == '\\\\') { bbyte(o, '\\\\'); bbyte(o, c); }", LINK, 'FAIL the default page (first), APP_WASM_SEPARATE: APP_WASM_URL'),
  ('page-title-unescaped', 'tools/cdpage.c', '    else if (*s == \'<\') bstr(o, "&lt;");\n', '', LINK, 'FAIL the default page (first), APP_WASM_SEPARATE: not shim/page.html'),
  # tools/cdpage standing alone (tests/page/run.sh): the page from its parts
  ('cdpage-table-after-shim', 'tools/cdpage.c', '      put_lines(o, lits.p, lits.n);\n      if (a->njobs) {', '      if (!slurp(a->shim, &shim)) die("%s: cannot read the shim %s", page_name, a->shim);\n      put_lines(o, shim.p, shim.n);\n      put_lines(o, lits.p, lits.n);\n      if (a->njobs) {', PAGE, 'FAIL the default page, bundled (8-byte module), titled and escaped: the page is not its parts'),
  ('cdpage-no-final-newline', 'tools/cdpage.c', "  if (n && p[n - 1] != '\\n') bbyte(o, '\\n');\n", '', PAGE, 'FAIL the default page, bundled (8-byte module), titled and escaped: the page is not its parts'),
  ('cdpage-b64-unpadded', 'tools/cdpage.c', "    char q[4] = { A[v >> 18], A[v >> 12 & 63], '=', '=' };\n    bput(o, q, 4);", "    char q[4] = { A[v >> 18], A[v >> 12 & 63], '=', '=' };\n    bput(o, q, 2);", PAGE, 'FAIL the default page, bundled (10-byte module), titled and escaped: the page is not its parts'),
  # tools/cdpage, the stylesheets (tests/page/run.sh): discovery and its order, the list file, the </style escape,
  # every refusal of a stylesheet or of the marker, and the edges
  ('css-sort-reversed', 'tools/cdpage.c', 'return strcmp(((const Sheet *)a)->rel, ((const Sheet *)b)->rel);', 'return strcmp(((const Sheet *)b)->rel, ((const Sheet *)a)->rel);', PAGE, 'FAIL discovery: the page holds'),
  ('css-no-sort', 'tools/cdpage.c', '  qsort(s.v, s.n, sizeof *s.v, by_path);\n', '  if (0) qsort(s.v, s.n, sizeof *s.v, by_path);\n', PAGE, 'FAIL discovery:'),
  ('css-sort-case-blind', 'tools/cdpage.c', 'return strcmp(((const Sheet *)a)->rel, ((const Sheet *)b)->rel);', 'const char *x = ((const Sheet *)a)->rel, *y = ((const Sheet *)b)->rel;\n  while (*x && lower((uint8_t)*x) == lower((uint8_t)*y)) x++, y++;\n  return lower((uint8_t)*x) - lower((uint8_t)*y);', PAGE, 'FAIL discovery: the page holds'),
  ('css-no-subfolders', 'tools/cdpage.c', '    if (S_ISDIR(st.st_mode) && !link) walk(dir, r, out);', '    if (S_ISDIR(st.st_mode) && !link) {}', PAGE, 'FAIL discovery: the page holds'),
  ('css-dot-names-taken', 'tools/cdpage.c', "    if (e->d_name[0] == '.') continue;", '    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;', PAGE, 'FAIL discovery: the page holds'),
  ('css-suffix-anywhere', 'tools/cdpage.c', '  return n > 4 && !memcmp(name + n - 4, ".css", 4);', '  return n > 4 && strstr(name, ".css");', PAGE, 'FAIL discovery: the page holds'),
  ('css-link-entered', 'tools/cdpage.c', '    if (S_ISDIR(st.st_mode) && !link) walk(dir, r, out);', '    if (S_ISDIR(st.st_mode)) walk(dir, r, out);', PAGE, 'FAIL discovery: the page holds'),
  ('csslist-always-written', 'tools/cdpage.c', '  if (slurp(out, &old) && old.n == o.n && (!o.n || !memcmp(old.p, o.p, o.n))) return;', '  (void)old;', PAGE, 'FAIL the list file: a second run replaced it'),
  ('csslist-hash-path', 'tools/cdpage.c', 'fnv1a(b.p, b.n)', 'fnv1a((const uint8_t *)s.v[f].rel, strlen(s.v[f].rel))', PAGE, 'FAIL the list file: an edit did not replace it'),
  ('css-unescaped', 'tools/cdpage.c', '      if (p[i] == \'<\' && i + 1 < n && p[i + 1] == \'/\' && starts_ci(p + i + 2, n - i - 2, "style")) { bstr(o, "<\\\\/"); i++; continue; }\n', '', PAGE, 'FAIL the escape'),
  ('css-escape-case', 'tools/cdpage.c', 'starts_ci(p + i + 2, n - i - 2, "style")) { bstr(o, "<\\\\/");', 'n - i - 2 >= 5 && !memcmp(p + i + 2, "style", 5)) { bstr(o, "<\\\\/");', PAGE, 'FAIL the escape'),
  ('css-comment-open-ok', 'tools/cdpage.c', '  if (state == COMMENT) die(', '  if (0) die(', PAGE, 'FAIL refuses the stylesheet comment'),
  ('css-string-newline-ok', 'tools/cdpage.c', "      if (c == '\\n') die(", '      if (0) die(', PAGE, 'FAIL refuses the stylesheet string'),
  ('css-string-open-ok', 'tools/cdpage.c', '  if (state == DQ || state == SQ) die(', '  if (0) die(', PAGE, 'FAIL refuses the stylesheet string_end'),
  ('css-url-open-ok', 'tools/cdpage.c', '  if (state == URL) die(', '  if (0) die(', PAGE, 'FAIL refuses the stylesheet url'),
  ('css-brace-open-ok', 'tools/cdpage.c', '  if (depth) die(', '  if (0) die(', PAGE, 'FAIL refuses the stylesheet open'),
  ('css-brace-stray-ok', 'tools/cdpage.c', '        if (!depth) die(', '        if (0) die(', PAGE, 'FAIL refuses the stylesheet close'),
  ('css-import-ok', 'tools/cdpage.c', '      } else if (c == \'@\' && starts_ci(p + i + 1, n - i - 1, "import")) {', '      } else if (0) {', PAGE, 'FAIL refuses the stylesheet import'),
  ('css-nul-ok', 'tools/cdpage.c', '    if (!c) die(', '    if (0) die(', PAGE, 'FAIL refuses the stylesheet nul'),
  ('css-utf8-ok', 'tools/cdpage.c', '    if (!k) die(', '    if (!k) k = 1;\n    if (0) die(', PAGE, 'FAIL refuses the stylesheet utf8'),
  ('css-unreadable-ok', 'tools/cdpage.c', '    if (!slurp(s->v[f].path, &b)) die(', '    if (!slurp(s->v[f].path, &b) && 0) die(', PAGE, 'FAIL refuses the stylesheet unreadable'),
  ('css-no-comments', 'tools/cdpage.c', "      if (c == '/' && i + 1 < n && p[i + 1] == '*') { state = COMMENT;", '      if (0) { state = COMMENT;', PAGE, 'FAIL good.css is refused'),
  ('css-mark-beside-ok', 'tools/cdpage.c', '      if (!alone(l, n, CSS)) die(', '      if (0) die(', PAGE, 'FAIL refuses css_beside'),
  ('css-mark-twice-ok', 'tools/cdpage.c', '      if (++css_marks > 1) die(', '      if (++css_marks > 2) die(', PAGE, 'FAIL refuses css_twice'),
  ('css-mark-outside-ok', 'tools/cdpage.c', '      if (!in_style) die(', '      if (0) die(', PAGE, 'FAIL refuses css_outside'),
  ('css-mark-after-ok', 'tools/cdpage.c', '      if (marks) die("%s:%zu: /*CDOM_CSS*/ after', '      if (0) die("%s:%zu: /*CDOM_CSS*/ after', PAGE, 'FAIL refuses css_after'),
  ('css-no-mark-ok', 'tools/cdpage.c', '  if (sheets.n && !css_marks) {', '  if (0) {', PAGE, 'FAIL refuses css_none'),
  ('css-bom-kept', 'tools/cdpage.c', '    if (n >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) p += 3, n -= 3;\n', '', PAGE, 'FAIL the edges:'),
  ('css-no-last-newline', 'tools/cdpage.c', "    if (o->n > at && o->p[o->n - 1] != '\\n') bbyte(o, '\\n');\n", "    if (0 && o->n > at && o->p[o->n - 1] != '\\n') bbyte(o, '\\n');\n", PAGE, 'FAIL the edges:'),
  ('css-comments-kept', 'tools/cdpage.c', "        if (from != SIZE_MAX && (j == n || p[j] == '\\n')) {", "        if (0 && from != SIZE_MAX && (j == n || p[j] == '\\n')) {", PAGE, 'FAIL comments:'),
  ('css-inline-comments-dropped', 'tools/cdpage.c', '        from = blank ? line_at : SIZE_MAX;', '        from = blank >= 0 ? line_at : SIZE_MAX;', PAGE, 'FAIL comments:'),  # a comment beside a rule goes with its line, and the rule with it
  # cdom.mk and the stylesheets (tests/link/run.sh, the fixture apps of tests/link/apps, and tests/link/css.mjs in Chrome):
  # discovery through make, rebuilding on a stylesheet change only, the first build, an own page, the cascade
  ('mk-css-dir-dropped', 'cdom.mk', ')--css-dir src $(9)\n', ') $(9)\n', LINK, 'FAIL discovery: the css app'),
  ('mk-css-walk-app-dir', 'cdom.mk', ')--css-dir src $(9)\n', ')--css-dir . $(9)\n', LINK, 'FAIL an own page that links static/app.css does not build'),
  ('mk-csslist-not-prereq', 'cdom.mk', '$$(cdom_mk_noscript_tools)) $$(cdom_mk_csslist)', '$$(cdom_mk_noscript_tools))', LINK, 'FAIL rebuilding: a.css edited, and make -q says'),
  ('mk-css-files-prereq', 'cdom.mk', 'cdom_mk_csslist = $(if $(filter $@,$(APP_PAGE)),$(shell [ -x $(call cdom_mk_q,$(CDOM_PAGE_TOOL)) ] && [ -d $(call cdom_mk_q,$(@D)) ] \\\n  && $(call cdom_mk_q,$(CDOM_PAGE_TOOL)) --css-list src $(call cdom_mk_q,$@.csslist))$@.csslist)', 'cdom_mk_csslist = $(if $(filter $@,$(APP_PAGE)),$(wildcard src/*.css src/*/*.css src/*/*/*.css))', LINK, 'FAIL rebuilding: a/new.css'),
  ('mk-csslist-no-shell', 'cdom.mk', 'cdom_mk_csslist = $(if $(filter $@,$(APP_PAGE)),$(shell [ -x $(call cdom_mk_q,$(CDOM_PAGE_TOOL)) ] && [ -d $(call cdom_mk_q,$(@D)) ] \\\n  && $(call cdom_mk_q,$(CDOM_PAGE_TOOL)) --css-list src $(call cdom_mk_q,$@.csslist))$@.csslist)', 'cdom_mk_csslist = $(if $(filter $@,$(APP_PAGE)),$@.csslist)', LINK, 'FAIL rebuilding: a.css edited, and make -q says'),
  ('mk-csslist-not-in-recipe', 'cdom.mk', '\t$(CDOM_PAGE_TOOL) --css-list src $(call cdom_mk_q,$@.csslist)\n', '', LINK, 'a second make is not up to date'),
  ('mk-tool-intermediate', 'cdom.mk', '$$(cdom_mk_page_src) $$(CDOM_PAGE_TOOL) $$(APP_PAGE).csslist)', '$$(cdom_mk_page_src) $$(APP_PAGE).csslist)', LINK, 'FAIL the first build: make did not keep tools/cdpage'),
  ('mk-csslist-intermediate', 'cdom.mk', '$$(cdom_mk_page_src) $$(CDOM_PAGE_TOOL) $$(APP_PAGE).csslist)', '$$(cdom_mk_page_src) $$(CDOM_PAGE_TOOL))', LINK, 'a second make is not up to date'),  # make removes the list after the page, so every make writes the page again
  ('css-no-mark-silent', 'tools/cdpage.c', '  if (sheets.n && !css_marks) {', '  if (0) {', LINK, 'FAIL an own page with no /*CDOM_CSS*/ took'),
  ('css-sort-reversed-chrome', 'tools/cdpage.c', 'return strcmp(((const Sheet *)a)->rel, ((const Sheet *)b)->rel);', 'return strcmp(((const Sheet *)b)->rel, ((const Sheet *)a)->rel);', LINK, 'FAIL the last stylesheet in byte order of its path'),
  ('css-unescaped-chrome', 'tools/cdpage.c', '      if (p[i] == \'<\' && i + 1 < n && p[i + 1] == \'/\' && starts_ci(p + i + 2, n - i - 2, "style")) { bstr(o, "<\\\\/"); i++; continue; }\n', '', LINK, 'FAIL the </style in a string reads back exactly'),
  # The lessons' stylesheets (tutorial/run.sh): styled before the app starts, and no <style> built by C
  ('cdpage-no-css', 'tools/cdpage.c', '      bput(o, css.p, css.n);\n      continue;\n', '      continue;\n', TUT, 'FAIL 01-hello: styled before the app started'),
  ('lesson-builds-style', 'tutorial/01-hello/src/app.c', 'void cd_main(void) {\n', 'void cd_main(void) {\n  cd_open("style"); cd_text("body { margin: 0; }"); cd_close();\n', TUT, 'FAIL 01-hello: the app builds no <style>'),
  # The starter (make new, tools/new.sh), built and opened from disk (tests/link/run.sh, tools/starter.mjs)
  ('new-not-centered', 'tools/new.sh', '  justify-content: center;\n', '', LINK, 'FAIL "cdom" is centered'),
  ('new-separate', 'tools/new.sh', 'APP_PAGE := build/index.html\n', 'APP_PAGE := build/index.html\nAPP_WASM_SEPARATE := 1\n', LINK, "FAIL the starter's page does not start its bundled module"),
  ('new-css-outside-src', 'tools/new.sh', 'cat > "$dir/src/app.css" <<', 'cat > "$dir/app.css" <<', LINK, 'FAIL make new wrote other files than'),  # the stylesheet beside the Makefile is not bundled
  ('new-into-full-dir', 'tools/new.sh', 'if [ -e "$dir" ] && [ -n "$(ls -A "$dir")" ]; then', 'if false; then', LINK, 'FAIL make new wrote into a directory that is not empty'),
  ('new-relative-to-cdom', 'tools/new.sh', '    dir="$from/$dir" ;;', '    ;;', LINK, 'FAIL make new wrote other files than'),
  ('new-hint-off-center', 'tools/new.sh', '.hint {\n  position: fixed;\n  left: 0;\n  right: 0;\n', '.hint {\n  position: fixed;\n  width: 100%;\n', LINK, 'FAIL the hint "edit src/app.c" is on screen, centered'),
  ('new-cdom-fixed', 'tools/new.sh', 'CDOM ?= $cdom\n', 'CDOM := $cdom\n', LINK, 'FAIL the starter copied elsewhere ignored CDOM from the environment'),
  ('flags-hardcoded', 'tests/build.sh', 'W=(-Wall -Wextra -Werror)', 'W=(-Wall -Wextra -Werror -mbulk-memory)', LINK, 'FAIL tests/build.sh:15 hardcodes -mbulk-memory'),
  ('flags-hardcoded-link', 'tests/link/run.sh', 'C=(-O2 "${CDOM_CFLAGS[@]}"', 'C=(--target=wasm32 -O2 "${CDOM_CFLAGS[@]}"', LINK, 'FAIL tests/link/run.sh:'),
  ('mini-fill-strong', 'src/mini.c', '__attribute__((weak)) void *memset', 'void *memset', LINK, "FAIL an app's own libc"),
  ('region-kids-after', 'src/cdom.c', '  for (cd_region *k = r->kids, *next; k; k = next) {\n    next = k->next;\n    take(k, 0);\n  }\n  r->kids = 0;\n  if (r->views) views_gone(r);\n  while (r->nfns) {\n    cd_cleanup c = r->fns[--r->nfns];\n    releasing++;\n    c.fn(c.ctx);\n    releasing--;\n  }\n', '  if (r->views) views_gone(r);\n  while (r->nfns) {\n    cd_cleanup c = r->fns[--r->nfns];\n    releasing++;\n    c.fn(c.ctx);\n    releasing--;\n  }\n  for (cd_region *k = r->kids, *next; k; k = next) {\n    next = k->next;\n    take(k, 0);\n  }\n  r->kids = 0;\n', UNIT, 'FAIL nested regions: taking'),
  ('region-cleanups-oldest-first', 'src/cdom.c', '  while (r->nfns) {\n    cd_cleanup c = r->fns[--r->nfns];', '  for (uint32_t f = 0; f < r->nfns || (r->nfns = 0); f++) {\n    cd_cleanup c = r->fns[f];', UNIT, 'FAIL nested regions: taking'),
  ('region-no-cleanup', 'src/cdom.c', '    c.fn(c.ctx);\n', '    (void)c;\n', UNIT, 'FAIL nested regions: taking'),
  ('region-cleanups-after-free', 'src/cdom.c', '  while (r->nfns) {\n    cd_cleanup c = r->fns[--r->nfns];\n    releasing++;\n    c.fn(c.ctx);\n    releasing--;\n  }\n  uint32_t i = 0;\n  if (dom && r->parent) cd_clear(r->parent);\n  else if (dom) { cd_remove(r->root); i = 1; } /* the root is ids[0] */\n  for (; i < r->n; i++) cd_id_free(r->ids[i]);\n', '  uint32_t i = 0;\n  if (dom && r->parent) cd_clear(r->parent);\n  else if (dom) { cd_remove(r->root); i = 1; } /* the root is ids[0] */\n  for (; i < r->n; i++) cd_id_free(r->ids[i]);\n  while (r->nfns) {\n    cd_cleanup c = r->fns[--r->nfns];\n    releasing++;\n    c.fn(c.ctx);\n    releasing--;\n  }\n', UNIT, 'FAIL nested regions: taking'),
  ('region-kid-dom', 'src/cdom.c', '    take(k, 0);\n', '    take(k, 1);\n', UNIT, 'FAIL nested regions: taking'),
  ('region-no-unlink', 'src/cdom.c', '    region_unlink(r);\n    take(r, 1);', '    take(r, 1);', UNIT, 'FAIL nested regions: a child hidden alone', HANGS),  # shown again, the child is pushed on its parent's list a second time (r->next == r), and the walk of that list never ends
  ('region-up-ignored', 'src/cdom.c', '  if (r->up) {\n    if (!r->up->shown)', '  if (in) {\n    if (!r->up->shown)', UNIT, 'FAIL t_region_handoff'),
  ('region-forget-dom', 'src/cdom.c', '  region_unlink(r);\n  take(r, 0);\n}', '  region_unlink(r);\n  take(r, 1);\n}', UNIT, 'FAIL cd_region_forget'),
  ('region-cleanup-dom-ok', 'src/cdom.c', '  if (releasing) __builtin_trap(); /* a cleanup changed the DOM */\n', '', UNIT, 'FAIL traps: '),
  ('region-depth-unchecked', 'src/cdom.c', '  if (nbuilding == CD_REGION_DEPTH) __builtin_trap(); /* nested deeper than CD_REGION_DEPTH */\n', '', UNIT, 'FAIL traps: '),
  ('region-on-free-idle', 'src/cdom.c', '  if (!r || !r->shown || releasing) __builtin_trap();', '  if (!r) return;\n  if (!r->shown || releasing) __builtin_trap();', UNIT, 'FAIL traps: '),
  ('region-up-other-ok', 'src/cdom.c', '    if (r->up && r->up != in) __builtin_trap(); /* r lives in another region */\n', '', UNIT, 'FAIL traps: '),
  ('region-up-hidden-ok', 'src/cdom.c', '    if (!r->up->shown) __builtin_trap(); /* its parent is not shown */\n', '', UNIT, 'FAIL traps: '),
  ('list-destroy-after-free', 'src/cdom.c', '  f->destroy(f->ctx, it);\n  if (dom) cd_remove(it->node); else cd_id_free(it->node);', '  if (dom) cd_remove(it->node); else cd_id_free(it->node);\n  f->destroy(f->ctx, it);', UNIT, 'FAIL regions in list rows'),
  ('list-forget-destroy-after-free', 'src/cdom.c', '    f->destroy(f->ctx, &L->items[i]);\n    cd_id_free(L->items[i].node);', '    cd_id_free(L->items[i].node);\n    f->destroy(f->ctx, &L->items[i]);', UNIT, 'FAIL regions in list rows'),
  ('dirty-no-dedup', 'src/cdom.c', '  if (asked_find(fn, ctx)) return; /* pending */\n', '', UNIT, 'FAIL cd_dirty: each'),
  ('dirty-dedup-by-fn', 'src/cdom.c', '  pair_ent *e = pair_find(&asked, (uint32_t)(uintptr_t)ctx ^ f * 0x9e3779b1u, f);', '  pair_ent *e = pair_find(&asked, f * 0x9e3779b1u + 0 * (uint32_t)(uintptr_t)ctx, f);', UNIT, 'FAIL cd_dirty: 100 distinct'), # a key of fn alone: one function's many ctx are one ask
  ('dirty-not-flushed', 'src/cdom.c', 'void cd_flush(void) { renders_now(); walk_done(); }', 'void cd_flush(void) { walk_done(); }', UNIT, 'FAIL cd_dirty: each'),
  ('dirty-render-now-nests', 'src/cdom.c', '  if ((render_at == nrenders && !changed) || rendering || releasing) return;', '  if ((render_at == nrenders && !changed) || (rendering && 0) || releasing) return;', UNIT, 'FAIL cd_dirty: a render that asks'),
  ('undirty-not-in-round', 'src/cdom.c', '  if (e) renders[(uintptr_t)e->p].fn = 0;', '  if (e && (uintptr_t)e->p > render_at) renders[(uintptr_t)e->p].fn = 0;', UNIT, 'FAIL cd_undirty'),
  ('undirty-noop', 'src/cdom.c', '  pair_ent *e = asked_find(fn, ctx);\n  if (e) renders[(uintptr_t)e->p].fn = 0;', '  (void)fn; (void)ctx;', UNIT, 'FAIL cd_undirty'),
  ('render-now-noop', 'src/cdom.c', 'void cd_render_now(void) { renders_now(); }', 'void cd_render_now(void) {}', UNIT, 'FAIL cd_render_now'),
  ('render-rounds-unbounded', 'src/cdom.c', '#define CD_RENDER_ROUNDS 16', '#define CD_RENDER_ROUNDS 1000', UNIT, 'FAIL traps: renders that keep'),
  ('render-pass-everywhere', 'src/cdom.c', 'static void (*volatile render_pass)(void);', 'static void (*render_pass)(void);', LINK, 'FAIL an app that never calls cd_dirty carries the render pass'),
  # cd_dirty's table (cdom.c, renders: cd_dirty's asks)
  ('dirty-ran-counts-pending', 'src/cdom.c', '  return e && (uintptr_t)e->p >= render_at && renders[(uintptr_t)e->p].fn ? e : 0;', '  return e && renders[(uintptr_t)e->p].fn ? e : 0;', UNIT, 'FAIL cd_dirty: a render that asks'),
  ('dirty-dropped-counts-pending', 'src/cdom.c', '  return e && (uintptr_t)e->p >= render_at && renders[(uintptr_t)e->p].fn ? e : 0;', '  return e && (uintptr_t)e->p >= render_at ? e : 0;', UNIT, 'FAIL cd_dirty: 100 distinct'),
  ('dirty-table-kept', 'src/cdom.c', '  asked_reset();\n}', '  (void)asked_reset;\n}', UNIT, 'FAIL cd_undirty'), # last pass's asks look pending: a render asked again is dropped
  # shared state (cdom.c, shared state; cdom.h)
  ('state-runs-per-state', 'src/cdom.c', '      if (v->queued || v->ran > s->tick) continue;', '      if (v->ran > s->tick) continue;', UNIT, 'FAIL cd_state: a view of two states'),
  ('state-changed-relisted', 'src/cdom.c', '  if (s->changed) return; /* on the list already */\n', '', UNIT, 'FAIL cd_state: states changed in the order'), # a state listed twice cuts the one after it off the list
  ('state-ver-32', 'src/cdom.c', '  s->ver++;\n  s->tick', '  s->ver = (uint32_t)(s->ver + 1);\n  s->tick', UNIT, 'FAIL cd_state: the version is 64 bits'),
  ('state-no-tick-skip', 'src/cdom.c', '      if (v->queued || v->ran > s->tick) continue;', '      if (v->queued) continue;', UNIT, 'FAIL cd_state: a view that ran after'),
  ('state-skip-any-ran', 'src/cdom.c', '      if (v->queued || v->ran > s->tick) continue;', '      if (v->queued || v->ran) continue;', UNIT, 'FAIL cd_state: a view that ran after'),
  ('state-change-tick-zero', 'src/cdom.c', '  s->tick = rendering ? ++tick : 0;', '  s->tick = 0;', UNIT, 'FAIL cd_state: a view that ran after'),
  ('state-made-not-ran', 'src/cdom.c', '      v->ran = ++tick;\n      pv_push(v);', '      pv_push(v);', UNIT, 'FAIL cd_state: a view shows a region'),
  ('state-made-not-reset', 'src/cdom.c', '      v->ran = ++tick;\n      pv_push(v);', '      v->ran = ++tick;', UNIT, 'FAIL cd_state: two views of one state'),
  ('state-ran-not-reset', 'src/cdom.c', '    for (uint32_t i = 0; i < npv; i++) pv[i]->ran = 0;\n', '', UNIT, 'FAIL cd_state: a view shows a region'),
  ('state-pass-not-ended', 'src/cdom.c', '  if (views_pass) views_pass(1);\n', '', UNIT, 'FAIL cd_state: a view shows a region'),
  ('state-no-order', 'src/cdom.c', '  if (in_order) return;\n', '  (void)in_order;\n  return;\n', UNIT, 'FAIL cd_state: outermost region first'),
  ('state-depth-flat', 'src/cdom.c', '  r->depth = d + 1;', '  r->depth = 1;', UNIT, 'FAIL cd_state: outermost region first'),
  ('state-depth-old-rule', 'src/cdom.c', '    if (r->depth > 0xffff) __builtin_trap();\n    v = cd_alloc(sizeof *v);\n    *v = (cd_view){fn, ctx, r->views, 0, 0, (uint16_t)r->depth, 0};', '    uint32_t d = 1;\n    for (cd_region *u = r->up; u; u = u->up) d++;\n    if (d > 0xffff) __builtin_trap();\n    v = cd_alloc(sizeof *v);\n    *v = (cd_view){fn, ctx, r->views, 0, 0, (uint16_t)d, 0};', UNIT, 'FAIL cd_state: a region opened in a keyed-list row'), # the depth was the r->up chain: a row's region was depth 1
  ('state-depth-build-ignored', 'src/cdom.c', '  if (r) open_depth = r->depth;\n', '', UNIT, 'FAIL cd_state: a region opened in a keyed-list row'),
  ('state-depth-build-not-restored', 'src/cdom.c', '  open_depth = open_was[nbuilding];\n', '', UNIT, 'FAIL cd_state: a region opened in a keyed-list row'),
  ('state-depth-view-ignored', 'src/cdom.c', '    open_depth = v->depth; /* a region it shows, or a list it updates, is under its region */\n', '', UNIT, 'FAIL cd_state: a region opened in a keyed-list row'),
  ('state-depth-view-not-restored', 'src/cdom.c', '    open_depth = depth_was;\n', '    (void)depth_was;\n', UNIT, 'FAIL cd_state: a region opened in a keyed-list row'),
  ('state-depth-up-ignored', 'src/cdom.c', '  if (r->up && r->up->depth > d) d = r->up->depth;\n', '', UNIT, 'FAIL cd_state: a region opened in a keyed-list row'),
  ('state-order-unstable', 'src/cdom.c', '  for (uint32_t i = a; i < b; i++) pv_tmp[depth_n[pv[i]->depth]++] = pv[i];', '  for (uint32_t i = b; i-- > a;) pv_tmp[depth_n[pv[i]->depth]++] = pv[i];', UNIT, 'FAIL cd_state: views found at depths'),
  ('state-views-after-renders', 'src/cdom.c', '    if (changed) views_pass(0);\n    renders_round();', '    renders_round();\n    if (changed) views_pass(0);', UNIT, 'FAIL cd_state: a view that changes a state and asks'),
  ('state-dead-runs', 'src/cdom.c', '    if (!v->fn) continue; /* its region went, earlier in this round */\n', '', UNIT, 'FAIL cd_state: two views of one state'),
  ('state-dead-freed-in-pass', 'src/cdom.c', '    if (rendering) { /* pv may hold it: the pass\'s end frees it */', '    if (0) { /* pv may hold it: the pass\'s end frees it */', UNIT, 'FAIL cd_state: two views of one state'),
  ('state-dead-kept', 'src/cdom.c', '      dead = v->next;\n      cd_free(v);', '      dead = v->next;', UNIT, 'FAIL cd_state: 3,050 rounds'),
  ('state-links-kept', 'src/cdom.c', '      if (l->next) l->next->prev = l->prev; else s->last = l->prev;\n      cd_free(l);', '      if (l->next) l->next->prev = l->prev; else s->last = l->prev;', UNIT, 'FAIL cd_state: 3,050 rounds'),
  ('state-not-dropped', 'src/cdom.c', '  if (r->views) views_gone(r);\n', '', UNIT, 'FAIL cd_state: a state changed, then its view'),
  ('state-dropped-after-cleanups', 'src/cdom.c', '  if (r->views) views_gone(r);\n  while (r->nfns) {\n    cd_cleanup c = r->fns[--r->nfns];\n    releasing++;\n    c.fn(c.ctx);\n    releasing--;\n  }\n', '  while (r->nfns) {\n    cd_cleanup c = r->fns[--r->nfns];\n    releasing++;\n    c.fn(c.ctx);\n    releasing--;\n  }\n  if (r->views) views_gone(r);\n', UNIT, 'FAIL cd_state: a region\'s views go before its cleanups'),
  ('state-forget-keeps-views', 'src/cdom.c', '  if (r->views) views_gone(r);\n', '  if (r->views && dom) views_gone(r);\n', UNIT, 'FAIL cd_state: views in regions opened in keyed-list rows'),
  ('state-same-view-not-found', 'src/cdom.c', '  while (v && (v->fn != fn || v->ctx != ctx)) v = v->next;', '  v = 0;', UNIT, 'FAIL cd_state: one (fn, ctx) watched'),
  ('state-watch-twice', 'src/cdom.c', '      if (l->s == s) return; /* it watches s already */', '      if (l->s == s && 0) return; /* it watches s already */', UNIT, 'FAIL cd_state: a view that watches a state it watches already'),
  ('state-watch-no-region-ok', 'src/cdom.c', '  if (!r || releasing) __builtin_trap();', '  if (!r) return;\n  if (releasing) __builtin_trap();', UNIT, 'FAIL traps: cd_watch with no region'),
  ('state-watch-in-cleanup-ok', 'src/cdom.c', '  if (!r || releasing) __builtin_trap();', '  if (!r) __builtin_trap();', UNIT, 'FAIL traps: cd_watch in a cleanup'),
  ('state-changed-no-pass', 'src/cdom.c', '  changed_last = s;\n  render_pass = run_renders;', '  changed_last = s;', UNIT, 'FAIL traps: views that keep changing'), # the trap page asks for no render: only cd_changed starts the pass
  ('state-changed-no-views', 'src/cdom.c', '  render_pass = run_renders;\n  views_pass = views_run;\n}', '  render_pass = run_renders;\n}', UNIT, 'FAIL cd_state on a fresh page: a state that no view ever watched'),
  ('state-watch-no-views', 'src/cdom.c', '  views_pass = views_run; /* a view made in a pass is in pv, which the pass\'s end empties */\n', '', UNIT, 'FAIL cd_state on a fresh page: a view made by a render'),
  ('state-pass-ignores-changed', 'src/cdom.c', '  if ((render_at == nrenders && !changed) || rendering || releasing) return;', '  if (render_at == nrenders || rendering || releasing) return;', UNIT, 'FAIL cd_state: a read (cd_get_num)'),
  ('state-rounds-unbounded', 'src/cdom.c', '#define CD_RENDER_ROUNDS 16', '#define CD_RENDER_ROUNDS 1000', UNIT, 'FAIL traps: views that keep changing'),
  ('state-pass-everywhere', 'src/cdom.c', 'static void (*volatile views_pass)(int end);', 'static void (*views_pass)(int end);', LINK, 'FAIL an app that never uses cd_state carries'),
  ('state-drop-everywhere', 'src/cdom.c', '  if (r->views) views_gone(r);', '  if (r->views) views_drop(r);', LINK, 'FAIL an app that never uses cd_state carries'),
  ('state-read-traps-plain', 'src/cdom.h', 'static inline void cd_read(const cd_state *) {}', 'static inline void cd_read(const cd_state *) { __builtin_trap(); }', UNIT, 'FAIL without CD_STATE_CHECK cd_read is nothing'),
  ('state-check-view-unchecked', 'src/cdom.c', '  } else if (running && !view_watches(running, s)) {', '  } else if (0 && running && !view_watches(running, s)) {', UNIT, 'FAIL traps: the check build: a view in the pass'),
  ('state-check-build-unchecked', 'src/cdom.c', '    if (!ok) __builtin_trap(); /* a build read a state that nothing around it watches */', '    (void)ok;', UNIT, 'FAIL traps: the check build: a build that read'),
  ('state-check-under-ignored', 'src/cdom.c', '    for (uint32_t j = 0; j < level && !ok; j++) ok = building[j] && region_watches(building[j], s);\n', '', UNIT, 'FAIL the check build: a build that reads what'),
  ('state-check-up-ignored', 'src/cdom.c', '    for (const cd_region *u = r; u && !ok; u = u->up) ok = region_watches(u, s);', '    ok = region_watches(r, s);', UNIT, 'FAIL the check build: a build that reads what'),
  ('state-check-build-read-is-view', 'src/cdom.c', '  if (lv > running_base) {\n    uint32_t i = lv - 1;', '  if (!running && lv > running_base) {\n    uint32_t i = lv - 1;', UNIT, 'FAIL the check build: a build that reads what'),
  ('state-check-reads-unchecked-early', 'src/cdom.c', '  reads_check(nbuilding - 1);\n', '  (void)reads_check;\n', UNIT, 'FAIL traps: the check build: a build that read'),
  # cd_bind_prop and cd_bound_shows_bytes
  ('bound-shows-bytes-noop', 'src/cdom.c', '  if (!b->id || b->kind == BOUND_BOOL) __builtin_trap(); /* unbound, or a bool */\n  bound_keep(b, p, len);', '  if (!b->id || b->kind == BOUND_BOOL) __builtin_trap(); /* unbound, or a bool */\n  (void)p; (void)len;', UNIT, 'FAIL cd_bind_prop and cd_bound_shows_bytes'),
  ('bound-prop-as-attr', 'src/cdom.c', 'static void write_prop(cd_id id, cd_str name, cd_str v) { cd_set_str(id, name, v); }', 'static void write_prop(cd_id id, cd_str name, cd_str v) { cd_set_attr(id, name, v); }', UNIT, 'FAIL cd_bind_prop and cd_bound_shows_bytes'),
  ('sync-get', 'src/cdom.c', 'static double get(cd_id id, cd_str path, uint32_t kind, void *dst, uint32_t cap) {\n  renders_now();\n', 'static double get(cd_id id, cd_str path, uint32_t kind, void *dst, uint32_t cap) {\n', UNIT, 'FAIL the reads, the layout reads'),
  ('sync-set', 'src/cdom.c', '  if (v.tag == CD_V_OBJ) __builtin_trap(); /* its pairs cannot follow a value passed alone */\n  renders_now();\n', '  if (v.tag == CD_V_OBJ) __builtin_trap(); /* its pairs cannot follow a value passed alone */\n', UNIT, 'FAIL the reads, the layout reads'),
  ('sync-call', 'src/cdom.c', 'void cd_call_args(cd_id id, cd_str method, uint32_t n, const cd_val *a) {\n  renders_now();\n', 'void cd_call_args(cd_id id, cd_str method, uint32_t n, const cd_val *a) {\n', UNIT, 'FAIL the reads, the layout reads'),
  ('sync-hit', 'src/cdom.c', 'cd_id cd_hit(int32_t x, int32_t y) { renders_now(); return', 'cd_id cd_hit(int32_t x, int32_t y) { return', UNIT, 'FAIL the reads, the layout reads'),
  ('sync-viewport', 'src/cdom.c', 'void cd_viewport(cd_viewport_rec *v) { renders_now(); j_viewport', 'void cd_viewport(cd_viewport_rec *v) { j_viewport', UNIT, 'FAIL the reads, the layout reads'),
  ('sync-rect', 'src/cdom.c', 'int cd_rect(cd_id id, double out[4]) { renders_now(); return', 'int cd_rect(cd_id id, double out[4]) { return', UNIT, 'FAIL the reads, the layout reads'),
  ('sync-in-cleanup', 'src/cdom.c', '  if ((render_at == nrenders && !changed) || rendering || releasing) return;', '  if ((render_at == nrenders && !changed) || rendering || (releasing && 0)) return;', UNIT, 'FAIL a cleanup that reads'),
  ('bound-no-skip-id', 'src/cdom.h', '  if (b->seen != 1 || b->last != value.v) cd_bound_set_(b, value);', '  cd_bound_set_(b, value);', UNIT, 'FAIL cd_bound: a set writes'),
  ('bound-bytes-no-skip', 'src/cdom.c', '  if (b->seen == SEEN_BYTES && b->blen == len', '  if (0 && b->seen == SEEN_BYTES && b->blen == len', UNIT, 'FAIL cd_bound: a set writes'),
  ('bound-bytes-made-anyway', 'src/cdom.c', '  if (!b->id || b->kind == BOUND_BOOL) __builtin_trap();\n  if (b->seen == SEEN_BYTES', '  if (!b->id || b->kind == BOUND_BOOL) __builtin_trap();\n  cd_tmp(p, len);\n  if (b->seen == SEEN_BYTES', UNIT, 'FAIL cd_bound: unchanged bytes'),
  ('bound-num-no-skip', 'src/cdom.c', '  if (b->seen == SEEN_NUM && b->last == n) return;\n', '', UNIT, 'FAIL cd_bound: a set writes'),
  ('bound-bool-not-truth', 'src/cdom.c', '  if (b->kind == BOUND_BOOL) n = n != 0; /* 1 and 5 are the same true */\n', '', UNIT, 'FAIL cd_bound: a set writes'),
  ('bound-no-auto-unbind', 'src/cdom.c', '  if (recording) cd_on_free(0, unbind_cleanup, b);', '  if (0 && recording) cd_on_free(0, unbind_cleanup, b);', UNIT, 'FAIL cd_bound: values bound while'),
  ('bound-unbound-ok', 'src/cdom.c', '  if (!b->id) __builtin_trap(); /* unbound */', '', UNIT, 'FAIL traps: a set on an unbound'),
  ('bound-tmp-accepted', 'src/cdom.c', '  if (s.v >> 31) __builtin_trap(); /* a runtime string', '  if (0) __builtin_trap(); /* a runtime string', UNIT, 'FAIL traps: cd_bound_set given a runtime string'),
  ('req-no-gen-check', 'src/cdom.c', '  if (r->state != CD_REQ_LOADING || (v >> 16) != r->gen) return 0;', '  if (r->state != CD_REQ_LOADING) return 0;', UNIT, 'FAIL cd_req: the latest'),
  ('req-no-state-check', 'src/cdom.c', '  if (r->state != CD_REQ_LOADING || (v >> 16) != r->gen) return 0;', '  if ((v >> 16) != r->gen) return 0;', UNIT, 'FAIL cd_req: the latest'),
  ('req-index-lost', 'src/cdom.c', '  if (index) *index = (uint16_t)v;', '  if (index) *index = 0;', UNIT, 'FAIL cd_req: the latest'),
  ('req-begin-no-bump', 'src/cdom.c', 'void *cd_req_begin(cd_req *r, uint16_t index) {\n  r->gen++;\n', 'void *cd_req_begin(cd_req *r, uint16_t index) {\n', UNIT, 'FAIL cd_req: the latest'),
  ('req-drop-noop', 'src/cdom.c', 'void cd_req_drop(cd_req *r) { r->state = CD_REQ_IDLE; }', 'void cd_req_drop(cd_req *r) { (void)r; }', UNIT, 'FAIL cd_req: the latest'),
  ('bound-shows-noop', 'src/cdom.c', '  b->seen = SEEN_ID;\n  b->last = s.v;\n}\nvoid cd_bound_shows_num', '  (void)s;\n}\nvoid cd_bound_shows_num', UNIT, 'FAIL cd_bound_shows'),
  ('tslot-no-stop', 'src/cdom.c', '  cd_timer_clear(t);\n  t->fn = fn;', '  t->fn = fn;', UNIT, 'FAIL cd_timer_set'),
  ('tslot-cleared-after', 'src/cdom.c', '  t->token = 0;\n  t->fn(t->ctx);', '  t->fn(t->ctx);\n  t->token = 0;', UNIT, 'FAIL cd_timer_set'),
  ('tslot-clear-noop', 'src/cdom.c', '  if (t->token) cd_timer_stop(t->token);\n', '', UNIT, 'FAIL cd_timer_set'),
  ('input-whole-fits', 'src/cdom.c', '  return n <= cap;\n}', '  return n < cap;\n}', UNIT, 'FAIL cd_input_whole: the whole value'),
  ('input-whole-len', 'src/cdom.c', '  *len = n;\n  return n <= cap;', '  *len = n <= cap ? n : 0;\n  return n <= cap;', UNIT, 'FAIL cd_input_whole: the whole value'),
  ('ui-el-no-class', 'src/cdom_ui.c', '  cd_open(tag);\n  if (cls.v) cd_class(cls);', '  cd_open(tag);\n  (void)cls;', UNIT, 'FAIL cd_el,'),
  ('ui-button-type', 'src/cdom_ui.c', '  cd_attr("type", "button");\n', '', UNIT, 'FAIL cd_button'),
  ('ui-button-ctx', 'src/cdom_ui.c', '  cd_on(b, CD_EV_CLICK, fn, ctx);', '  cd_on(b, CD_EV_CLICK, fn, 0);\n  (void)ctx;', UNIT, 'FAIL cd_button'),
  ('ui-button-label', 'src/cdom_ui.c', '  if (label_text) cd_name_text(*label_text = cd_id_new());\n', '  (void)label_text;\n', UNIT, 'FAIL cd_button'),
  ('ui-buf-partial', 'src/cdom_ui.c', '  if (b->over || n > b->cap - b->len) { b->over = 1; return; }', '  if (b->over) return;\n  if (n > b->cap - b->len) { b->over = 1; n = b->cap - b->len; }', UNIT, 'FAIL cd_buf'),
  ('ui-buf-not-sticky', 'src/cdom_ui.c', '  if (b->over || n > b->cap - b->len)', '  if (n > b->cap - b->len)', UNIT, 'FAIL cd_buf'),
  ('ui-i64-sign', 'src/cdom_ui.c', "  if (v < 0) t[--k] = '-';\n", '', UNIT, 'FAIL cd_buf'),
  ('ui-linked-always', 'src/cdom_ui.c', 'void (cd_el)(cd_str tag, cd_str cls) {', '__attribute__((export_name("cd_el"))) void (cd_el)(cd_str tag, cd_str cls) {', LINK, 'FAIL cdom_ui linked and not called'),
  ('callbacks-only-http', 'src/cdom_callbacks_only.h', '#pragma GCC poison cd_http cd_on_http', '#pragma GCC poison cd_on_http', LINK, 'FAIL the callbacks-only profile let a poisoned call through (USE_HTTP)'),
  ('regbuf-all-kept', 'src/cdom.c', '  cd_free(r->ids);\n  cd_free(r->fns);\n  r->ids = 0;\n  r->fns = 0;\n  r->n = r->cap = r->nfns = r->fcap = 0;', '  r->n = 0;', UNIT, 'FAIL a region taken away frees'),
  ('regbuf-fns-kept', 'src/cdom.c', '  cd_free(r->ids);\n  cd_free(r->fns);\n  r->ids = 0;\n  r->fns = 0;\n  r->n = r->cap = r->nfns = r->fcap = 0;', '  cd_free(r->ids);\n  r->ids = 0;\n  r->n = r->cap = 0;', UNIT, 'FAIL a region taken away frees'),
  ('cleanup-stop-traps', 'src/cdom.c', 'void cd_timer_stop(uint32_t token) {\n', 'void cd_timer_stop(uint32_t token) {\n  if (releasing) __builtin_trap();\n', UNIT, 'FAIL t_region_handoff'),  # the handoff case stops B's timer from a cleanup first; the stop case after it pins the header's words
  # picked files (src/cdom_file.c, tools/file.mjs; the old tier in tools/legacy.mjs) and the encoder's quality
  ('file-no-unsupported', 'shim/cdom.js', '    const answer = (status, ptr, length) => exports.cd_file_got(token, status, ptr, length);\n    if (typeof FileReader === "undefined") {', '    const answer = (status, ptr, length) => exports.cd_file_got(token, status, ptr, length);\n    if (false) {', LEG, 'FAIL IE 9'),
  ('file-nofile-sync', 'shim/cdom.js', '      setTimeout(() => answer(1, 0, 0));', '      answer(1, 0, 0);', FILE, 'FAIL every answer comes from the event loop'),
  ('file-toobig-sync', 'shim/cdom.js', '      setTimeout(() => answer(2, 0, Math.min(file.size, 4294967295)));', '      answer(2, 0, Math.min(file.size, 4294967295));', FILE, 'FAIL every answer comes from the event loop'),
  ('file-toobig-read', 'shim/cdom.js', '      setTimeout(() => answer(2, 0, Math.min(file.size, 4294967295)));\n      return;', '      const peek = new FileReader();\n      peek.onload = () => answer(2, 0, file.size);\n      peek.readAsArrayBuffer(file);\n      return;', FILE, 'FAIL a file over max_bytes is never read'),
  ('file-toobig-no-size', 'shim/cdom.js', 'answer(2, 0, Math.min(file.size, 4294967295))', 'answer(2, 0, 0)', FILE, 'FAIL TOO_BIG'),
  ('file-max-exclusive', 'shim/cdom.js', '    if (file.size > maxBytes >>> 0) {', '    if (file.size >= maxBytes >>> 0) {', FILE, 'FAIL OK: the file'),
  ('file-error-status', 'shim/cdom.js', '    reader.onerror = () => answer(3, 0, 0);', '    reader.onerror = () => answer(1, 0, 0);', FILE, 'FAIL UNREADABLE: a picked file'),
  ('file-index-ignored', 'shim/cdom.js', '    return (files && files[index >>> 0]) || null;', '    return (files && files[0]) || null;', FILE, 'FAIL NO_FILE: nothing at the index'),
  ('file-no-room-unchecked', 'shim/cdom.js', '    if (!ptr) {\n      answer(5, 0, data.length);', '    if (false) {\n      answer(5, 0, data.length);', FILE, 'FAIL NO_ROOM'),
  ('file-no-room-no-size', 'shim/cdom.js', '      answer(5, 0, data.length);', '      answer(5, 0, 0);', FILE, 'FAIL NO_ROOM'),
  ('file-bytes-kept', 'src/cdom_file.c', '  else if (cd_cb_take(token, &fn, &ctx)) ((cd_file_fn)fn)(ctx, status, p, len);\n  cd_flush();\n  cd_free((void *)p);', '  else if (cd_cb_take(token, &fn, &ctx)) ((cd_file_fn)fn)(ctx, status, p, len);\n  cd_flush();', FILE, 'FAIL nothing holds a file'),
  ('file-room-traps', 'src/cdom_file.c', 'return cd_alloc_try(n ? n : 1); }', 'return cd_alloc(n ? n : 1); }', FILE, 'FAIL'),
  ('file-no-flush', 'src/cdom_file.c', '((cd_file_fn)fn)(ctx, status, p, len);\n  cd_flush();', '((cd_file_fn)fn)(ctx, status, p, len);', FILE, 'FAIL every answer\'s export flushes'),
  ('file-token-hook', 'src/cdom_file.c', '  if (token < CD_TOKEN_CDOM) cd_on_file(token, status, p, len);', '  if (0) cd_on_file(token, status, p, len);', FILE, 'FAIL cd_file_read with a token'),
  ('file-token-unchecked', 'src/cdom_file.c', '  if (token >= CD_TOKEN_CDOM) __builtin_trap();\n  j_file_read', '  j_file_read', FILE, 'FAIL traps'),
  ('encode-quality-ignored', 'shim/cdom.js', 'str(mime), quality / 100);', 'str(mime), 0.92);', UNIT, "FAIL cd_encode_image's quality"),
  # the program's libc for libraries (src/libc/stdlib.h, stdio.h's FILE) and cdom.mk's module parts (tests/link/parts)
  ('stdlib-abs', 'src/libc/stdlib.h', 'static inline int abs(int v) { return v < 0 ? -v : v; }', 'static inline int abs(int v) { return v; }', LINK, 'FAIL stdlib.h (cdom flags): abs, labs'),
  ('stdlib-labs', 'src/libc/stdlib.h', 'static inline long labs(long v) { return v < 0 ? -v : v; }', 'static inline long labs(long v) { return v < 0 ? v : v; }', LINK, 'FAIL stdlib.h (cdom flags): abs, labs'),
  ('stdio-file-complete', 'src/libc/stdio.h', 'typedef struct cd_no_file FILE;', 'typedef struct cd_no_file { int fd; } FILE;', LINK, 'FAIL stdio.h (cdom flags): a FILE could be declared'),
  ('stdio-no-file', 'src/libc/stdio.h', 'typedef struct cd_no_file FILE;', '', LINK, 'FAIL stdlib.h (cdom flags): a file that uses'),
  ('part-flags-dropped', 'cdom.mk', '$(APP_CFLAGS) $(call cdom_mk_obj_flags,$@) -c', '$(APP_CFLAGS) -c', LINK, 'FAIL cdom.mk (first): a module with a part does not build'),
  ('part-flags-leak', 'cdom.mk', '$(cdom_mk_mod_ldflags) $(APP_LDFLAGS) -o $@.linked', '$(cdom_mk_mod_ldflags) $(foreach o,$(call cdom_mk_objs,$@),$(call cdom_mk_obj_flags,$(o))) $(APP_LDFLAGS) -o $@.linked', LINK, 'FAIL cdom.mk (first): an app file sees'),
  ('module-one-file', 'cdom.mk', '$(foreach m,$(APP_MODULES),$(CDOM_$(call cdom_mk_up,$(m))_SRC))', '$(foreach m,$(APP_MODULES),$(firstword $(CDOM_$(call cdom_mk_up,$(m))_SRC)))', LINK, 'FAIL cdom.mk (first): the module'),
  ('module-ldflags-dropped', 'cdom.mk', '$(CDOM_LDFLAGS) $(cdom_mk_mod_ldflags) $(APP_LDFLAGS)', '$(CDOM_LDFLAGS) $(APP_LDFLAGS)', LINK, 'FAIL cdom.mk (first): the module'),
  ('part-dup-unchecked', 'cdom.mk', 'not a cdom module ($(CDOM_MODULES))))$(cdom_mk_unique)', 'not a cdom module ($(CDOM_MODULES))))', LINK, 'doubled part'),
  ('part-objs-intermediate', 'cdom.mk', '$$(APP_WASM) $$(cdom_mk_app_src) $$(call cdom_mk_objs,$$(APP_WASM)) $$(CDOM_LITS)', '$$(APP_WASM) $$(cdom_mk_app_src) $$(CDOM_LITS)', LINK, 'FAIL cdom.mk (first): make deleted'),
  # the image module (tests/image/run.sh, its wasm runs, its unit cases and its link case)
  ('img-green-as-red', 'src/cdom_image_loops.c', 'q[0] += wgt * px[0]; q[1] += wgt * px[1];', 'q[0] += wgt * px[0]; q[1] += wgt * px[0];', IMG, 'differs from the exact average'),
  ('img-truncates', 'src/cdom_image.c', 'uint64_t v = (a[k] * 2 + t) / t2;', 'uint64_t v = (a[k] * 2 + 0 * t) / t2;', IMG, 'differs from the exact average'),
  ('img-halves-down', 'src/cdom_image.c', 'uint64_t v = (a[k] * 2 + t) / t2;', 'uint64_t v = (a[k] * 2 + t - 1) / t2;', IMG, 'scaler-2x2-1x1'),
  ('img-down-weight', 'src/cdom_image.c', 'uint64_t wgt = b - s->ypos;', 'uint64_t wgt = b - s->ypos + 1;', IMG, 'scaler-'),
  ('img-o6-mirrored', 'src/cdom_image.c', 'case 6: at = (ptrdiff_t)(ow - 1 - y) * 4;', 'case 6: at = (ptrdiff_t)y * 4;', IMG, 'o6'),
  ('img-o8-row', 'src/cdom_image.c', 'case 8: at = ((ptrdiff_t)(oh - 1) * ow + y) * 4;', 'case 8: at = ((ptrdiff_t)(oh - 1) * ow + (ow - 1 - y)) * 4;', IMG, 'o8'),
  ('img-rows-past-end', 'src/cdom_image.c', '  if (s->row >= s->sh) return;\n', '', IMG, 'past the end'),
  ('img-take-early', 'src/cdom_image.c', 'uint8_t *o = s->oy == s->dh ? s->out : 0;', 'uint8_t *o = s->out;', IMG, 'before the last source row'),
  ('img-short-side-floor', 'src/cdom_image.c', 'uint64_t v = ((uint64_t)h * bw * 2 + w) / ((uint64_t)w * 2);', 'uint64_t v = ((uint64_t)h * bw) / w;', IMG, 'rounds half up'),
  ('img-enlarges', 'src/cdom_image.c', '  if (w <= bw && h <= bh) { *dw = w; *dh = h; return; }\n', '', IMG, 'never enlarged'),
  ('img-box-not-shown', 'src/cdom_image.c', 'if (o >= 5 && o <= 8) fit_box(sw, sh, bh, bw, dw, dh);', 'if (o > 8) fit_box(sw, sh, bh, bw, dw, dh);', IMG, 'in a 900x300 box'),
  ('img-scaler-4k-more', 'src/cdom_image.c', 's->hrow = cd_alloc_try(dw * 3 * (uint32_t)sizeof(uint32_t));', 's->hrow = cd_alloc_try(dw * 3 * (uint32_t)sizeof(uint32_t) + 4096);', IMG, 'the scaler holds'),
  ('img-acc-kept', 'src/cdom_image.c', 'cd_free(s->out); cd_free(s->acc); cd_free(s->hrow);', 'cd_free(s->out); cd_free(s->hrow);', IMG, 'leak'),
  ('img-box-zero', 'src/cdom_image.c', '{ return !w || !h || w > CD_IMAGE_BOX_MAX', '{ return w > CD_IMAGE_BOX_MAX', IMG, 'a box of 0'),
  ('img-box-cap', 'src/cdom_image.c', 'return !w || !h || w > CD_IMAGE_BOX_MAX || h > CD_IMAGE_BOX_MAX; }', 'return !w || !h; }', IMG, 'CD_IMAGE_BOX_MAX'),
  ('exif-ifd-unchecked', 'src/cdom_image.c', '  if (ifd + 2 > room) return 1;\n', '', IMG, 'AddressSanitizer'),
  ('exif-9', 'src/cdom_image.c', 'return o >= 1 && o <= 8 ? o : 1;', 'return o >= 1 && o <= 9 ? o : 1;', IMG, 'orientation 9'),
  ('exif-any-type', 'src/cdom_image.c', 'if (rd16(e, le) == 0x0112 && rd16(e + 2, le) == 3) {', 'if (rd16(e, le) == 0x0112) {', IMG, 'a LONG'),
  ('exif-any-tag', 'src/cdom_image.c', 'if (rd16(e, le) == 0x0112 && rd16(e + 2, le) == 3) {', 'if (rd16(e + 2, le) == 3) {', IMG, 'another tag'),
  ('exif-big-endian', 'src/cdom_image.c', "int le = t[0] == 'I';", 'int le = 1;', IMG, 'big-endian'),
  ('exif-xmp-first', 'src/cdom_image_jpeg.c', '      if (memcmp(w->og + 4, "Exif\\0\\0", 6)) { w->oat += 2 + (uint64_t)w->oseg; w->os = O_HEAD; continue; }\n', '', IMG, 'jpeg-exif-after-xmp'),
  ('exif-count-unbounded', 'src/cdom_image.c', 'i < count && ifd + 2 + 12 * (uint64_t)(i + 1) <= room;', 'i < count;', IMG, 'AddressSanitizer'),
  ('jpeg-full-size', 'src/cdom_image_jpeg.c', 'if (cw >= dw && ch >= dh) return n;', 'if (cw >= dw && ch >= dh) return 8;', IMG, 'jpeg-'),
  ('jpeg-eighth', 'src/cdom_image_jpeg.c', 'if (cw >= dw && ch >= dh) return n;', 'if (cw >= dw && ch >= dh) return 1;', IMG, 'jpeg-'),
  ('jpeg-cut-ignored', 'src/cdom_image_jpeg.c', 'x->status = x->ran_dry ? CD_IMAGE_CORRUPT : CD_IMAGE_OK;', 'x->status = CD_IMAGE_OK;', IMG, 'cut in half'),
  ('jpeg-not-destroyed', 'src/cdom_image_jpeg.c', '  jpeg_destroy_decompress(&x->cinfo);\n  cd_free(x->row);', '  cd_free(x->row);', IMG, 'left'),
  ('jpeg-destroy-after-create', 'src/cdom_image_jpeg.c', '  jpeg_destroy_decompress(&x->cinfo);\n  cd_free(x->row);', '  if (x->cinfo.src) jpeg_destroy_decompress(&x->cinfo);\n  cd_free(x->row);', IMG, 'review item 4'),
  ('jpeg-scaler-kept', 'src/cdom_image_jpeg.c', '  cd_free(x->row);\n  cd_image_scaler_free(&x->sc);\n  cd_free(x);', '  cd_free(x->row);\n  cd_free(x);', IMG, 'left'),
  ('jpeg-pixels-off-by-one', 'src/cdom_image_jpeg.c', '(uint64_t)w * h > CD_IMAGE_PIXELS_MAX) return CD_IMAGE_TOO_BIG;', '(uint64_t)w * h >= CD_IMAGE_PIXELS_MAX) return CD_IMAGE_TOO_BIG;', IMG, '8000x8000 is allowed'),
  ('jpeg-side-unchecked', 'src/cdom_image_jpeg.c', 'if (w > CD_IMAGE_SIDE_MAX || h > CD_IMAGE_SIDE_MAX || (uint64_t)w * h', 'if ((uint64_t)w * h', IMG, '65501'),
  ('jpeg-12-bit', 'src/cdom_image_jpeg.c', '      if (f[0] != 8) return CD_IMAGE_UNSUPPORTED;', '', IMG, '12-bit'),
  ('jpeg-arithmetic', 'src/cdom_image_jpeg.c', '      if (m != 0xc0 && m != 0xc1 && m != 0xc2) return CD_IMAGE_UNSUPPORTED;', '', IMG, 'arithmetic'),
  ('jpeg-cmyk', 'src/cdom_image_jpeg.c', '      if (ncomp != 1 && ncomp != 3) return CD_IMAGE_UNSUPPORTED;', '', IMG, 'jpeg-cmyk'),
  ('jpeg-plain-upsampling', 'src/cdom_image_jpeg.c', 'c->do_fancy_upsampling = TRUE;', 'c->do_fancy_upsampling = FALSE;', IMG, 'jpeg-'),
  ('jpeg-error-returns', 'src/cdom_image_jpeg.c', 'static void err_exit(j_common_ptr c) { cd_image_throw(((jctx *)c->client_data)->guard); }', 'static void err_exit(j_common_ptr c) { (void)c; }', IMG, 'UndefinedBehaviorSanitizer'),
  ('jpeg-scans-unlimited', 'src/cdom_image_jpeg.c', 'if ((uint32_t)x->cinfo.input_scan_number > CD_IMAGE_SCANS_MAX)', 'if (0)', IMG, 'jpeg-scans501'),
  ('jpeg-scans-off-by-one', 'src/cdom_image_jpeg.c', 'input_scan_number > CD_IMAGE_SCANS_MAX)', 'input_scan_number >= CD_IMAGE_SCANS_MAX)', IMG, 'jpeg-scans500'),
  ('jpeg-no-budget', 'src/cdom_image_jpeg.c', 'return cap > already ? cap - already : 0;', '(void)cap; (void)already; return max_needed;', IMGBIG, 'over-budget'),
  ('jpeg-budget-corrupt', 'src/cdom_image_jpeg.c', 'x->too_many_scans || code == JERR_NO_BACKING_STORE ? CD_IMAGE_TOO_BIG', 'x->too_many_scans ? CD_IMAGE_TOO_BIG', IMGBIG, 'over-budget'),
  ('jpeg-no-room-corrupt', 'src/cdom_image_jpeg.c', ': code == JERR_OUT_OF_MEMORY ? CD_IMAGE_NO_ROOM', ': code == JERR_OUT_OF_MEMORY ? CD_IMAGE_CORRUPT', IMG, 'bytes of room'),
  ('png-paeth-ties', 'src/cdom_image_loops.c', 'pa <= pb && pa <= pc ? a : pb <= pc ? b : c', 'pa < pb && pa < pc ? a : pb < pc ? b : c', IMG, 'png-'),
  ('png-sub-neighbour', 'src/cdom_image_loops.c', 'cur[i] = (uint8_t)(cur[i] + cur[i - bpp]); break;', 'cur[i] = (uint8_t)(cur[i] + cur[i - 1]); break;', IMG, 'png-'),
  ('png-average-up', 'src/cdom_image_loops.c', '((cur[i - bpp] + prev[i]) >> 1)', '((cur[i - bpp] + prev[i] + 1) >> 1)', IMG, 'png-'),
  ('png-up-off', 'src/cdom_image_loops.c', 'case 2: for (uint32_t i = 0; i < n; i++) cur[i] = (uint8_t)(cur[i] + prev[i]); break;', 'case 2: break;', IMG, 'png-'),
  ('png-filter9', 'src/cdom_image_loops.c', '    default: return 0;\n  }\n  return 1;', '    default: break;\n  }\n  return 1;', IMG, 'png-filter9'),
  ('png-alpha-dropped', 'src/cdom_image_png.c', 'else { out[0] = over(r[0], a); out[1] = over(r[1], a); out[2] = over(r[2], a); }\n        out[3] = 255;\n      }\n      break;\n    default:', 'else { out[0] = r[0]; out[1] = r[1]; out[2] = r[2]; }\n        out[3] = 255;\n      }\n      break;\n    default:', IMG, 'png-rgba'),
  ('png-over-floor', 'src/cdom_image_png.c', '255u * (255u - a) + 127u) / 255u', '255u * (255u - a)) / 255u', IMG, 'png-'),
  ('png-grey-unscaled', 'src/cdom_image_png.c', 'd == 8 ? v : v * 255 / mask);', 'v);', IMG, 'png-grey'),
  ('png-bit-order', 'src/cdom_image_png.c', '  for (uint32_t x = 0; x < w; x++) {\n    uint32_t v = d == 8 ? r[x] : (uint32_t)(r[x / per] >> (8 - d - d * (x % per)))', '  for (uint32_t x = 0; x < w; x++) {\n    uint32_t v = d == 8 ? r[x] : (uint32_t)(r[x / per] >> (d * (x % per)))', IMG, 'png-'),
  ('png-bit-order-palette', 'src/cdom_image_png.c', '      for (uint32_t x = 0; x < w; x++, out += 4) {\n        uint32_t v = d == 8 ? r[x] : (uint32_t)(r[x / per] >> (8 - d - d * (x % per)))', '      for (uint32_t x = 0; x < w; x++, out += 4) {\n        uint32_t v = d == 8 ? r[x] : (uint32_t)(r[x / per] >> (d * (x % per)))', IMG, 'png-'),
  ('png-palette-trns', 'src/cdom_image_png.c', 'if (ctype == 3) for (uint32_t i = 0; i < n && i < 256; i++) x->f.pal[4 * i + 3] = data[i];', 'if (ctype == 3) {}', IMG, 'png-palette'),
  ('png-plte-257', 'src/cdom_image_png.c', 'if (l % 3 || l > 768)', 'if (l % 3)', IMG, 'AddressSanitizer'),
  ('png-plte-not-3', 'src/cdom_image_png.c', 'if (l % 3 || l > 768)', 'if (l > 768)', IMG, 'multiple of 3'),
  ('png-no-plte', 'src/cdom_image_png.c', 'if (x->f.ctype == 3 && !x->seen_plte) return CD_IMAGE_CORRUPT;', 'if (0 && x->f.ctype == 3 && !x->seen_plte) return CD_IMAGE_CORRUPT;', IMG, 'no PLTE'),
  ('png-bad-stream', 'src/cdom_image_png.c', 'if (st != CD_INFLATE_OK) return CD_IMAGE_CORRUPT;', 'if (st > CD_INFLATE_STOPPED) return CD_IMAGE_CORRUPT;', IMG, 'png-adler-mismatch'),
  ('png-interlaced', 'src/cdom_image_png.c', 'if (d[12] || bd == 16) return CD_IMAGE_UNSUPPORTED;', 'if (bd == 16) return CD_IMAGE_UNSUPPORTED;', IMG, 'interlaced'),
  ('png-16-bit', 'src/cdom_image_png.c', 'if (d[12] || bd == 16) return CD_IMAGE_UNSUPPORTED;', 'if (d[12]) return CD_IMAGE_UNSUPPORTED;', IMG, '16-bit'),
  ('png-pixels-unchecked', 'src/cdom_image_png.c', '|| (uint64_t)w * h > CD_IMAGE_PIXELS_MAX) return CD_IMAGE_TOO_BIG;', ') return CD_IMAGE_TOO_BIG;', IMG, 'PNG over'),
  ('png-side-unchecked', 'src/cdom_image_png.c', 'if (w > CD_IMAGE_SIDE_MAX || h > CD_IMAGE_SIDE_MAX || (uint64_t)w * h', 'if ((uint64_t)w * h', IMG, 'PNG side'),
  ('png-rows-kept', 'src/cdom_image_png.c', '  cd_image_scaler_free(&x->sc);\n  cd_free(x->zero);\n  cd_free(x);', '  cd_image_scaler_free(&x->sc);\n  cd_free(x);', IMG, 'left'),
  ('png-whole-image', 'src/cdom_image_png.c', 'uint64_t room = row + px_n + cap;', 'uint64_t room = row + px_n + cap + (uint64_t)h * stride;', IMGBIG, 'png-p12: peak'),
  ('png-idat-not-entered', 'src/cdom_image_png.c', '        x->kind = K_IDAT;', '        x->kind = K_SKIP;', IMG, 'png-rgba'),
  ('png-window-unfiltered', 'src/cdom_image_png.c', 'n > CD_INFLATE_WINDOW ? n - CD_INFLATE_WINDOW : 0;', 'n > CD_INFLATE_WINDOW ? n - CD_INFLATE_WINDOW / 8 : 0;', IMG, 'png-'),
  # png-fast-2: the inflate's buffer of a small PNG, and no RGBA row on the RGB read (the oracle's small and wide cases)
  ('png-cap-whole-chunk', 'src/cdom_image_png.c', 'uint64_t cap = (all > keep ? all : keep) + PNG_MATCH_ROOM;', 'uint64_t cap = keep + PNG_CHUNK + 0 * all;', IMG, 'png-small16-rgba: peak'),
  # inflate-fast (docs/image-png-speed.md, "The small PNG's buffer"): the image, or the window and two rows, and the match room
  ('png-cap-chunk-formula', 'src/cdom_image_png.c', 'uint64_t cap = (all > keep ? all : keep) + PNG_MATCH_ROOM;', 'uint64_t cap = keep + (all < PNG_CHUNK ? all : PNG_CHUNK);', IMG, 'png-chunk291x299-rgb: peak'),
  ('png-cap-image-only', 'src/cdom_image_png.c', 'uint64_t cap = (all > keep ? all : keep) + PNG_MATCH_ROOM;', 'uint64_t cap = all + PNG_MATCH_ROOM;', IMG, 'png-past-last-row-rgb: status CORRUPT'),
  ('png-cap-no-match-room', 'src/cdom_image_png.c', '#define PNG_MATCH_ROOM (CD_INFLATE_CAP_MIN - CD_INFLATE_WINDOW - 1024u)', '#define PNG_MATCH_ROOM 0u', IMG, 'png-past-last-row-rgb: status CORRUPT'),
  ('png-cap-chunk-unbounded', 'src/cdom_image_png.c', '  if (cap > keep + PNG_CHUNK) cap = keep + PNG_CHUNK;\n', '', IMGBIG, 'png-p12: peak'),
  ('png-cap-no-floor', 'src/cdom_image_png.c', '  if (cap < CD_INFLATE_CAP_MIN) cap = CD_INFLATE_CAP_MIN;\n', '', IMG, 'png-small16-rgba: status CORRUPT'),
  ('png-rgba-row-on-rgb-read', 'src/cdom_image_png.c', 'uint64_t px_n = x->read == READ_AS_IS ? 0 : (uint64_t)x->f.w * (x->read == READ_GREY ? 1 : 4);', 'uint64_t px_n = (uint64_t)x->f.w * (x->read == READ_GREY ? 1 : 4);', IMG, 'png-wide8000x3-rgb: peak'),
  # png-fast (docs/image-png-speed.md): the row flow, the fused RGB read, and cdom's inflate against zlib (inflate_diff)
  ('png-prev-row-dropped', 'src/cdom_image_png.c', 'uint32_t drop = x->done ? x->row_at - row : x->row_at;', 'uint32_t drop = x->row_at;', IMG, 'png-'),
  ('png-rgb-key-fused', 'src/cdom_image_png.c', 'int rgb_read = !CDOM_IMAGE_SIMD && ct == 2 && x->f.depth == 8 && !x->f.key;', 'int rgb_read = !CDOM_IMAGE_SIMD && ct == 2 && x->f.depth == 8;', IMG, 'png-rgb-key'),
  ('img-step-rgba', 'src/cdom_image_loops.c', '{ across(h, px, sw, dw, 3); }', '{ across(h, px, sw, dw, 4); }', IMG, 'png-rgb'),
  ('inflate-dist-past-start', 'src/cdom_image_inflate.c', 'if (dist > (uint32_t)(out - start)) return 0;', 'if (dist > (uint32_t)(out - start) + 1) return 0;', IMG, 'fast loop, a distance one past the output: cdom answers 0'),
  ('inflate-dist-past-start-checked', 'src/cdom_image_inflate.c', '    if (dist > o->total) return 0;', '    if (dist > o->total + 1) return 0;', IMG, 'checked loop, a distance one past the output: cdom answers 0'),
  ('inflate-copy-overlap', 'src/cdom_image_inflate.c', '  if (dist >= 16) {\n    memcpy(o, s, 16);', '  if (dist >= 8) {\n    memcpy(o, s, 16);', IMG, 'memcpy-param-overlap'),
  ('inflate-incomplete-code', 'src/cdom_image_inflate.c', 'if (left > 0 && (codes || max != 1)) return 0;', 'if (left > 0 && codes) return 0;', IMG, 'an incomplete'),
  ('inflate-lit2-too-long', 'src/cdom_image_inflate.c', '(l1 + E_BITS(e2) <= root);', '(l1 + E_BITS(e2) <= root + 1);', IMG, 'FAIL inflate_diff'),
  ('inflate-subtable-size', 'src/cdom_image_inflate.c', '          room -= count[ll] - j;', '          room -= count[ll];', IMG, 'FAIL inflate_diff'),
  ('inflate-third-literal', 'src/cdom_image_inflate.c', 'e = lt[((uint32_t)b >> (e3 & 31)) & lm];', 'e = lt[((uint32_t)b >> ((e3 & 31) + 1)) & lm];', IMG, 'FAIL inflate_diff'),
  ('inflate-stored-nlen', 'src/cdom_image_inflate.c', 'if (cut(s) || len != (~nlen & 0xffff)) return bad;', 'if (cut(s) || (len != (~nlen & 0xffff) && 0)) return bad;', IMG, 'a stored block, NLEN wrong: zlib refuses, cdom answers 0'),
  ('inflate-sink-overdrops', 'src/cdom_image_inflate.c', '  if (drop < 0 || (uint64_t)drop > can) return 0;', '  if (drop < 0 || (uint64_t)drop > can + (uint64_t)o->pos) return 0;', IMG, 'in greedy'),
  ('inflate-adler-unchecked', 'src/cdom_image_inflate.c', 'return a == o->adler ? CD_INFLATE_OK : bad;', 'return a == o->adler || 1 ? CD_INFLATE_OK : bad;', IMG, 'png-adler-mismatch'),
  ('inflate-adler-weight', 'src/cdom_image_inflate.c', 'co = 1 | 3ull << 16 | 5ull << 32 | 7ull << 48;', 'co = 1 | 3ull << 16 | 5ull << 32 | 6ull << 48;', IMG, 'adler32 of'),
  ('inflate-reads-past-piece', 'src/cdom_image_inflate.c', '#define FAST_IN 32', '#define FAST_IN 4', IMG, 'AddressSanitizer'),
  # inflate-fast (docs/image-png-speed.md, "The inflate fast loop"): the one limit, the bit count, the extra bits, the build
  ('inflate-one-limit-gone', 'src/cdom_image_inflate.c', '      if ((size_t)(out_lim - out) > turns) out_lim = out + turns;\n', '      (void)turns;\n', IMG, 'AddressSanitizer'),
  ('inflate-bl-unmasked', 'src/cdom_image_inflate.c', 's->bb = bb; s->bl = bl & 63;', 's->bb = bb; s->bl = bl;', IMG, 'png-rgb: status CORRUPT'),
  ('inflate-len-extra-mask', 'src/cdom_image_inflate.c', 'uint32_t len = (e >> 16) + (((uint32_t)bb & ((1u << (e & 31)) - 1))', 'uint32_t len = (e >> 16) + (((uint32_t)bb & ((2u << (e & 31)) - 1))', IMG, 'FAIL inflate_diff'),
  ('inflate-rev-carry', 'src/cdom_image_inflate.c', '      while (r & inc) inc >>= 1;\n', '', IMG, 'png-rgb: status CORRUPT'),   # then inflate_diff runs out of memory: over 150 s, run by hand
  ('inflate-second-index-short', 'src/cdom_image_inflate.c', 'uint32_t e2 = lt[((uint32_t)bb >> (e & 31)) & lm];', 'uint32_t e2 = lt[((uint32_t)bb >> (e & 7)) & lm];', IMG, 'FAIL inflate_diff'),
  ('inflate-pair-first-class', 'src/cdom_image_inflate.c', 'uint32_t pair = (((e | e2) & 0xc0u) == 0)', 'uint32_t pair = ((e2 & 0xc0u) == 0)', IMG, 'FAIL inflate_diff'),
  ('inflate-sink-ignored', 'src/cdom_image_inflate.c', '  if (drop < 0 || (uint64_t)drop > can) return 0;', '  if (drop < 0) drop = 0;\n  if ((uint64_t)drop > can) return 0;', IMG, 'a sink that stops'),
  # png-fast-2 (docs/image-png-speed.md, "png-fast-2"): the review's gaps, each caught by its own case in inflate_diff
  ('inflate-hlit-287', 'src/cdom_image_inflate.c', 'if (nlen > 286 || ndist > 30) return 0;', 'if (ndist > 30) return 0;', IMG, 'HLIT 30 (287 literal/length codes): zlib refuses, cdom answers 0'),
  ('inflate-hdist-31', 'src/cdom_image_inflate.c', 'if (nlen > 286 || ndist > 30) return 0;', 'if (nlen > 286) return 0;', IMG, 'HDIST 30 (31 distance codes): zlib refuses, cdom answers 0'),
  ('inflate-repeat-first', 'src/cdom_image_inflate.c', 'if (sym == 16) { if (!i) return 0; v = lens[i - 1];', 'if (sym == 16) { v = i ? lens[i - 1] : 0;', IMG, 'a code 16 first (a repeat of no length): zlib refuses, cdom answers 0'),
  ('inflate-repeat-past-end', 'src/cdom_image_inflate.c', 'if (i + rep > nlen + ndist) return 0;', 'if (i + rep > nlen + ndist + 1) return 0;', IMG, 'a repeat one past HLIT + HDIST: zlib refuses, cdom answers 0'),
  ('inflate-repeat-unchecked', 'src/cdom_image_inflate.c', '    if (i + rep > nlen + ndist) return 0;\n', '', IMG, 'index 316 out of bounds'),
  ('inflate-block-type-3', 'src/cdom_image_inflate.c', '        } else if (type == 3) {\n          return bad;\n        } else {', '        } else {', IMG, 'block type 3 (the good block\'s bits): zlib refuses, cdom answers 0'),
  ('inflate-header-cm', 'src/cdom_image_inflate.c', '(cmf & 15) != 8 || (cmf >> 4) > 7', '(cmf >> 4) > 7', IMG, 'header CM 7: zlib refuses, cdom answers 0'),
  ('inflate-header-cinfo', 'src/cdom_image_inflate.c', '(cmf & 15) != 8 || (cmf >> 4) > 7', '(cmf & 15) != 8', IMG, 'header CINFO 8: zlib refuses, cdom answers 0'),
  ('inflate-header-fcheck', 'src/cdom_image_inflate.c', ' || (cmf * 256 + flg) % 31 || (flg & 32)', ' || (flg & 32)', IMG, 'header FCHECK wrong: zlib refuses, cdom answers 0'),
  ('inflate-header-fdict', 'src/cdom_image_inflate.c', ' || (cmf * 256 + flg) % 31 || (flg & 32)', ' || (cmf * 256 + flg) % 31', IMG, 'header FDICT set: zlib refuses, cdom answers 0'),
  ('inflate-fixed-286', 'src/cdom_image_inflate.c', '  return BAD;\n}\n\n/* The table for n', '  return OTHER(c, O_END, 0, 0);\n}\n\n/* The table for n', IMG, 'fixed literal/length symbol 286: zlib refuses, cdom answers 0'),
  ('inflate-fixed-dist-30', 'src/cdom_image_inflate.c', 'dist_base[sym]) : BAD;', 'dist_base[sym]) : LENE(c, c, 1);', IMG, 'fixed distance code 30: zlib refuses, cdom answers 0'),
  ('inflate-fixed-dist-no-30-31', 'src/cdom_image_inflate.c', '  lens[30] = lens[31] = 5;', '', IMG, 'FAIL inflate_diff'),
  ('inflate-stops-on-good', 'src/cdom_image_inflate.c', 'o->cap - o->pos < FAST_OUT) { *st = CD_INFLATE_STOPPED; return 0; }', 'o->cap - o->pos < FAST_OUT + 2048) { *st = CD_INFLATE_STOPPED; return 0; }', FUZZI, 'FAIL fuzz_inflate found'),
  ('inflate-lt-short', 'src/cdom_image_int.h', '#define CD_INFLATE_LT 2340u', '#define CD_INFLATE_LT 2339u', IMG, 'the largest tables (2340 and 592 entries): zlib inflates 8 bytes, cdom answers 1'),
  ('inflate-dt-short', 'src/cdom_image_int.h', '#define CD_INFLATE_DT 592u', '#define CD_INFLATE_DT 591u', IMG, 'the largest tables (2340 and 592 entries): zlib inflates 8 bytes, cdom answers 1'),
  ('png-grey-key', 'src/cdom_image_png.c', '(uint8_t)(f->key && v == f->key_g ? 255 :', '(uint8_t)(0 ? 255 :', IMG, 'png-grey-key'),
  ('png-rgb-key', 'src/cdom_image_png.c', 'if (r[0] == f->key_rgb[0] && r[1]', 'if (0 && r[0] == f->key_rgb[0] && r[1]', IMG, 'png-rgb-key'),
  ('png-rgb-key-low-byte', 'src/cdom_image_png.c', ' && !data[0] && !data[2] && !data[4]) {', ') {', IMG, 'png-rgb-key-over-255'),
  # the image module's hand-written wasm SIMD loops (tests/image/simd.sh, docs/image-simd-hand.md): each must make the
  # differential test FAIL on the line of the loop it breaks; a read past a buffer traps at the top of memory
  ('idct4-const', 'src/jsimd/jsimd_wasm.c', 'C1_847 = 15137', 'C1_847 = 15138', SIMD, 'FAIL idct4'),  # a rounding constant of the 4x4 IDCT is off by one
  ('idct4-pass1-round', 'src/jsimd/jsimd_wasm.c', 'V rnd = SPLAT(1 << 11), dc = SHL(z0, 2);', 'V rnd = SPLAT(1 << 10), dc = SHL(z0, 2);', SIMD, 'FAIL idct4'),  # the first pass of the 4x4 rounds with half the right constant
  ('idct4-lane', 'src/jsimd/jsimd_wasm.c', '0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);\n  wasm_v128_store32_lane(output_buf[0]', '0, 4, 8, 12, 5, 1, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);\n  wasm_v128_store32_lane(output_buf[0]', SIMD, 'FAIL idct4'),  # two output pixels of the 4x4 swapped
  ('idct4-dc-column-shortcut', 'src/jsimd/jsimd_wasm.c', '  w[0] = wasm_v128_bitselect(dc, SHR(ADD(s0, rnd), 12), acz);', '  w[0] = SHR(ADD(s0, rnd), 12);', SIMD, 'FAIL idct4'),  # the 4x4 skips the C's DC-only column shortcut for the first row (they differ when the C wraps)
  ('idct4-range-mask', 'src/jsimd/jsimd_wasm.c', 'wasm_v128_and(ADD(x, SPLAT(512)), SPLAT(1023))', 'wasm_v128_and(ADD(x, SPLAT(512)), SPLAT(2047))', SIMD, 'FAIL idct4'),  # the range limit does not wrap at 10 bits as the C's table does
  ('idct4-quant-low-half', 'src/jsimd/jsimd_wasm.c', 'ALWAYS V deqB(const JCOEF *c, const int *q, int r) { return wasm_i32x4_mul(wasm_i32x4_load16x4(c + 8 * r + 4), wasm_v128_load(q + 8 * r + 4)); }', 'ALWAYS V deqB(const JCOEF *c, const int *q, int r) { return wasm_i32x4_mul(wasm_i32x4_load16x4(c + 8 * r + 4), wasm_v128_load(q + 8 * r)); }', SIMD, 'FAIL idct4'),  # the right half of the block is dequantized with the left half's table entries
  ('idct2-const', 'src/jsimd/jsimd_wasm.c', 'C3_624 = 29692', 'C3_624 = 29693', SIMD, 'FAIL idct2'),  # a constant of the 2x2 IDCT is off by one
  ('idct2-lane', 'src/jsimd/jsimd_wasm.c', 'wasm_v128_store8_lane(output_buf[1] + output_col, b, 1);', 'wasm_v128_store8_lane(output_buf[1] + output_col, b, 3);', SIMD, 'FAIL idct2'),  # a pixel of the 2x2 goes to the wrong place
  ('idct2-sign', 'src/jsimd/jsimd_wasm.c', 'wasm_i32x4_make(1, 1, -1, -1)', 'wasm_i32x4_make(1, -1, -1, 1)', SIMD, 'FAIL idct2'),  # the 2x2's second pixel takes the wrong sign on a row
  ('ycc-const', 'src/jsimd/jsimd_wasm.c', 'MULC(crl, 91881)', 'MULC(crl, 93000)', SIMD, 'FAIL ycc'),  # the red constant is 1.4191 and not 1.4020 in the low half of a vector
  ('ycc-green-round', 'src/jsimd/jsimd_wasm.c', 'MULC(cbh, -22554), half)', 'MULC(cbh, -22554), SPLAT(30000))', SIMD, 'FAIL ycc'),  # the green channel of the high 4 lanes of 8 rounds with 30000, not 32768
  ('ycc-order', 'src/jsimd/jsimd_wasm.c', 'wasm_v128_store(o + 16, wasm_i16x8_shuffle(rg0, ba0, 4, 12, 5, 13, 6, 14, 7, 15));\n      wasm_v128_store(o + 32, wasm_i16x8_shuffle(rg1', 'wasm_v128_store(o + 32, wasm_i16x8_shuffle(rg0, ba0, 4, 12, 5, 13, 6, 14, 7, 15));\n      wasm_v128_store(o + 16, wasm_i16x8_shuffle(rg1', SIMD, 'FAIL ycc random'),  # pixels 4 to 7 and 8 to 11 of 16 swapped
  ('ycc-alpha', 'src/jsimd/jsimd_wasm.c', 'wasm_i8x16_const_splat((int8_t)0xff);\n  while', 'wasm_i8x16_const_splat((int8_t)0xfe);\n  while', SIMD, 'FAIL ycc random'),  # alpha is 254
  ('ycc-tail', 'src/jsimd/jsimd_wasm.c', 'o[0] = clamp8(y + ((91881 * cr + 32768) >> 16));', 'o[0] = clamp8(y + ((91881 * cr + 30000) >> 16));', SIMD, 'FAIL ycc random'),  # the pixels past the last 16 round the red channel with 30000
  ('ycc-read-past', 'src/jsimd/jsimd_wasm.c', 'for (; i + 16 <= n; i += 16, o += 64) {', 'for (; i + 15 <= n; i += 16, o += 64) {', SIMD, 'FAIL ycc random'),  # the loop reads and writes 16 pixels when only 15 are left (a trap at the top of memory)
  ('up-saturate', 'src/cdom_image_loops.c', 'wasm_v128_store(cur + i, wasm_i8x16_add(wasm_v128_load(cur + i), wasm_v128_load(prev + i)));', 'wasm_v128_store(cur + i, wasm_u8x16_add_sat(wasm_v128_load(cur + i), wasm_v128_load(prev + i)));', SIMD, 'FAIL unfilter'),  # Up adds with saturation, not modulo 256
  ('sub4-carry', 'src/cdom_image_loops.c', 'carry = wasm_i8x16_shuffle(v, v, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15);', 'carry = wasm_i8x16_shuffle(v, v, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11);', SIMD, 'FAIL unfilter'),  # Sub (4 bytes) carries the wrong pixel to the next 4
  ('sub3-store', 'src/cdom_image_loops.c', 'wasm_v128_store32_lane(cur + i + 8, v, 2);', 'wasm_v128_store32_lane(cur + i + 8, v, 3);', SIMD, 'FAIL unfilter'),  # Sub (3 bytes) stores its last pixel's bytes from the wrong lane
  ('avg-rounds-up', 'src/cdom_image_loops.c', '  const V one = wasm_i8x16_const_splat(1);\n  V a = wasm_i8x16_const_splat(0);\n  uint32_t i = 0;\n  for (; i + 4 <= n; i += bpp) {\n    V b = ld_px(prev + i);', '  const V one = wasm_i8x16_const_splat(0);\n  V a = wasm_i8x16_const_splat(0);\n  uint32_t i = 0;\n  for (; i + 4 <= n; i += bpp) {\n    V b = ld_px(prev + i);', SIMD, 'FAIL unfilter'),  # Avg rounds the half up (the floor is the filter)
  ('paeth-tie', 'src/cdom_image_loops.c', 'mb = wasm_i16x8_le(pb, pc);', 'mb = wasm_i16x8_lt(pb, pc);', SIMD, 'FAIL unfilter'),  # Paeth takes the up-left byte on a tie of b and c (the order of the tie is the filter)
  ('paeth-upleft', 'src/cdom_image_loops.c', '    st_px(cur + i, wasm_u8x16_narrow_i16x8(a, a), bpp);\n    c = b;\n', '    st_px(cur + i, wasm_u8x16_narrow_i16x8(a, a), bpp);\n', SIMD, 'FAIL unfilter'),  # Paeth never moves the up-left pixel along
  ('paeth-min', 'src/cdom_image_loops.c', 'wasm_i16x8_min(pb, pc)', 'wasm_i16x8_max(pb, pc)', SIMD, 'FAIL unfilter'),  # Paeth takes a when pa is at most the larger of pb and pc
  ('paeth-mask', 'src/cdom_image_loops.c', '  int m = 0xff;\n  __asm__', '  int m = 0x1ff;\n  __asm__', SIMD, 'FAIL unfilter'),  # Paeth's left value is not taken modulo 256
  # the SIMD pick (cdom.mk's APP_SIMD, shim/cdom.js start(), tools/cdpage.c, shim/boot.js; tests/pick/run.sh, tests/link/run.sh)
  ('pick-inverted', 'shim/cdom.js', 'url = url[+WebAssembly.validate(', 'url = url[+!WebAssembly.validate(', PICK, 'FAIL bundled page, readable shim: the real Chrome gets the SIMD module'),  # the scalar module where SIMD validates, and the twin where it does not
  ('pick-probe-broken', 'shim/cdom.js', '65, 0, 253, 15, 11]', '65, 0, 254, 15, 11]', PICK, 'FAIL bundled page, readable shim: the real Chrome gets the SIMD module'),  # the probe is no valid module (an opcode no browser has), so validate is false everywhere
  ('pick-bundle-order', 'tools/cdpage.c', '        module_at(o, a->wasm, a->wasm_url);', '        module_at(o, a->simd_wasm ? a->simd_wasm : a->wasm, a->wasm_url);', PICK, 'FAIL bundled page, readable shim: with no wasm SIMD'),  # the bundled pair's first entry is the twin
  ('pick-lits-unchecked', 'cdom.mk', '\t$(if $(filter $@,$(cdom_mk_simd_wasm)),@$(call cdom_mk_lits_same,$(APP_WASM),$@))\n', '', LINK, 'FAIL APP_SIMD: a twin with a string literal APP_WASM lacks builds'),  # the twin's string table is never compared with APP_WASM's
  ('pick-boot-simd', 'shim/boot.js', 'var base = typeof url === "string" ? url : url[0];', 'var base = typeof url === "string" ? url : url[1];', LEGW2J, 'FAIL booted'),  # the no-WebAssembly fallback loads the twin's wasm2js build, which is not there
  ('rgba-alpha', 'src/cdom_image_loops.c', '0, 1, 2, 16, 3, 4, 5, 16, 6, 7, 8, 16, 9, 10, 11, 16));', '0, 1, 2, 16, 3, 4, 5, 16, 6, 7, 8, 16, 9, 10, 11, 0));', SIMD, 'FAIL rgb to rgba'),  # the last pixel of 4 gets the red byte of the first as its alpha
  ('rgba-read-past', 'src/cdom_image_loops.c', 'for (; x + 6 <= w; x += 4)', 'for (; x + 5 <= w; x += 4)', SIMD, 'FAIL rgb to rgba'),  # the loop reads a byte past the row (a trap at the top of memory)
  ('across-store-lane', 'src/cdom_image_loops.c', 'wasm_v128_store32_lane(q + 2, sum, 2);', 'wasm_v128_store32_lane(q + 2, sum, 3);', SIMD, 'FAIL scaler across'),  # a pixel's blue sum is stored from its alpha lane
  ('across-no-reset', 'src/cdom_image_loops.c', '        sum = wasm_i32x4_const_splat(0);\n        ox++; hend += sw;', '        ox++; hend += sw;', SIMD, 'FAIL scaler across'),  # a finished pixel's sum carries into the next
  ('across-weight', 'src/cdom_image_loops.c', 'wasm_i32x4_splat((int32_t)(uint32_t)(b - pos))', 'wasm_i32x4_splat((int32_t)(uint32_t)(b - pos) + 1)', SIMD, 'FAIL scaler across'),  # every weight one too big
  ('down-halves', 'src/cdom_image_loops.c', 'wasm_u64x2_extmul_low_u32x4(v, w)));\n      wasm_v128_store(a + i + 2, wasm_i64x2_add(wasm_v128_load(a + i + 2), wasm_u64x2_extmul_high_u32x4(v, w)));', 'wasm_u64x2_extmul_high_u32x4(v, w)));\n      wasm_v128_store(a + i + 2, wasm_i64x2_add(wasm_v128_load(a + i + 2), wasm_u64x2_extmul_low_u32x4(v, w)));', SIMD, 'FAIL scaler down'),  # the two halves of 4 sums take each other's products
  ('down-big-weight', 'src/cdom_image_loops.c', 'if (!(wgt >> 32)) {', 'if (1) {', SIMD, 'FAIL scaler down'),  # a weight over 32 bits is cut to 32 bits
  ('down-read-past', 'src/cdom_image_loops.c', 'for (; i + 4 <= n; i += 4) {\n      V v = wasm_v128_load(h + i);', 'for (; i + 3 <= n; i += 4) {\n      V v = wasm_v128_load(h + i);', SIMD, 'FAIL scaler down'),  # the loop reads and writes 4 sums when 3 are left (a trap at the top of memory)
  ('div-round', 'src/cdom_image_loops.c', 'V x = wasm_f64x2_add(wasm_f64x2_add(wasm_f64x2_mul(hi, k33), wasm_f64x2_add(lo, lo)), tt);', 'V x = wasm_f64x2_add(wasm_f64x2_add(wasm_f64x2_mul(hi, k33), wasm_f64x2_add(lo, lo)), wasm_f64x2_sub(tt, tt));', SIMD, 'FAIL scaler divide'),  # the divide does not add the half (t) before dividing
  ('div-halves', 'src/cdom_image_loops.c', 'V hi = wasm_f64x2_convert_low_u32x4(wasm_i32x4_shuffle(v, v, 1, 3, 1, 3));', 'V hi = wasm_f64x2_convert_low_u32x4(wasm_i32x4_shuffle(v, v, 1, 1, 1, 3));', SIMD, 'FAIL scaler divide'),  # the high half of the second sum is read as the first's
  ('div-cap', 'src/cdom_image_loops.c', 'const V cap = wasm_i32x4_const_splat(255);', 'const V cap = wasm_i32x4_const_splat(254);', SIMD, 'FAIL scaler divide'),  # the quotient is capped at 254, not 255
  ('div-trunc-round', 'src/cdom_image_loops.c', 'wasm_u32x4_trunc_sat_f64x2_zero(wasm_f64x2_div(x, d))', 'wasm_u32x4_trunc_sat_f64x2_zero(wasm_f64x2_nearest(wasm_f64x2_div(x, d)))', SIMD, 'FAIL scaler divide'),  # the quotient is rounded to nearest, not truncated
  ('div-read-past', 'src/cdom_image_loops.c', 'for (; i + 2 <= n; i += 2) {\n    V v = wasm_v128_load(a + i);', 'for (; i + 1 <= n; i += 2) {\n    V v = wasm_v128_load(a + i);', SIMD, 'FAIL scaler divide'),  # the loop reads and writes two sums when one is left (a trap at the top of memory)
  # the progressive decoder and its store (src/cdom_image_jpegp.c, docs/image-progressive-speed.md): each must make the
  # differential test against libjpeg's own (tests/image/prog.sh), the IDCT table test (simd.sh) or a peak case fail
  ('jpegp-deposit', 'src/cdom_image_jpegp.c', '(l1 & (0 - (w >> 62 & 1)))', '(l1 & (0 - (w >> 61 & 1)))', PROG, 'FAIL progressive differential'),  # the second correction bit of a step goes to the wrong coefficient
  ('jpegp-correct-sign', 'src/cdom_image_jpegp.c', '*c = (int16_t)(*c + (*c >= 0 ? p1 : -p1));', '*c = (int16_t)(*c + p1);', PROG, 'FAIL progressive differential'),  # a correction moves a negative coefficient toward zero
  ('jpegp-new-sign', 'src/cdom_image_jpegp.c', 'v[pc->slot[j]] = (int16_t)(ng >> j & 1 ? -p1 : p1);', 'v[pc->slot[j]] = (int16_t)(ng >> j & 1 ? p1 : -p1);', PROG, 'FAIL progressive differential'),  # a new coefficient of a refinement takes the opposite sign
  ('jpegp-refine-eobrun', 'src/cdom_image_jpegp.c', '    s->eobrun--;\n  }\n  for (uint64_t q = cm', '  }\n  for (uint64_t q = cm', PROG, 'FAIL progressive differential'),  # an EOB run of a refinement never ends
  ('jpegp-first-run', 'src/cdom_image_jpegp.c', 'k += (fe >> 8) & 15;', 'k += (fe >> 8) & 7;', PROG, 'FAIL progressive differential'),  # the one-lookup path of a first scan loses the run's top bit
  ('jpegp-dc-refine', 'src/cdom_image_jpegp.c', 'v[0] = (int16_t)(v[0] | (1 << al));', 'v[0] = (int16_t)(v[0] | (2 << al));', PROG, 'FAIL progressive differential'),  # a DC refinement sets the wrong bit
  ('jpegp-insufficient', 'src/cdom_image_jpegp.c', 'b->cinfo->entropy->insufficient_data = TRUE; }\n  b->real = 0;', '}\n  b->real = 0;', PROG, 'FAIL progressive differential'),  # running past the scan's data does not stop the rest of the interval (a cut file)
  ('jpegp-restart-eob', 'src/cdom_image_jpegp.c', '  s->eobrun = 0;\n  s->rtg = cinfo->restart_interval;', '  s->rtg = cinfo->restart_interval;', PROG, 'FAIL progressive differential'),  # a restart marker leaves an EOB run running
  ('jpegp-skip-all', 'src/cdom_image_jpegp.c', 'if (!is_dc) x->skip = !(touched(', 'if (!is_dc) x->skip = 1 || !(touched(', PROG, 'FAIL progressive differential'),  # every AC scan is skipped, read or not
  ('jpegp-no-closure', 'src/cdom_image_jpegp.c', '{ pc->dec |= t; changed = 1; }', '{ changed = 0; }', PROG, 'FAIL progressive differential'),  # a scan is skipped when only a later scan that is decoded touches its band
  ('idct-reads-small', 'src/cdom_image_int.h', 'size == 2 ? 0xabu', 'size == 2 ? 0xa9u', SIMD, 'FAIL unread'),  # the 2x2 table drops row and column 1, which the 2x2 reads
  ('idct-reads-large', 'src/cdom_image_int.h', 'size == 2 ? 0xabu', 'size == 2 ? 0xafu', SIMD, 'FAIL unread'),  # the 2x2 table keeps row and column 2, which no 2x2 reads
  ('jpegp-window-all', 'src/cdom_image_jpegp.c', 'pc->win = (uint32_t)cp->v_samp_factor;', 'pc->win = pc->nrows;', MEMB, 'jpeg-prog444-over-budget: status OK'),  # the store's window is the whole array, which realize_virt_arrays then holds whatever the budget
  ('jpegp-store-full', 'src/cdom_image_jpegp.c', 'uint64_t nat = cd_image_idct_reads(size), z = 0;', 'uint64_t nat = cd_image_idct_reads(size) | ~0ull, z = 0;', MEMB, 'jpeg-p12-progressive: peak'),  # the store keeps every coefficient at 2/8
  ('image-ctz-builtin', 'src/cdom_image_jpegp.c', 'int to = zs ? cd_ctz64(zs) : se + 1;', 'int to = zs ? __builtin_ctzll(zs) : se + 1;', LINK, 'FAIL the image module holds i64.ctz'),  # an i64.ctz back in the module (V8 on arm64 calls out for it)
  ('jpegp-walk-short-length', 'src/cdom_image_jpegp.c', 'at += len >= 2 ? len : 2;', 'if (len < 2) break;\n      at += len;', PROG, 'walk: libjpeg read a scan the walk did not see'),  # an APPn, COM, DNL or DAC segment of length 0 or 1 ends the walk (the bug of #66: libjpeg skips nothing and goes on)
  ('jpegp-walk-dht-length', 'src/cdom_image_jpegp.c', '      if (left != 0) return;                              /* JERR_BAD_LENGTH */\n      at = q;\n    } else if (m == 0xdb)', '      at = q;\n    } else if (m == 0xdb)', PROG, 'walk: libjpeg stopped with an error the walk did not give up at'),  # the walk takes a DHT whose length get_dht refuses
  ('jpegp-walk-dht-index', 'src/cdom_image_jpegp.c', '        if ((index & 0x10 ? index - 0x10 : index) >= NUM_HUFF_TBLS) return;   /* JERR_DHT_INDEX */\n', '        (void)index;\n', PROG, 'walk: libjpeg stopped with an error the walk did not give up at'),  # the walk takes a DHT of a table number get_dht refuses
  ('jpegp-walk-dqt-index', 'src/cdom_image_jpegp.c', '        if ((t & 15) >= NUM_QUANT_TBLS) return;           /* JERR_DQT_INDEX */\n', '', PROG, 'walk: libjpeg stopped with an error the walk did not give up at'),  # the walk takes a DQT of a table number get_dqt refuses
  ('jpegp-walk-dri-length', 'src/cdom_image_jpegp.c', '      if (len != 4) return;                               /* JERR_BAD_LENGTH */\n', '', PROG, 'walk: libjpeg stopped with an error the walk did not give up at'),  # the walk takes a DRI of a length get_dri refuses
  ('jpegp-walk-ahal', 'src/cdom_image_jpegp.c', '  if (q->ah != 0 && q->al != q->ah - 1) return 0;\n', '', PROG, 'walk: libjpeg stopped with an error the walk did not give up at'),  # the walk takes a refinement band (Al not Ah - 1) jdphuff.c's start_pass refuses
  ('jpegp-walk-eof-bytes', 'src/cdom_image_jpegp.c', '((i - n) & 1) ? 0xd9 : 0xff; }', '0; }', PROG, 'walk: libjpeg stopped with an error the walk did not give up at'),  # past the end of the file the walk reads zeros, not the source's 0xFF 0xD9
  ('jpegp-walk-tem', 'src/cdom_image_jpegp.c', 'if (m == 0x01 || (m >= 0xd0 && m <= 0xd7)) goto next;', 'if (m >= 0xd0 && m <= 0xd7) goto next;', PROG, 'walk: gave up on markers libjpeg reads without an error'),  # the walk gives up at a TEM between scans
  ('jpegp-walk-stuffed', 'src/cdom_image_jpegp.c', 'if (m == 0) { at = scan_end(p + at, n - at) + at; continue; }', 'if (m == 0) return;', PROG, 'walk: gave up on markers libjpeg reads without an error'),  # the walk gives up at 0xFF 0x00 after a segment between scans, which next_marker skips
  ('jpegp-walk-band', 'src/cdom_image_jpegp.c', 'q->ss = p[at + 3 + 2 * c];', 'q->ss = p[at + 4 + 2 * c];', PROG, 'walk: a scan of the walk is not the one libjpeg read'),  # the walk reads Se as Ss
  ('jpegp-buffered', 'src/cdom_image_jpegp.c', '  if (cinfo->buffered_image) ERREXIT(cinfo, JERR_NOT_COMPILED);\n', '', PROG, 'in buffered-image mode: taken'),  # a progressive file in buffered-image mode (jpeg_read_coefficients' way in) gets cdom's controller, which has no coefficient arrays
  ('simd-flags-everywhere', 'cdom.mk', 'CDOM_IMAGE_OWN_SIMD_CFLAGS := $(cdom_mk_simd_flags)\n', 'CDOM_IMAGE_OWN_SIMD_CFLAGS := $(cdom_mk_simd_flags)\nCDOM_CFLAGS += $(if $(filter 1,$(CDOM_IMAGE_SIMD)),$(cdom_mk_simd_flags))\n', LINK, 'FAIL an app that does not call the image module has SIMD instructions'),  # -msimd128 for every file, not the image module's
  ('simd-default-on', 'cdom.mk', 'CDOM_IMAGE_SIMD ?= 0', 'CDOM_IMAGE_SIMD ?= 1', LINK, 'FAIL CDOM_IMAGE_SIMD:'),  # the default is SIMD, not scalar
  ('simd-adler-weight-16', 'src/cdom_image_inflate.c', 'w_lo = wasm_i16x8_make(16, 15,', 'w_lo = wasm_i16x8_make(17, 15,', SIMD, 'FAIL adler32'),  # the high weight of a block's first byte
  ('simd-adler-step-16', 'src/cdom_image_inflate.c', '(uint64_t)s1 * (16 * k) + 16 * hp + h2', '(uint64_t)s1 * (16 * k) + 15 * hp + h2', SIMD, 'FAIL adler32'),  # s2's 16 x s1 before each block, per lane
  ('simd-adler-no-nmax', 'src/cdom_image_inflate.c', '    if (k > ADLER_NMAX / 16) k = ADLER_NMAX / 16;\n    v128_t', '    v128_t', SIMD, 'FAIL adler32'),  # no modulo every NMAX bytes: the lanes wrap
  ('image-cap-case-missing', 'tools/image.mjs', "const p12p = cases.find((c) => c[0] === 'jpeg-p12-progressive')", "const p12p = cases.find((c) => c[0] === 'jpeg-p12-progressive-gone')", IMGALL, "FAIL wasm_js_cap: no jpeg-p12-progressive case in the oracle's cases"),  # a full run that lost the case fails; only IMAGE_BIG=0 skips it
  # Paeth as a wavefront (docs/image-png-speed.md): the SIMD loop over runs of up to 4 rows, and the PNG sink that hands them over
  ('paeth-wave-lag-off-by-one', 'src/cdom_image_loops.c', '*r1 = o[1] - (k > 1 ? 1 : 0) * LAG * bpp', '*r1 = o[1] - (k > 1 ? 1 : 0) * (LAG - 1) * bpp', SIMD, 'FAIL paeth runs'),  # row 1 one pixel off the lag its lanes' b assume
  ('paeth-wave-c-old', 'src/cdom_image_loops.c', '        c = b;\n        V st = wasm_v128_bitselect(raw, a, keep);\n        wasm_v128_store32_lane(r3', '        c = wasm_i8x16_shuffle(b, c, 0, 1, 2, 3, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);\n        V st = wasm_v128_bitselect(raw, a, keep);\n        wasm_v128_store32_lane(r3', SIMD, 'FAIL paeth runs'),  # the lagging rows keep an old up-left pixel
  ('paeth-wave-tail-drop-last', 'src/cdom_image_loops.c', '  for (uint32_t j = 0; j < k; j++) {\n    const uint8_t *up = j ? o[j - 1] : prev;', '  for (uint32_t j = 0; j < k - (k == 4); j++) {\n    const uint8_t *up = j ? o[j - 1] : prev;', SIMD, 'FAIL paeth runs'),  # a run of 4 leaves its last row's tail filtered
  ('paeth-wave-keep-off', 'src/cdom_image_loops.c', 'bpp == 3 ? wasm_i32x4_const_splat((int32_t)0xff000000u) : wasm_i32x4_const_splat(0)', 'wasm_i32x4_const_splat(0)', SIMD, 'FAIL paeth runs'),  # 3 bytes a pixel: the next pixel's raw byte is overwritten
  ('paeth-wave-store-order', 'src/cdom_image_loops.c', '        wasm_v128_store32_lane(r3 + i, st, 3); wasm_v128_store32_lane(r2 + i, st, 2);\n        wasm_v128_store32_lane(r1 + i, st, 1); wasm_v128_store32_lane(r0 + i, st, 0);', '        wasm_v128_store32_lane(r0 + i, st, 0); wasm_v128_store32_lane(r1 + i, st, 1);\n        wasm_v128_store32_lane(r2 + i, st, 2); wasm_v128_store32_lane(r3 + i, st, 3);', SIMD, 'FAIL paeth runs'),  # a run of 2 or 3: the rows past it land on the last row
  ('paeth-run-not-broken', 'src/cdom_image_loops.c', 'while (k < most && k < cap && row[(size_t)k * pitch] == 4) k++;', 'while (k < most && k < cap && (k ? pitch > 0 : row[0] == 4)) k++;', SIMD, 'FAIL paeth runs'),  # a row of another filter is taken into the run
  ('paeth-run-not-broken-png', 'src/cdom_image_loops.c', 'while (k < most && k < cap && row[(size_t)k * pitch] == 4) k++;', 'while (k < most && k < cap && (k ? pitch > 0 : row[0] == 4)) k++;', IMGW, 'FAIL wasm_js_simd: png-paeth-runs-rgb'),  # the same, through the PNG path in Chrome
  ('png-sink-keeps-row-short', 'src/cdom_image_png.c', '  uint32_t drop = x->done ? x->row_at - row : x->row_at;', '  uint32_t drop = x->row_at;', IMG, 'png-paeth-runs-rgb: '),  # the row above the next run is dropped
  ('png-sink-run-past-window', 'src/cdom_image_png.c', 'avail < left ? avail : left);', '((void)avail, left < 4 ? left : 4));', IMGW, 'FAIL wasm_js_simd: png-paeth-runs-far-rgb'),  # a run takes rows the inflate has not finished
  ('png-sink-run-scaled-once', 'src/cdom_image_png.c', '    for (uint32_t j = 0; j < k; j++, cur += row) {', '    for (uint32_t j = 0; j < 1; j++, cur += row) {', IMGW, 'FAIL wasm_js_simd: png-paeth-runs-rgb'),  # only a run's first row reaches the scaler
  # Grey PNG (docs/image-png-speed.md): the wavefront for 1 and 2 bytes a pixel (wave_n), the sink's runs of 16, the grey read
  ('wave-n-lag-off-by-one', 'src/cdom_image_loops.c', 'r[j] = o[j] - (size_t)(j < k ? j : k - 1) * LAG * W;', 'r[j] = o[j] - (size_t)(j < k ? j : k - 1) * (LAG - 1) * W;', SIMD, 'FAIL paeth runs'),  # the rows one pixel off the lag their lanes' b assume
  ('wave-n-c-old', 'src/cdom_image_loops.c', '        c = b;\n        if (W == 1) {\n#define WN_ST(j) wasm_v128_store8_lane(r[15 - j]', '        c = a;\n        if (W == 1) {\n#define WN_ST(j) wasm_v128_store8_lane(r[15 - j]', SIMD, 'FAIL paeth runs'),  # the up-left pixel is the left one
  ('wave-n-store-order', 'src/cdom_image_loops.c', '#define WN_ST(j) wasm_v128_store8_lane(r[15 - j] + i, a, 15 - j);', '#define WN_ST(j) wasm_v128_store8_lane(r[j] + i, a, j);', SIMD, 'FAIL paeth runs'),  # a run under 16: the lanes past it land on its last row
  ('wave-n-two-byte-store', 'src/cdom_image_loops.c', '#define WN_ST(j) wasm_v128_store16_lane(r[7 - j] + i, a, 7 - j);', '#define WN_ST(j) wasm_v128_store8_lane(r[7 - j] + i, a, 2 * (7 - j));', SIMD, 'FAIL paeth runs'),  # 2 bytes a pixel: only a pixel's first byte stored
  ('wave-n-lane-up-two', 'src/cdom_image_loops.c', '? wasm_i8x16_shuffle(h, z, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14)', '? wasm_i8x16_shuffle(h, z, 16, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13)', SIMD, 'FAIL paeth runs'),  # 1 byte: b from the row two above
  ('wave-n-ramp-past-end', 'src/cdom_image_loops.c', 'in[j] = j < k && t >= j * LAG && t - j * LAG < np;', 'in[j] = j < k && t >= j * LAG && t - j * LAG <= np;', SIMD, 'FAIL paeth runs'),  # the parts where rows end store one pixel past a row
  ('wave-n-cap-17', 'src/cdom_image_loops.c', 'bpp == 1 ? 16 : bpp == 2', 'bpp == 1 ? 17 : bpp == 2', SIMD, 'FAIL paeth runs'),  # a run of 17 rows of 1 byte, for 16 lanes
  ('wave-n-from-2', 'src/cdom_image_loops.c', 'if (k >= (bpp <= 2 ? 3u : 2u))', 'if (k >= 2u)', SIMD, 'FAIL paeth runs'),  # wave_n from runs of 2 (the rows each call answers differ)
  ('png-sink-grey-run-past-window', 'src/cdom_image_png.c', 'avail < left ? avail : left);', '((void)avail, left < 16 ? left : 16));', IMGW, 'FAIL wasm_js_simd: png-paeth-runs-far-grey'),  # a run of grey rows takes rows the inflate has not finished
  ('across-grey-middle', 'src/cdom_image_loops.c', '    sum += m * dw;', '    sum += m * (dw - 1);', SIMD, 'FAIL scaler across grey'),  # the whole pixels weighted one unit short
  ('across-grey-left', 'src/cdom_image_loops.c', '    if (left) sum += left * px[c];', '    if (left > 1) sum += left * px[c];', SIMD, 'FAIL scaler across grey'),  # a last piece of one unit dropped
  ('across-grey-reads-past', 'src/cdom_image_loops.c', '    if (left) sum += left * px[c];', '    sum += left * px[c];', SIMD, 'FAIL scaler across grey'),  # the row's last output pixel reads the byte past the row (a trap at the top of memory)
  ('grey-emit-scalar', 'src/cdom_image.c', '      d[1] = d[2] = d[0]; d[3] = 255;', '      d[1] = d[0]; d[3] = 255;', IMG, 'png-grey: '),  # the scalar build's grey output has no blue
  ('grey-emit-simd', 'src/cdom_image.c', '      d[0] = d[1] = d[2] = q[0]; d[3] = 255;', '      d[0] = d[1] = q[0]; d[3] = 255;', IMGW, 'FAIL wasm_js_simd: png-grey'),  # the SIMD build's grey output has no blue
  ('grey-step-unset', 'src/cdom_image_png.c', '  if (grey) x->sc.step = 1;', '  if (0) x->sc.step = 1;', IMG, 'png-grey'),  # the scaler reads grey rows as RGBA
  ('grey-key-dropped', 'src/cdom_image_png.c', 'out[x] = (uint8_t)(f->key && v == f->key_g ? 255', 'out[x] = (uint8_t)(0 && v == f->key_g ? 255', IMG, 'png-grey-key: '),  # a colour key's pixels not white
  ('grey-alpha-raw', 'src/cdom_image_png.c', 'out[x] = r[1] == 255 ? r[0] : over(r[0], r[1]);', 'out[x] = r[0];', IMG, 'png-grey-alpha: '),  # grey+alpha not composited on white
  ('grey-low-bits-scale', 'src/cdom_image_png.c', 'd == 8 ? v : v * 255 / mask);', 'd == 8 ? v : v * 255 / (mask + 1));', IMG, 'png-grey1: '),  # 1, 2 and 4 bits scaled to 8 wrong
  ('grey-rgba-row-back', 'src/cdom_image_png.c', '(x->read == READ_GREY ? 1 : 4)', '(x->read == READ_GREY ? 4 : 4)', IMG, 'png-paeth-runs-grey-key: peak'),  # the grey row as large as an RGBA one
  ('simd-emit-alpha', 'src/cdom_image.c', '    d[0] = q[0]; d[1] = q[1]; d[2] = q[2]; d[3] = 255;', '    d[0] = q[0]; d[1] = q[1]; d[2] = q[2]; d[3] = 254;', IMGW, 'FAIL wasm_js_simd: '),  # the SIMD build's emit writes the wrong alpha
  ('sjlj-sp-kept-low', 'src/cdom_image_sjlj.c', '  sp_set(sp);', '  sp_set(sp_get() + 0 * sp);', UNIT, "FAIL the image module's guard"),
  ('sjlj-eh-val-zero', 'src/cdom_image_sjlj_eh.c', 'jb->arg.val = val ? val : 1;', 'jb->arg.val = 0 * val;', IMGW, 'wasm_eh: jpeg-dqt-bad', HANGS),  # setjmp answers 0, the first pass, so the decode starts again, meets the same error and jumps again, for good
  ('shim-image-swallows', 'shim/cdom.js', '      if (e !== IMAGE_THROW) throw e;\n', '', UNIT, 'a trap inside the body'),
  ('shim-image-answers-0', 'shim/cdom.js', '      if (e !== IMAGE_THROW) throw e;\n      return 1;', '      if (e !== IMAGE_THROW) throw e;\n      return 0;', UNIT, "FAIL the image module's guard"),
  ('shim-image-throws-error', 'shim/cdom.js', '    throw IMAGE_THROW;', '    throw new Error("image");', UNIT, "FAIL the image module's guard"),
  ('vendor-edited', 'third_party/libjpeg-turbo-3.2.1/src/jdhuff.c', '#define JPEG_INTERNALS', '#define JPEG_INTERNALS\n/* edited */', IMG, 'differs libjpeg-turbo-3.2.1/src/jdhuff.c'),
  ('vendor-cut-moved', 'tools/third_party.py', "'tinfl.c': ('miniz.c', 2278, 3027)", "'tinfl.c': ('miniz.c', 2278, 3026)", IMG, 'not its inflate section'),
  ('sjlj-js-gets-eh', 'cdom.mk', 'cdom_mk_sjlj_js := -DCDOM_IMAGE_SJLJ_JS', 'cdom_mk_sjlj_js := -DCDOM_IMAGE_SJLJ_EH -mexception-handling -mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=true', LINK, "FAIL the image module's wasm test app does not build"),
  ('image-import-in-core', 'src/cdom.c', '#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))\n', '#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))\nIMPORT("image_throw") void j_core_image_throw(void);\n__attribute__((export_name("cd_core_panic"))) void cd_core_panic(void) { j_core_image_throw(); }\n', LINK, "of the shim's imports (wanted none)"),
  # the browser resize ban (tools/resize_ban.sh): each banned form planted in shim/, src/, cdom.mk and tools/
  ('ban-bitmap-resize', 'shim/cdom.js', '    return Date.now();', '    createImageBitmap(file, { resizeWidth: 900, resizeHeight: 600 });\n    return Date.now();', RESIZE, 'FAIL resize ban: shim/cdom.js'),
  ('ban-bitmap-quality', 'shim/cdom.js', '    return Date.now();', '    createImageBitmap(file, { resizeQuality: "high" });\n    return Date.now();', RESIZE, 'FAIL resize ban: shim/cdom.js'),
  ('ban-smoothing', 'shim/cdom.js', '    return Date.now();', '    canvas.getContext("2d").imageSmoothingQuality = "high";\n    return Date.now();', RESIZE, 'FAIL resize ban: shim/cdom.js'),
  ('ban-draw-5', 'shim/cdom.js', '    return Date.now();', '    context.drawImage(bitmap, 0, 0, w, h);\n    return Date.now();', RESIZE, 'FAIL resize ban: shim/cdom.js'),
  ('ban-draw-9-lines', 'shim/cdom.js', '    return Date.now();', '    context.drawImage(bitmap,\n      0, 0, f(a, b), sh,\n      0, 0, Math.round(w / 2), h);\n    return Date.now();', RESIZE, 'FAIL resize ban: shim/cdom.js'),
  ('ban-src', 'src/cdom_file.c', 'void cd_file_read_cb(', '/* resizeWidth */\nvoid cd_file_read_cb(', RESIZE, 'FAIL resize ban: src/cdom_file.c'),
  ('ban-mk', 'cdom.mk', 'CDOM_OPT ?= -Oz\n', 'CDOM_OPT ?= -Oz\n# drawImage(img, 0, 0, w, h)\n', RESIZE, 'FAIL resize ban: cdom.mk'),
  ('ban-tools', 'tools/minshim.mjs', "import fs from 'node:fs';", "import fs from 'node:fs';\nconst q = { imageSmoothingQuality: 'low' };", RESIZE, 'FAIL resize ban: tools/minshim.mjs'),
  # the third tier, IE 9 (shim/boot.js, tools/legacy/memory-words.js, the seed exception, the shim-ie9 gate)
  ('ie9-boot-typed-when-absent', 'shim/boot.js', '    if (!window[typed[i]]) words = true;', '    if (!window[typed[i]]) words = false;', LEG9, 'FAIL booted'),
  ('ie9-words-load8-off-by-one', 'tools/legacy/memory-words.js', 'function load8(ptr) {\n  return memory.load8_u(ptr);', 'function load8(ptr) {\n  return memory.load8_u(ptr + 1);', LEG9, 'FAIL booted'),
  ('ie9-seed-weak-with-crypto', 'shim/cdom.js', '    if (globalThis.crypto && crypto.getRandomValues) {\n      return crypto.getRandomValues', '    if (false) {\n      return crypto.getRandomValues', LEG1, 'FAIL cd_seed from crypto.getRandomValues, never Math.random, where crypto exists'),
  # the fourth tier, IE 6 to 8 (shim/boot.js, tools/legacy/es3.mjs, dom-old.js, shim-old.js, polyfills-es3.js)
  ('ie6-boot-new-when-es5-missing', 'shim/boot.js', '    if (!es5[i]) old = true;', '    if (!es5[i]) old = false;', LEG6, 'FAIL booted'),
  ('ie6-floor-without-listeners', 'shim/boot.js', '  if (!d.addEventListener && !d.attachEvent) lack[lack.length]', '  if (false) lack[lack.length]', LEGBELOW, "FAIL one error, the boot's"),
  ('boot-serial-fallback-load', 'shim/boot.js', '  load(old ? "cdom.legacy-ie6.js" : words ? "cdom.legacy-ie9.js" : "cdom.legacy.js", loaded);\n  load(base + (old || words ? ".ie9.js" : ".js"), loaded);',
   '  load(old ? "cdom.legacy-ie6.js" : words ? "cdom.legacy-ie9.js" : "cdom.legacy.js", function () { loaded(); load(base + (old || words ? ".ie9.js" : ".js"), loaded); });', LEG9, 'FAIL legacy.wasm.ie9.js requested while'),  # a serial load: the app's request comes only after the shim file has arrived (loaded() counts the shim, then the app is requested), so the held shim times out
  ('ie6-boot-swallows-failed-start', 'shim/boot.js', '    setTimeout(function () { throw e; }, 0);', '', LEG, 'FAIL a failed start is thrown again'),
  ('ie6-catch-not-renamed', 'tools/legacy/es3.mjs', '      if (t.isIdentifier(p.node.param)) p.scope.rename(', '      if (false) p.scope.rename(', ES3T, "catch parameter's name used elsewhere in its function"),
  ('ie6-catch-gate-off', 'tools/legacy/es3.mjs', '      if (found) leaks.add(n);', '      if (false) leaks.add(n);', ES3T, "FAIL catch (e) over the function's own e is refused"),
  ('ie6-reserved-gate-off', 'tools/legacy/es3.mjs', '  if (minified) walk(tree);', '  if (false) walk(tree);', ES3T, 'FAIL a.class is refused'),
  ('ie6-es5-gate-off', 'tools/legacy/es3.mjs', '      if (ES5_NAMES.has(key) && !ES5_SUPPLIED.has(key) && !ok.has(key)) es5.add(key);', '      if (false) es5.add(key);', ES3T, 'FAIL Object.getOwnPropertyNames is refused'),
  ('ie6-proto-gate-off', 'tools/legacy/es3.mjs', "  if (/\\b__proto__\\b/.test(src)) throw", "  if (false) throw", ES3T, 'FAIL __proto__ is refused'),
  # the fifth tier, the frames tier (shim/boot.js's first part, tools/legacy/frames.js, js11.mjs, tools/cdp.mjs's FRAMES_OLD:
  # docs/frames-design.md)
  ('frames-boot-takes-dom', 'shim/boot.js', '  cdomBootFrames(url, mount, env, ready, options);\n}', '  cdomBootDom(url, mount, env, ready, options);\n}', FRAMES, 'FAIL booted'),
  ('frames-boot-not-js10', 'shim/boot.js', '  var base = url;\n  if (!url.substring)', '  var base = url, none = {};\n  if (!url.substring)', FRAMES, 'shim/boot.js part 1 is not JavaScript 1.0'),
  ('frames-shim-not-js11', 'tools/legacy/frames.js', '  cf_fields[cf_nfields] = x;', '  cf_fields.push(x);', FRAMES, 'frames.js is not JavaScript 1.1: .push'),
  ('frames-profile-keeps-getelementbyid', 'tools/cdp.mjs', "    'getElementById', 'importNode',", "    'importNode',", FRAMES, 'FAIL no DOM mutation in the page'),
  ('frames-seed-zero', 'tools/legacy/frames.js', '  return (r ^ t) >>> 0;', '  return 0;', FRAMES, 'FAIL two boots with a different clock give different seeds'),
  ('frames-seed-stray-random', 'tools/legacy/frames.js', 'var cf_hasFrom = 0;', 'var cf_hasFrom = 0, cf_stray = Math.random();', LINK, 'FAIL tools/legacy/frames.js:'),
  ('frames-seed-second-draw', 'tools/legacy/frames.js', '  t = d.getTime();', '  t = d.getTime() + Math.random();', LINK, '2 Math.random in cf_seed(options), want exactly 1'),
  ('frames-no-sync', 'tools/legacy/frames.js', '  if (!w || !cdomF.draws) return;\n  form = w.document.forms[0];', '  return;\n  form = w.document.forms[0];', FRAMES, 'FAIL typed text C has not seen survives a redraw'),
  ('frames-draws-unchanged', 'tools/legacy/frames.js', '  if (cf_dirty || cf_pending) cf_draw();', '  if (true) cf_draw();', FRAMES, 'FAIL an event whose handler changes nothing draws nothing'),
  ('frames-draw-per-op', 'tools/legacy/frames.js', 'function cf_placeNew(x) {\n  cf_dirty = 1;\n', 'function cf_placeNew(x) {\n  cf_dirty = 1;\n  cf_draw();\n', FRAMES, 'FAIL an export of 15 ops (5 items) draws once'),
  ('frames-path-counts-text', 'tools/legacy/frames.js', '    for (s = x.v; s; s = s.v) if (s.y == 1) index++;', '    for (s = x.v; s; s = s.v) index++;', FRAMES, 'FAIL links and a span'),
  ('frames-text-not-wrapped', 'tools/legacy/frames.js', '    if (inh & 1 << cf_CLICK && !(ctx & 3) && x.p && x.p != cf_mount && !cf_blank(s)) {', '    if (false) {', FRAMES, 'FAIL links and a span'),
  ('frames-focus-not-restored', 'tools/legacy/frames.js', '      el.focus();\n', '', FRAMES, 'FAIL the focus and the caret survive it'),
  ('frames-caret-not-restored', 'tools/legacy/frames.js', '      if (f.ss >= 0 && cf_isText(el.type) && el.setSelectionRange) el.setSelectionRange(f.ss, f.se);', '      if (false) el.setSelectionRange(f.ss, f.se);', FRAMES, 'FAIL the focus and the caret survive it'),
  ('frames-bold-dropped', 'tools/legacy/frames.js', '  if (v == "bold" || v == "bolder" || parseInt(v, 10) >= 600) cf_wrapIn("<b>", "</b>");', '', FRAMES, 'FAIL color, font-weight'),
  ('frames-hidden-drawn', 'tools/legacy/frames.js', '  if (cf_in(cf_SKIP, t) || cf_pair(x.s, "display") == "none") return;', '  if (cf_in(cf_SKIP, t)) return;', FRAMES, 'FAIL display: none'),
  ('frames-style-attr-unread', 'tools/legacy/frames.js', '  if (name != "style") {', '  if (true) {', FRAMES, "FAIL the heading's color as FONT COLOR"),
  ('frames-change-drawn-at-once', 'tools/legacy/frames.js', '  if (cf_dirty && later) {', '  if (false) {', FRAMES, 'FAIL the click that left the field still reached C'),
  ('frames-no-input-on-change', 'tools/legacy/frames.js', '  if (now != x.seen) {', '  if (false) {', FRAMES, 'FAIL leaving the field: one input'),
  ('frames-checkbox-no-change', 'tools/legacy/frames.js', '      cf_fire(x, cf_INPUT, 0);\n      cf_fire(x, cf_CHANGE, 0);\n', '      cf_fire(x, cf_INPUT, 0);\n', FRAMES, "FAIL the checkbox's change reached C"),
  ('frames-recover-off', 'tools/legacy/frames.js', '  if (cf_busy || cf_holds) {', '  if (false) {', FRAMES, 'FAIL the next click recovers'),
  ('frames-history-entry', 'tools/legacy/frames.js', '  d.close();\n  d.onmousedown = cf_onDown;', '  d.close();\n  w.location.hash = "cdomfocus";\n  d.onmousedown = cf_onDown;', FRAMES, 'FAIL no redraw added a history entry'),
  ('frames-utf8-two-bytes', 'tools/legacy/frames.js', '      u = (c & 31) << 6 | cf_load8(p) & 63;\n      p++;', '      u = (c & 31) << 6 | cf_load8(p) & 63;', FRAMES, 'FAIL the input reached C, which read the value'),
  ('frames-encode-two-bytes', 'tools/legacy/frames.js', '      cf_store8(p + cf_wrote, 192 | u >> 6);', '      cf_store8(p + cf_wrote, 224 | u >> 6);', FRAMES, 'FAIL the input reached C, which read the value'),
  ('frames-no-fallback', 'tools/legacy/frames.js', '    if (!cf_over && (document.readyState == "complete"', '    if (false && (document.readyState == "complete"', FRAMES, 'FAIL frames-body.html (a BODY tag)'),
  ('frames-timer-handle', 'tools/legacy/frames.js', '  cf_tid[cf_timers] = setTimeout("cdomF.t(" + cf_timers + ")", ms);', '  cf_tid[cf_timers] = setTimeout("cdomF.t(" + (cf_timers + 1) + ")", ms);', FRAMES, 'FAIL a timer reached C'),
  ('frames-esc-off', 'tools/legacy/frames.js', 'function cf_esc(s) {\n  var out = "", k, c;', 'function cf_esc(s) {\n  return s;\n  var out = "", k, c;', FRAMES, 'FAIL a text with markup characters reads back as them'),
  ('js11-new-member-allowed', 'tools/legacy/js11.mjs', "      add('a member of a new expression', n);", '', JS11T, 'FAIL var t = new Date().getTime(); is refused'),
  ('frames-new-member', 'tools/legacy/frames.js', 'function cf_op_epoch() {\n  var d = new Date();\n  return d.getTime();', 'function cf_op_epoch() {\n  return (new Date()).getTime();', JS11T, 'frames.js is not JavaScript 1.1: a member of a new expression'),  # the build's gate refuses it before js11.mjs runs
  ('js11-assign-in-test-allowed', 'tools/legacy/js11.mjs', "    if (test) walk(test, (m) => { if (m.type === 'AssignmentExpression') add('an assignment in a condition', m); });", '', JS11T, 'FAIL if (a = f()) g(); is refused'),
  ('frames-assign-in-test', 'tools/legacy/frames.js', '  while (a < b && s.charAt(a) == " ") a++;', '  while (a < b && (c = s.charAt(a)) == " ") a++;', JS11T, 'frames.js is not JavaScript 1.1: an assignment in a condition'),  # the build's gate refuses it before js11.mjs runs
  ('js11-isnan-allowed', 'tools/legacy/js11.mjs', "'globalThis', 'isNaN', 'setInterval']);", "'globalThis']);", JS11T, 'FAIL if (isNaN(x)) f(); is refused'),
  ('js11-substr-allowed', 'tools/legacy/js11.mjs', "'hasOwnProperty', 'substr', 'isNaN', 'setInterval']);", "'hasOwnProperty', 'isNaN', 'setInterval']);", JS11T, 'FAIL var s = t.substr(1, 2); is refused'),
  ('js10-arguments-allowed', 'tools/legacy/js11.mjs', "      if (js10 && JS10_IDENTS.has(n.name)) add(n.name, n);\n", '', JS11T, 'FAIL js10: function f() { return arguments.length; } is refused'),
  ('js10-images-allowed', 'tools/legacy/js11.mjs', "'constructor', 'onerror', 'images',", "'constructor', 'onerror',", JS11T, 'FAIL js10: var n = document.images.length; is refused'),
  ('js11-with-allowed', 'tools/legacy/js11.mjs', " DebuggerStatement: 'debugger', WithStatement: 'with',", " DebuggerStatement: 'debugger',", JS11T, 'FAIL with (o) f(); is refused'),
  ('js11-debugger-allowed', 'tools/legacy/js11.mjs', " DebuggerStatement: 'debugger', WithStatement: 'with',", " WithStatement: 'with',", JS11T, 'FAIL debugger; is refused'),
  ('js11-getter-allowed', 'tools/legacy/js11.mjs', "    if (n.type === 'Property' && n.kind !== 'init') add('a getter or setter', n);", '', JS11T, 'FAIL var o = new F(); o.k = 1; x = { get k() { return 1; } }; is refused'),
  ('frames-ie4-caret-unread', 'tools/legacy/frames.js', '  } else if (w.document.selection && el.createTextRange) {\n    r = w.document.selection', '  } else if (false) {\n    r = w.document.selection', FRAMES, 'FAIL ie4: typing in the middle of a field C answers goes on at the caret'),
  ('frames-ns4-draws-while-typing', 'tools/legacy/frames.js', '  cf_fire(x, cf_KEYUP, cf_key(e));\n  cf_release(2 * cf_blind(x));', '  cf_fire(x, cf_KEYUP, cf_key(e));\n  cf_release(0);', FRAMES, 'FAIL ns4: no draw while a key is typed in the focused field'),
  ('frames-profile-ie4-keeps-setselectionrange', 'tools/cdp.mjs', "'selectionDirection', 'setSelectionRange', 'setRangeText']) delete p[k];", "'selectionDirection', 'setRangeText']) delete p[k];", FRAMES, 'FAIL ie4: the field has no selectionStart'),
  ('frames-press-untracked', 'tools/legacy/frames.js', '  cf_pressed = 1;\n  cf_pressWaits = 0;\n', '', FRAMES, 'FAIL a click held 600 ms that left a field reaches C'),
  ('frames-radio-renamed', 'tools/legacy/frames.js', '  if (x.t == "input" && name != null && cf_type(x) == "radio") return', '  if (false) return', FRAMES, 'FAIL two radios of one name are one group'),
  ('frames-form-nested', 'tools/legacy/frames.js', ' nav main aside figure form ";', ' nav main aside figure ";', FRAMES, 'FAIL a form from C is a DIV'),
  ('frames-drawing-not-settled', 'tools/legacy/frames.js', '  if (!cf_drawing) return;\n  cf_drawing = 0;\n', '  if (!cf_drawing) return;\n', FRAMES, 'FAIL a throw while the shim draws does not lock the page'),
  ('frames-select-checkbox-throws', 'tools/legacy/frames.js', '      if (f.ss >= 0 && cf_isText(el.type) && el.setSelectionRange)', '      if (f.ss >= 0 && el.setSelectionRange)', FRAMES, 'FAIL cd_call(checkbox, "select") does not throw'),
  ('frames-move-foreign', 'tools/legacy/frames.js', '  if (next && next.p != parent) {', '  if (false) {', FRAMES, "FAIL cd_move before another parent's node moves nothing"),
  ('frames-move-self', 'tools/legacy/frames.js', '  if (next == x) return;\n', '', FRAMES, 'FAIL cd_move before itself moves nothing'),  # the self-link breaks the next draw: Chrome throws a RangeError in cf_w
  ('frames-tag-case', 'tools/legacy/frames.js', '  var t = name.toLowerCase(), k;', '  var t = name, k;', FRAMES, 'FAIL a tag in capitals is the same tag'),
  ('frames-tag-unchecked', 'tools/legacy/frames.js', '  if (t.length && k == t.length) return t;', '  return t;', FRAMES, 'FAIL a tag that is not letters and digits is a SPAN'),
  ('frames-start-trap-unnoted', 'tools/legacy/frames.js', '    cf_fail("cdom: cd_start did not return (a trap in cd_main?)");\n', '', FRAMES, 'FAIL frames.html: a trap in cd_start is noted'),
  ('frames-start-trap-no-poll', 'tools/legacy/frames.js', '  setTimeout("cdomF.p()", 0);\n  cf_startDone = 0;\n  cf_exp("cd_start", cf_seed(options));\n  cf_startDone = 1;\n', '  cf_startDone = 0;\n  cf_exp("cd_start", cf_seed(options));\n  cf_startDone = 1;\n  setTimeout("cdomF.p()", 0);\n', FRAMES, 'FAIL frames-body.html: a trap in cd_start is noted'),
  ('js11-try-allowed', 'tools/legacy/js11.mjs', "SwitchStatement: 'switch', DoWhileStatement: 'do-while', LabeledStatement: 'a label', TryStatement: 'try',", "SwitchStatement: 'switch', DoWhileStatement: 'do-while', LabeledStatement: 'a label',", JS11T, 'FAIL try { f(); } catch (e) {} is refused'),
  ('js11-literal-allowed', 'tools/legacy/js11.mjs', "ThrowStatement: 'throw', ObjectExpression: 'an object literal', ", "ThrowStatement: 'throw', ", JS11T, 'FAIL var o = {}; is refused'),
  ('js11-strict-equality-allowed', 'tools/legacy/js11.mjs', "    if (n.type === 'BinaryExpression' && (n.operator === '===' || n.operator === '!==')) add(n.operator, n);", '', JS11T, 'FAIL if (a === b) f(); is refused'),
  ('js11-nested-allowed', 'tools/legacy/js11.mjs', "    if (n.type === 'FunctionDeclaration' && depth > 0) add(", "    if (false) add(", JS11T, 'FAIL function f() { function g() {} return g; } is refused'),
  ('js11-regex-allowed', 'tools/legacy/js11.mjs', "    if (n.type === 'Literal' && n.regex) add('a regex literal', n);", '', JS11T, 'FAIL var r = /a+/; is refused'),
  ('js11-push-allowed', 'tools/legacy/js11.mjs', "const METHODS = new Set(['push', ", "const METHODS = new Set([", JS11T, 'FAIL .push is refused'),
  ('js11-allowed-ignored', 'tools/legacy/js11.mjs', "      if (METHODS.has(name) && !ok.has(name)) add(", "      if (METHODS.has(name)) add(", JS11T, 'shim/boot.js part 1 is not JavaScript 1.0: .getElementById'),  # the build refuses the boot's own feature tests first
  ('js11-undefined-allowed', 'tools/legacy/js11.mjs', "const GLOBALS = new Set(['undefined', ", "const GLOBALS = new Set([", JS11T, 'FAIL var u = undefined; is refused'),
  ('js10-typeof-allowed', 'tools/legacy/js11.mjs', "    if (js10 && n.type === 'UnaryExpression' && (n.operator === 'typeof' || n.operator === 'void')) add(n.operator, n);", '', JS11T, 'FAIL js10: if (typeof x == "string") f(); is refused'),
  ('js10-names-allowed', 'tools/legacy/js11.mjs', "      if (js10 && JS10_NAMES.has(n.name)) add(n.name, n);", '', JS11T, 'FAIL js10: var a = new Array(); is refused'),
  ('mount-waits-on-gone-domcontentloaded', 'shim/cdom.js', '      return new Promise((resolve) => addEventListener("load", resolve, { once: true })).then(found);', '      return new Promise((resolve) => doc.addEventListener("DOMContentLoaded", resolve, { once: true })).then(found);', LEG, 'FAIL a missing mount is an error once the page has loaded'),
  # no LEGACY=ie6 twin of the one above: dom-old.js listens for DOMContentLoaded as the window's load there, so it is the same wait
  ('mount-missing-no-error', 'shim/cdom.js', '      if (!element) {\n        throw new Error(`cdom: no element has the id ${mount}`);', '      if (false) {\n        throw new Error(`cdom: no element has the id ${mount}`);', LEG, 'FAIL a missing mount is an error once the page has loaded'),
  # IE 5.0 (JScript 5.0, docs/ie5-design.md)
  ('ie5-holds-fallback-everywhere', 'tools/legacy/polyfills-es3.js', '    holds = new Function("o", "k", "return k in o");', '    throw 0;', LEG6, 'FAIL forEach over 10,000 undefined slots'),
  ('ie5-in-gate-off', 'tools/legacy/es3.mjs', "    if (n.type === 'BinaryExpression' && n.operator === 'in') found++;", '', ES3T, 'FAIL a binary in is refused'),
  ('ie5-numkey-gate-off', 'tools/legacy/es3.mjs', "  if (numKeys) throw", "  if (false) throw", ES3T, 'FAIL { 1: 0 } is refused'),
  ('ie5-regex-flag-gate-off', 'tools/legacy/es3.mjs', "  if (/[^gi]/.test(flags)) return true;", '', ES3T, 'FAIL /a/m is refused'),
  ('ie5-regex-group-gate-off', 'tools/legacy/es3.mjs', "    if (c === '?' && (prev === '(' || prev === 'q')) return true;", '', ES3T, 'FAIL /a+?/ is refused'),
  ('ie5-regex-class-slash-gate-off', 'tools/legacy/es3.mjs', "if (c === '/') return true; ", '', ES3T, 'FAIL /[/]/ is refused (Firefox 1.0)'),
  ('ie5-error-gate-off', 'tools/legacy/es3.mjs', "  if (minified && errorsWithMessage(tree)) throw", "  if (false) throw", ES3T, 'FAIL new Error("x") is refused'),
  ('ie5-error-not-rewritten', 'tools/legacy/es3.mjs', "        p.replaceWith(t.callExpression(t.identifier('cdomError'), [n.arguments[0]]));", '', ES3T, 'cdom.legacy-ie6.js makes an Error with a message'),
  ('ie5-boot-error-gate-off', 'tools/legacy/es3.mjs', "found.add('Error(message)');", "0;", ES3T, 'FAIL boot50 finds an Error made with a message'),
  ('ie5-boot50-off', 'tools/legacy/es3.mjs', "      if (names.has(key)) found.add(key);", '', ES3T, 'FAIL boot50 finds push, undefined and apply'),
  ('ie5-jscript55-unlisted', 'tools/legacy/es3.mjs', "'.propertyIsEnumerable', '.isPrototypeOf', 'encodeURI', 'decodeURI', 'encodeURIComponent', 'decodeURIComponent', '.toFixed',", "'.propertyIsEnumerable', '.isPrototypeOf', 'encodeURI', 'decodeURI', 'encodeURIComponent', 'decodeURIComponent',", ES3T, 'FAIL toFixed (JScript 5.5) is refused'),
  ('ie5-hold-in-shipped', 'tools/legacy/dom-old.js', '  function remove(x) {', '  // http://10.0.2.2:8096/x.gif\n  function remove(x) {', ES3T, 'the IE 5 test hold (port 8096) is in a shipped or built file'),
  ('ie5-replace-fn-gate-off', 'tools/legacy/es3.mjs', "  if (replaceWithFunction(tree)) throw", "  if (false) throw", ES3T, 'FAIL replace with a function is refused'),
  # SVG through VML on the fourth tier (docs/vml-design.md)
  ('vml-h-as-v', 'tools/legacy/vml.js', '        lineTo(args[0] + ox, y);', '        lineTo(x, args[0] + oy);', VMLT, 'FAIL H, V, h and v as l'),
  ('vml-pairs-after-m-moves', 'tools/legacy/vml.js', '        cmd = rel ? "l" : "L";                 /* pairs after a move are lines */', '', VMLT, 'FAIL pairs after M are lines'),
  ('vml-z-no-move-back', 'tools/legacy/vml.js', '  function lineTo(px, py) {\n    if (closed) moveTo(x0, y0);', '  function lineTo(px, py) {', VMLT, 'FAIL L after Z starts at the subpath start'),
  ('vml-s-no-reflection', 'tools/legacy/vml.js', '        var ax = prev === "C" || prev === "S" ? 2 * x - cx : x, ay = prev === "C" || prev === "S" ? 2 * y - cy : y;', '        var ax = x, ay = y;', VMLT, 'FAIL S reflects the C before'),
  ('vml-t-no-reflection', 'tools/legacy/vml.js', '          qx = prev === "Q" || prev === "T" ? 2 * x - cx : x;', '          qx = x;', VMLT, 'FAIL T reflects the Q before'),
  ('vml-q-half-not-two-thirds', 'tools/legacy/vml.js', 'curveTo(sx0 + 2 / 3 * (qx - sx0),', 'curveTo(sx0 + 1 / 2 * (qx - sx0),', VMLT, 'FAIL Q: '),
  ('vml-number-one-point', 'tools/legacy/vml.js', 'var TOKEN = /[A-Za-z]|[+\\-]?(\\d+\\.?\\d*|', 'var TOKEN = /[A-Za-z]|[+\\-]?(\\d+\\.?[\\d.]*|', VMLT, 'FAIL a number ends at a second point'),
  ('vml-malformed-goes-on', 'tools/legacy/vml.js', '      if (k < need) break;\n', '', VMLT, 'FAIL a missing number ends the path'),
  ('vml-floor-not-round', 'tools/legacy/vml.js', '    var a = (msx * px + mtx) * K + 0.5, b = (msy * py + mty) * K + 0.5, i, j;', '    var a = (msx * px + mtx) * K, b = (msy * py + mty) * K, i, j;', VMLT, 'FAIL rounding to a hundredth of a pixel'),
  ('vml-truncated-not-floored', 'tools/legacy/vml.js', '      if (i > a) i--;\n', '', VMLT, 'FAIL exponent signs, a leading point and plus'),
  ('vml-ctm-ignored', 'tools/legacy/vml.js', '    var a = (msx * px + mtx) * K + 0.5, b = (msy * py + mty) * K + 0.5, i, j;', '    var a = px * K + 0.5, b = py * K + 0.5, i, j;', VMLT, 'FAIL the CTM'),
  ('vml-arc-relative-end-absolute', 'tools/legacy/vml.js', '        arc(args[0], args[1], args[2], args[3], args[4], args[5] + ox, args[6] + oy);', '        arc(args[0], args[1], args[2], args[3], args[4], args[5], args[6]);', VMLT, 'FAIL a, relative'),
  ('vml-arc-radii-not-scaled', 'tools/legacy/vml.js', '    if (lam > 1) {', '    if (false) {', VMLT, 'FAIL radii too small scale up'),
  ('vml-arc-sweep-ignored', 'tools/legacy/vml.js', '    if (sweep && dt < 0) dt += 2 * Math.PI;\n    else ', '    ', VMLT, 'FAIL a large arc of an ellipse'),
  ('vml-arc-half-turns', 'tools/legacy/vml.js', 'Math.ceil(Math.abs(dt) / (Math.PI / 2) - 1e-9)', 'Math.ceil(Math.abs(dt) / Math.PI - 1e-9)', VMLT, 'of the true arc (tolerance'),
  ('vml-flags-not-split', 'tools/legacy/vml.js', '    else toks[ti] = t.substring(1);', '    else ti++;', VMLT, 'FAIL flags run into what follows'),
  ('vml-stray-skipped', 'tools/legacy/vml.js', '([eE][+\\-]?\\d+)?|[^\\s,]/g;', '([eE][+\\-]?\\d+)?/g;', VMLT, 'FAIL a character no path has ends it there'),
  ('vml-point-a-number', 'tools/legacy/vml.js', '    if (t.length < 2 && (c < "0" || c > "9")) return null;', '    if (t.length < 2 && c !== "." && (c < "0" || c > "9")) return null;', VMLT, 'FAIL a point alone is no number'),
  # What real IE 6 needs (docs/vml-design.md, section 9), each against the stand-in's model of it (tools/cdp.mjs)
  ('vml-no-namespace', 'tools/legacy/vml.js', '      try { ns.add("v", URN); } catch (caughtVml) {}', '      0;', LEG6, 'FAIL no VML node made that IE 6 would not draw'),
  ('vml-ns-implementation', 'tools/legacy/vml.js', '      try { ns.add("v", URN); }', '      try { ns.add("v", URN, "#default#VML"); }', LEG6, 'FAIL <path> as a v:shape'),
  ('vml-shape-before-namespace', 'tools/legacy/vml.js', '    if (!ready) setup();\n    return doc.createElement("v:" + tag);', '    var made = doc.createElement("v:" + tag);\n    if (!ready) setup();\n    return made;', LEG6, 'FAIL no VML node made that IE 6 would not draw'),
  ('vml-no-behavior-rule', 'tools/legacy/vml.js', '        for (j = 0; j < TAGS.length; j++) ss.addRule(', '        for (j = 0; j < 0; j++) ss.addRule(', LEG6, 'FAIL no VML node made that IE 6 would not draw'),
  ('vml-svg-group', 'tools/legacy/vml.js', '      el = doc.createElement("span");\n      st = { kind: tag === "svg" ? 0 : 1', '      el = tag === "svg" ? make("group") : doc.createElement("span");\n      st = { kind: tag === "svg" ? 0 : 1', LEG6, 'FAIL no VML node made that IE 6 would not draw'),
  ('vml-svg-no-clip', 'tools/legacy/vml.js', '    s.overflow = "hidden";\n', '', LEG6, 'FAIL the svg is a span of its own size that clips'),
  ('vml-stroke-round-join', 'tools/legacy/vml.js', 'st.vs.joinstyle = "miter";', 'st.vs.joinstyle = "round";', LEG6, 'FAIL <path> as a v:shape'),
  ('vml-svg-plain-element', 'tools/legacy/dom-old.js', ' : w.cdomVml.create(tag);', ' : d.createElement(tag);', LEG6, 'FAIL the svg is a span of its own size'),
  ('vml-viewbox-ignored', 'tools/legacy/vml.js', '    if (vb.length !== 4 || vb[2] <= 0 || vb[3] <= 0) return [1, 1, 0, 0];', '    return [1, 1, 0, 0];', LEG6, 'FAIL <path> as a v:shape'),
  ('vml-stroke-weight-unscaled', 'tools/legacy/vml.js', '    var weight = num(paint["stroke-width"], 1) * Math.sqrt(m[0] * m[1]);', '    var weight = num(paint["stroke-width"], 1);', LEG6, 'FAIL <path> as a v:shape'),
  ('vml-g-transform-ignored', 'tools/legacy/vml.js', '    } else if (a.transform && typeof st.tm !== "string") {', '    } else if (false) {', LEG6, 'FAIL <path in a g> as a v:shape'),
  ('vml-paint-not-inherited', 'tools/legacy/vml.js', '    for (k in ctx.paint) {', '    for (k in {}) {', LEG6, 'FAIL <path in a g> as a v:shape'),
  ('vml-opacity-not-multiplied', 'tools/legacy/vml.js', '    return { m: m, paint: paint, opacity: ctx.opacity * num(a.opacity, 1) };', '    return { m: m, paint: paint, opacity: ctx.opacity };', LEG6, 'FAIL <polygon> as a v:shape'),
  ('vml-short-hex-kept', 'tools/legacy/vml.js', 'return "#" + v.charAt(1) + v.charAt(1) + v.charAt(2) + v.charAt(2) + v.charAt(3) + v.charAt(3);', 'return v;', LEG6, 'FAIL <circle> as a v:shape'),
  ('vml-rgb-refused', 'tools/legacy/vml.js', '    if (/^rgb\\(/.test(v)) {', '    if (false) {', LEG6, 'FAIL <rect> as a v:shape'),
  ('vml-line-filled', 'tools/legacy/vml.js', '    var fill = st.tag === "line" ? "" : paintOf(', '    var fill = paintOf(', LEG6, 'FAIL <line> as a v:shape'),
  ('vml-circle-no-ry', 'tools/legacy/vml.js', '      ry = t === "circle" ? rx : num(a.ry, 0);', '      ry = num(a.ry, 0);', LEG6, 'FAIL <circle> as a v:shape'),
  ('vml-polygon-open', 'tools/legacy/vml.js', '    return t === "polygon" ? s + "Z" : s;', '    return s;', LEG6, 'FAIL <polygon> as a v:shape'),
  ('vml-unsupported-shown', 'tools/legacy/vml.js', '    el = doc.createElement("span");\n    el.style.display = "none";\n', '    el = doc.createElement("span");\n', LEG6, 'FAIL text and the gradient are hidden spans'),
  ('vml-rotate-drawn', 'tools/legacy/vml.js', '      st.hide.transform = typeof t === "string";', '      st.hide.transform = false;', LEG6, 'FAIL text and the gradient are hidden spans, the rotated rect is hidden'),
  ('vml-report-silent', 'tools/legacy/vml.js', '    if (typeof w.reportError === "function") w.reportError(e);', '', LEG6, 'FAIL each reported once through window.onerror'),
  ('vml-report-every-time', 'tools/legacy/vml.js', '    if (seen.hasOwnProperty(kind)) return;\n', '', LEG6, 'nothing is reported twice'),
  ('vml-path-kept-on-change', 'tools/legacy/vml.js', '    if (key !== st.key) {', '    if (!st.key) {', LEG6, 'FAIL cd_set_attr of d reached the v:shape'),
  ('vml-context-not-renewed', 'tools/legacy/vml.js', '    inner = st.inner = within(el,', '    inner = within(el,', LEG6, "FAIL the g's transform changed while its path was detached"),
  ('vml-set-never', 'tools/legacy/vml.js', '    if (fill !== st.f) {', '    if (fill !== fill) {', LEG6, 'FAIL <path> as a v:shape'),
  ('vml-colour-one-key', 'tools/legacy/vml.js', '    var c = COLOURS["k" + s];\n    if (c === void 0) c = COLOURS["k" + s] = colourOf(s);', '    var c = COLOURS.k;\n    if (c === void 0) c = COLOURS.k = colourOf(s);', LEG6, 'FAIL <path> as a v:shape'),
  ('vml-place-not-redrawn', 'tools/legacy/shim-old.js', '  if (node.$vml) {\n    cdomVml.placed(node);\n  }\n', '', LEG6, "FAIL the g's transform changed while its path was detached"),
  ('vml-held-never-drawn', 'tools/legacy/shim-old.js', '  cdomVml.drawHeld();\n', '', LEG6, 'FAIL <path> as a v:shape'),
  ('vml-shim-never-draws-held', 'shim/cdom.js', '    store32(tmpCount, 0);\n    drawHeld();\n', '    store32(tmpCount, 0);\n', LEG6, 'FAIL <path> as a v:shape'),
  ('vml-other-tiers-not-svg', 'shim/cdom.js', '      push(doc.createElementNS(SVG, name));', '      push(doc.createElement(name));', LEG9, 'FAIL the svg is real SVG'),
  ('vml-text-top-not-baseline', 'tools/legacy/vml.js', ' - ascent(style, weight, family) * px / 100;', ';', LEG6, 'FAIL <text> as a span in its font'),
  ('vml-text-size-unscaled', 'tools/legacy/vml.js', 'var px = num(paint["font-size"], 16) * m[1]', 'var px = num(paint["font-size"], 16)', LEG6, 'FAIL <text> as a span in its font'),
  ('vml-text-no-serif-fallback', 'tools/legacy/vml.js', '    if (!/(^|,)\\s*(serif|sans-serif|monospace|cursive|fantasy)\\s*$/.test(family)) family += ", serif";\n', '', LEG6, 'FAIL <text> as a span in its font'),
  ('vml-current-color-unresolved', 'tools/legacy/vml.js', '      v = cs && cs.color ? cs.color : v;\n', '', LEG6, 'FAIL a currentColor fill is the CSS color'),
  ('vml-nested-svg-at-origin', 'tools/legacy/vml.js', '      s.left = Math.round(m[0] * num(a.x, 0) + m[2]) + "px";', '      s.left = Math.round(m[2]) + "px";', LEG6, 'FAIL the svg in the svg is a span'),
  ('vml-slice-as-meet', 'tools/legacy/vml.js', '    s = /slice/.test(par) ? Math.max(sx, sy) : Math.min(sx, sy);', '    s = Math.min(sx, sy);', LEG6, 'FAIL its rect drawn from its corner'),
  ('vml-em-not-remeasured', 'tools/legacy/vml.js', '        if (list[j].offsetWidth > 0) refresh(list[j]);', '', LEG6, 'FAIL and drawn at its scale'),
  ('vml-em-as-px', 'tools/legacy/vml.js', '    if (v === void 0 || /^\\s*[+\\-]?(\\d+\\.?\\d*|\\.\\d+)(px)?\\s*$/.test(v)) return Math.round(num(v, dflt)) + "px";\n    return v;', '    return Math.round(num(v, dflt)) + "px";', LEG6, 'FAIL the svg in em is laid out by CSS'),
  ('ie6-old-dom-gate-off', 'tools/legacy/es3.mjs', "    if (dot && ((called.has(k.value) && call) || props.has(k.value))) left.add('.' + k.value);", '', ES3T, 'FAIL a call, a property and a global left are found'),
  ('ie6-old-dom-call-not-rewritten', 'tools/legacy/es3.mjs', "        if (m && (own(OLD_DOM.call, m) || (own(OLD_DOM.zero, m) && !p.node.arguments.length))) {", "        if (false) {", ES3T, 'W3C DOM the old DOM lacks left in the shim'),
  ('ie6-invoke-through-apply', 'tools/legacy/shim-old.js', '    case 0: return object[key]();', '    case 0: return object[key].apply(object, a);', LEG6, 'FAIL focus delivered once'),
  ('ie6-no-reach', 'tools/legacy/shim-old.js', '    cdomOld.reach(node, EVENTS[type], delegatedListener[type]);', '', LEG6, "FAIL the broken image's error"),
  ('ie6-splice-unfixed', 'tools/legacy/polyfills-es3.js', '  if (probe.length) {', '  if (false) {', LEG6, 'FAIL polyfills agree with the built-ins'),
  ('ie6-indexof-negative-from', 'tools/legacy/polyfills-es3.js', '    if (i < 0) i = Math.max(n + i, 0);\n    for (; i < n; i++) {', '    for (; i < n; i++) {', LEG6, 'FAIL polyfills agree with the built-ins'),
  ('ie6-pointerup-buttons-held', 'tools/legacy/dom-old.js', '      o.buttons = type === "pointerup" ? 0 : b;', '      o.buttons = b;', LEG6, 'FAIL pointerup at 60,55, no buttons'),
  ('ie6-input-never', 'tools/legacy/dom-old.js', '        if (e.propertyName === "value" && doc.activeElement === t) f(shaped(e, t, type));', '', LEG6, 'FAIL echo of'),
  ('ie6-style-attribute-dropped', 'tools/legacy/dom-old.js', '      el.style.cssText = value;', '      el.setAttribute(n, value);', LEG6, 'FAIL one pointerdown, one pointerup, one click'),
  ('ie6-wheel-unscaled', 'tools/legacy/dom-old.js', '    if (type === "wheel") o.deltaY = -(e.wheelDelta | 0) * 5 / 6;', '    if (type === "wheel") o.deltaY = -(e.wheelDelta | 0);', LEG6, "FAIL the wheel's deltas"),
  ('ie6-rect-no-width', 'tools/legacy/dom-old.js', '      width: r.right - r.left, height: r.bottom - r.top };', '      width: 0, height: r.bottom - r.top };', LEG6, "FAIL cd_rect's doubles"),
  ('ie6-prev-element-counts-text', 'tools/legacy/dom-old.js', '    while (s && s.nodeType !== 1) s = s.previousSibling;\n', '', LEG6, 'FAIL a click on the <i> reaches the paragraph'),
  ('ie5-words-push-at-load', 'tools/legacy/words.js', 'for (i = 0; i < 2098; i++, v *= 2) t[i] = v;', 'for (i = 0; i < 2098; i++, v *= 2) t.push(v);', WORDS, 'words.js names what JScript 5.0 (IE 5.0) lacks: .push'),
  ('ie6-input-type-refused', 'tools/legacy/dom-old.js', '    if (el.mergeAttributes && el.nodeName === "INPUT" && name.toLowerCase() === "type") {', '    if (0) {', LEG6, 'FAIL nested inputs typed'),
  ('ie6-clear-keeps-children', 'tools/legacy/dom-old.js', '    while (x.firstChild) x.removeChild(x.firstChild);\n', '', FUZZ6, 'errors round'),
  ('ie9-shim-gate-lets-typed-through', 'tools/legacy/memory.mjs', "if (node.type === 'Identifier' && names.has(node.name) && !SHIM_TYPED_ALLOWED.has(fn))", "if (false)", WORDS, 'FAIL the shim-ie9 gate'),
  ('encode-quality-unchecked', 'src/cdom.c', '|| y + h > H || quality > 100) __builtin_trap();', '|| y + h > H) __builtin_trap();', UNIT, "FAIL cd_encode_image's quality"),
  # cd_job (src/cdom_job.c, src/cdom_job_mod.c, tools/job.mjs; cdom.mk's job rule and tools/cdpage's --job in tests/link and tests/page)
  ('job-no-terminate', 'shim/cdom.js', '        worker.terminate();\n        delete jobLive[token];\n        jobAnswer(token, status, output);', '        delete jobLive[token];\n        jobAnswer(token, status, output);', JOB, 'FAIL one worker, ended when it answered'),
  ('job-no-transfer', 'shim/cdom.js', 'worker.postMessage([compiled, input, args, file], file ? [args] : [input, args]);', 'worker.postMessage([compiled, input, args, file], []);', JOB, 'FAIL the input and args are transferred'),
  ('job-compile-each', 'shim/cdom.js', 'const module = jobModules[name] || (jobModules[name] = fetch(', 'const module = (jobModules[name] = fetch(', JOB, 'FAIL the module is fetched and compiled once'),
  ('job-no-blob', 'shim/cdom.js', 'URL.createObjectURL(new Blob(["(" + jobWorker + ")()"], { type: "text/javascript" }))', '"data:text/javascript," + encodeURIComponent("(" + jobWorker + ")()")', JOB, 'FAIL the worker comes from a blob: URL'),
  ('job-trap-ok', 'shim/cdom.js', '(error) => postMessage([error === 1 ? 1 : 2]));', '(error) => postMessage([0]));', JOB, 'FAIL FAILED: a job that traps'),
  ('job-room-failed', 'shim/cdom.js', '            const code = exports.run(inAt, length, argsAt, args.byteLength) >>> 0;\n            status = code > 1 ? 2 : code;', '            const code = exports.run(inAt, length, argsAt, args.byteLength) >>> 0;\n            status = code ? 2 : 0;', JOB, 'FAIL NO_ROOM: a job that cannot get its memory'),
  ('job-input-room-failed', 'shim/cdom.js', '(error) => postMessage([error === 1 ? 1 : 2]));', '(error) => postMessage([2]));', JOB, 'FAIL NO_ROOM: a 20 MB file the capped job cannot hold'),
  ('job-file-read-on-main', 'shim/cdom.js', '    if (!input) {\n      setTimeout(() => jobAnswer(token, 4));', '    if (input && input.arrayBuffer) {\n      input.arrayBuffer();\n    }\n    if (!input) {\n      setTimeout(() => jobAnswer(token, 4));', JOB, 'FAIL the file never entered the app'),
  ('job-shared-worker', 'shim/cdom.js', 'worker = new Worker(jobScript);', 'worker = globalThis.jobOne || (globalThis.jobOne = new Worker(jobScript));', JOB, 'FAIL both jobs answered'),
  ('job-no-unsupported-worker', 'shim/cdom.js', 'if (typeof Worker === "undefined" || typeof WebAssembly === "undefined") {', 'if (typeof WebAssembly === "undefined") {', JOB, 'FAIL UNSUPPORTED: no Worker'),
  ('job-no-unsupported-wasm', 'shim/cdom.js', 'if (typeof Worker === "undefined" || typeof WebAssembly === "undefined") {', 'if (typeof Worker === "undefined") {', JOB, 'FAIL UNSUPPORTED: no WebAssembly'),
  ('job-nofile-status', 'shim/cdom.js', '      setTimeout(() => jobAnswer(token, 4));', '      setTimeout(() => jobAnswer(token, 2));', JOB, 'FAIL NO_FILE'),
  ('job-unsupported-sync', 'shim/cdom.js', '      setTimeout(() => jobAnswer(token, 3));', '      jobAnswer(token, 3);', JOB, 'FAIL UNSUPPORTED is answered from the event loop too'),
  ('job-legacy-no-unsupported', 'shim/cdom.js', 'if (typeof Worker === "undefined" || typeof WebAssembly === "undefined") {', 'if (typeof Worker === "undefined") {', LEG, 'FAIL the old tiers (no WebAssembly): cd_job_cb answers UNSUPPORTED'),
  ('job-answer-leaks', 'src/cdom_job.c', '  cd_flush();\n  cd_free((void *)p);', '  cd_flush();', JOB, 'FAIL the answer\'s bytes are freed after the callback'),
  ('job-room-limit', 'src/cdom_job_mod.c', 'return cd_alloc_try(n ? n : 1);', 'return cd_alloc(n ? n : 1);', JOB, 'FAIL NO_ROOM: a 20 MB file the capped job cannot hold'),
  ('job-heap-whole', 'src/cdom.c', '#ifndef CDOM_HEAP_ONLY\n', '#ifndef CDOM_HEAP_ONLY_X\n', LINK, 'undefined symbol: cd_main'),
  ('job-mod-dom-import', 'src/cdom_job_mod.c', '__attribute__((export_name("job_room"))) void *job_room(uint32_t n) { return cd_alloc_try(n ? n : 1); }', '__attribute__((import_module("cdom"), import_name("now"))) double j_now(void);\n__attribute__((export_name("job_room"))) void *job_room(uint32_t n) { return j_now() > 0 ? cd_alloc_try(n ? n : 1) : 0; }', LINK, 'FAIL a job module\'s imports are not cdjob.answer alone'),
  ('job-rule-no-heap-only', 'cdom.mk', '$(CDOM_CFLAGS) -DCDOM_HEAP_ONLY $(APP_JOB_CFLAGS)', '$(CDOM_CFLAGS) $(APP_JOB_CFLAGS)', LINK, 'FAIL cdom.mk\'s job rule does not build a job module'),
  ('job-rule-any-name', 'cdom.mk', '$$(call cdom_mk_for,$$(cdom_mk_job_files),$$@,$$(cdom_mk_job_src) $$(cdom_mk_job_objs) $$(CDOM_HEADERS))', '$$(cdom_mk_job_src) $$(cdom_mk_job_objs) $$(CDOM_HEADERS)', LINK, 'FAIL cdom.mk built a .job.wasm that APP_JOBS does not name'),
  ('job-page-always-bundled', 'cdom.mk', 'cdom_mk_job_args = $(if $(APP_WASM_SEPARATE),,$(foreach', 'cdom_mk_job_args = $(if x,$(foreach', LINK, 'FAIL a page with APP_WASM_SEPARATE carries the job module'),
  ('job-page-no-jobs', 'cdom.mk', ',$(cdom_mk_job_args) $(cdom_mk_noscript_args))', ',$(cdom_mk_noscript_args))', LINK, 'FAIL a bundled page does not carry its job module'),
  ('job-page-name', 'tools/cdpage.c', 'bstr(o, "var cdomJobs={");', 'bstr(o, "var cdomJob={");', PAGE, 'FAIL two job modules bundled'),
  ('job-page-no-comma', 'tools/cdpage.c', '          if (j) bbyte(o, \',\');\n', '', PAGE, 'FAIL two job modules bundled'),
  ('job-page-name-raw', 'tools/cdpage.c', '          js_string(o, name);\n', '          bbyte(o, \'"\');\n          bstr(o, name);\n          bbyte(o, \'"\');\n', PAGE, 'FAIL two job modules bundled'),
  ('job-page-no-eq-check', 'tools/cdpage.c', 'if (!eq || eq == a->jobs[j]) die(', 'if (eq == a->jobs[j]) die(', PAGE, 'FAIL refuses jobname'),
  ('job-page-empty-ok', 'tools/cdpage.c', 'if (!slurp(eq + 1, &job) || !job.n) die(', 'if (!slurp(eq + 1, &job)) die(', PAGE, 'FAIL refuses jobempty'),
  # The chunked read (docs/chunked-file-read-design.md, section 8's mutation list, and the build's own parts).
  # The prototype's eight, as the build has them:
  ('chunk-skip-dropped', 'src/cdom_image_jpeg.c', '    x->skip = (uint32_t)(past < left ? past : left);\n', '    (void)past; (void)left;\n', CHK, 'e-com.jpg at 300x300, pieces of'),
  ('chunk-skip-one-long', 'src/cdom_image_jpeg.c', '    x->skip = (uint32_t)(past < left ? past : left);\n', '    x->skip = (uint32_t)(past < left ? past + 1 : left);\n', CHK, 'jpeg-exif1 at 300x200, pieces of 1: status CORRUPT, whole OK'),
  ('chunk-keep-byte-lost', 'src/cdom_image_jpeg.c', '  d->used = keep;\n', '  d->used = keep > 1 ? keep - 1 : keep;\n', CHK, 'pieces of 1:'),
  ('chunk-png-crc-3', 'src/cdom_image_png.c', '      x->at += 4;\n      x->state = W_HEAD;', '      x->at += x->len ? 4 : 3;\n      x->state = W_HEAD;', CHK, 'e-zero.png: differs from e-plain.png'),
  ('chunk-png-past-file', 'src/cdom_image_png.c', '      if (x->at - 8 + 12 + (uint64_t)l > size) return data_end(d, x);   /* a chunk that runs past the file */\n', '', CHK, 'makes no progress'),
  ('chunk-png-plte-short', 'src/cdom_image_png.c', "uint32_t lim = x->kind == K_PLTE ? 768u : 256u", "uint32_t lim = x->kind == K_PLTE ? 767u : 256u", CHK, 'png-palette8'),
  ('chunk-short-read-glue', 'src/cdom_image_file.c', '  if (st != CD_FILE_OK || !n) { finish(j, CD_IMAGE_UNREADABLE); return; }', '  if (st != CD_FILE_OK) { finish(j, CD_IMAGE_UNREADABLE); return; }', CHK, 'after a 0-byte answer'),
  ('chunk-short-read-shim', 'shim/cdom.js', '    const status = rd.got ? 0 : rd.failed || !done ? 3 : 0;', '    const status = rd.got ? 0 : rd.failed ? 3 : 0;', FILE, 'FAIL a file replaced by a shorter one mid-read'),
  # in the shim:
  ('chunk-piece-again', 'shim/cdom.js', '    rd.max = max >>> 0;\n    rd.got = 0;\n', '    rd.max = max >>> 0;\n', FILE, 'end to end equal to the file'),
  ('chunk-split-byte-dropped', 'shim/cdom.js', '      rd.off += k;\n      rd.got += k;\n      rd.pos += k;', '      rd.off += k + (rd.off + k < rd.chunk.length && rd.got + k === rd.max ? 1 : 0);\n      rd.got += k;\n      rd.pos += k;', FILE, 'end to end equal to the file'),
  ('chunk-close-keeps-slot', 'src/cdom_file.c', '  if (token) cd_cb_drop(token);', '  (void)token;', FILE, 'FAIL cd_file_close with a piece pending frees its callback slot'),
  ('chunk-answer-microtask', 'shim/cdom.js', '    postTask(answer);\n', '    Promise.resolve().then(answer);\n', FILE, 'each answer is a task of its own'),
  # in C:
  ('chunk-room-one-more', 'src/cdom_image.c', '  return m < left ? m : left;', '  return m < left ? m : left + 1;', CHK, 'past a piece or the file'),
  ('chunk-walker-not-fed', 'src/cdom_image_jpeg.c', '  uint32_t st = walk_feed(d, p, n, (uint64_t)d->taken - n);   /* every byte through the walker first */', '  uint32_t st = d->taken == n ? walk_feed(d, p, n, 0) : CD_IMAGE_MORE;', CHK, 'pieces of 1:'),
  ('chunk-put-unguarded', 'src/cdom_image_jpeg.c', '  int threw = cd_image_guard(body, x, &x->guard);', '  int threw = (body(x), 0);', CHK, 'AddressSanitizer'),
  ('chunk-end-no-destroy', 'src/cdom_image_jpeg.c', '  jpeg_destroy_decompress(&x->cinfo);\n  cd_free(x->row);', '  cd_free(x->row);', CHK, 'left allocated'),
  # and the build's own:
  ('chunk-inflate-tail-lost', 'src/cdom_image_inflate.c', '    if (r <= k) { p -= r; n += r; z->carry_n = 0; }', '    if (r <= k) { p -= r ? r - 1 : 0; n += r ? r - 1 : 0; z->carry_n = 0; }', CHK, 'FAIL inflate_diff'),
  ('chunk-inflate-no-look', 'src/cdom_image_inflate.c', 'static inline int may_read(const bits *s) { return s->last || ahead(s) >= CD_INFLATE_LOOK; }', 'static inline int may_read(const bits *s) { return s->last || ahead(s) >= 1; }', CHK, '1-byte pieces'),
  ('chunk-inflate-carry-kept', 'src/cdom_image_inflate.c', '  z->carry_n = n - used;\n  if (z->carry_n) memcpy(z->carry, p + used, z->carry_n);', '  z->carry_n = 0;', CHK, 'FAIL inflate_diff'),
  ('chunk-steps-uncut', 'src/cdom_image_jpeg.c', '++x->since == 64', '++x->since == (1u << 30)', CHK, 'steps with no bytes wanted'),
  ('chunk-exif-head-counted', 'src/cdom_image_jpeg.c', '        w->exif_n = 6;                             /* "Exif', '        w->exif_n = 0;                             /* "Exif', CHK, 'runtime error'),
  ('chunk-glue-stop-keeps-reader', 'src/cdom_image_file.c', '  if (j->reader) cd_file_close(j->reader);\n  cd_image_end(j->d);', '  cd_image_end(j->d);', CHK, 'readers'),
  ('chunk-job-stop-no-drop', 'src/cdom_job.c', '  cd_cb_drop(token);\n  j_job_stop(token);', '  j_job_stop(token);', IMGW, 'cd_job_stop'),
  ('chunk-job-stop-no-terminate', 'shim/cdom.js', '      if (job.worker) {\n        job.worker.terminate();\n      }', '', IMGW, 'cd_job_stop'),
  ('chunk-job-pieces-past-max', 'shim/cdom.js', '            const k = Math.min(chunk.length - off, max - got, size - at);', '            const k = Math.min(chunk.length - off, max - got + 1, size - at);', IMGW, 'cd_job_pieces_cb: every case'),
  # the legacy build's float to integer rewrite (tools/legacy/build.mjs, wasm2js; tests/fptoint.c, tools/fptoint.mjs)
  ('fptoint-not-rewritten', 'tools/legacy/build.mjs', "if (/\\bi64\\.trunc_sat_f(32|64)_[su]\\b/.test(text)) {", 'if (false) {', FPT1, 'Command failed: wasm2js'),
  ('fptoint-f32-not-rewritten', 'tools/legacy/build.mjs', "/\\bi64\\.trunc_sat_(f(?:32|64)_[su])\\b/g", "/\\bi64\\.trunc_sat_(f(?:64)_[su])\\b/g", FPT9, 'Command failed: wasm2js'),
  ('fptoint-binaryen-lowering', 'tools/legacy/build.mjs', "js = execFileSync('wasm2js', ['-all', '-O2', input]",
   "execFileSync('wasm-opt', ['-all', '--llvm-nontrapping-fptoint-lowering', wasm, '-o', path.join(dir, 'l.wasm')]); js = execFileSync('wasm2js', ['-all', '-O2', path.join(dir, 'l.wasm')]", FPT1, 'FAIL 44 values through u64, i64, u32 and i32: in range exact'),
  # the network on the tiers with no fetch (tools/legacy/polyfills.js; tests/legacy.c net_ask, tools/legacy.mjs netRun)
  ('net-no-fetch', 'tools/legacy/polyfills.js', 'if (typeof w.fetch !== "function") (function () {', 'if (false) (function () {', LEG1, 'fetch is not defined'),
  ('net-no-activex', 'tools/legacy/polyfills.js', 'try { return new w.ActiveXObject("Microsoft.XMLHTTP"); } catch (e) {}', 'return null;', LEG6, 'FAIL on this page: network'),
  ('net-arraybuffer-unset', 'tools/legacy/polyfills.js', 'if (typed) xhr.responseType = "arraybuffer";', 'if (typed) xhr.responseType = "";', LEG1, 'FAIL on this page: network'),
  ('net-mime-high-bits', 'tools/legacy/polyfills.js', 'out[i] = mime ? c & 0xff : c < 256', 'out[i] = mime ? c & 0x7f : c < 256', LEG9, 'FAIL ?mime: network'),
  ('net-cp1252-unmapped', 'tools/legacy/polyfills.js', 'c < 256 ? c : back["c" + c] || 63;', 'c < 256 ? c : 63;', LEG9, 'FAIL on this page: network'),
  ('net-charset-ignored', 'tools/legacy/polyfills.js', 'data = mime || charset === "windows-1252" || charset === "iso-8859-1" ?', 'data = mime ?', LEG6, 'FAIL on this page: network'),
  ('net-abort-ignored', 'tools/legacy/polyfills.js', 'signal.addEventListener("abort", function () {', 'if (false) signal.addEventListener("abort", function () {', LEG9, 'FAIL on this page: network'),
  ('net-body-not-checked', 'tools/legacy/polyfills.js', 'return new w.TextEncoder().encode(s).length === bytes.length ? s : null;', 'return s;', LEG6, 'FAIL on this page: network'),
  ('net-no-trim', 'tools/legacy/polyfills.js', 'if (!String.prototype.trim) {\n    String.prototype.trim', 'if (false) {\n    String.prototype.trim', LEG9, 'trim is not a function'),
  # the no-script fallback (docs/noscript-design.md; tools/cdpage.c's block, wrappers and parse check, tools/noscript.mjs, tools/noscript_gif.py, cdom.mk)
  ('ns-no-hider', 'tools/cdpage.c', 'bstr(o, "<script><!--\\ndocument.write(\\"<!-\\"+\\"-\\");\\n//--></script><noscript>\\n");', 'bstr(o, "<noscript>\\n");', NOSCRIPT, 'FAIL script on: the fallback is in the comment'),
  ('ns-no-noscript-element', 'tools/cdpage.c', '//--></script><noscript>\\n");', '//--></script>\\n");', NOSCRIPT, 'Chrome with script requested'),
  ('ns-comment-never-ends', 'tools/cdpage.c', 'bstr(o, "</noscript><wbr -->\\n");', 'bstr(o, "</noscript><wbr>\\n");', NOSCRIPT, 'FAIL script on: the app drew'),
  ('ns-no-wrap-script', 'tools/cdpage.c', '    if (blank(b, bn)) continue;', '    if (blank(b, bn) || !e.style) continue;', NOSCRIPT, 'FAIL the HTML 2.0 lens'),
  ('ns-no-wrap-style', 'tools/cdpage.c', '    if (blank(b, bn)) continue;', '    if (blank(b, bn) || e.style) continue;', NOSCRIPT, 'FAIL the HTML 2.0 lens'),
  ('ns-text-changed', 'tools/cdpage.c', '"Here\'s a snowman";', '"Here is a snowman";', NOSCRIPT, "FAIL script off: the page's text"),
  ('ns-no-alt', 'tools/cdpage.c', 'img_tag(o, &snow, "[A snowman]");', 'img_tag(o, &snow, "");', NOSCRIPT, 'FAIL lynx -dump shows other text'),
  ('ns-marker-kept', 'tools/cdpage.c', '      if (a->noscript_shot) noscript_block(o, a->noscript_shot, a->noscript_snowman);', '      if (a->noscript_shot) noscript_block(o, a->noscript_shot, a->noscript_snowman);\n      else bput(o, l, n), bbyte(o, \'\\n\');', NOSCRIPT, 'FAIL CDOM_NOSCRIPT=0'),
  ('ns-escape-unchecked', 'tools/cdpage.c', '  return st >= DESC ? from : SIZE_MAX;', '  return (void)st, (void)from, SIZE_MAX;', NOSCRIPT, 'planted sequence), no fallback: taken'),
  ('ns-escape-dd-stays', 'tools/cdpage.c', "      if (c == '>' && st == DESC_DD) { st = DATA; break; }\n", '', NOSCRIPT, 'FAIL the parse check and Chrome disagree'),
  ('ns-escape-no-delim', 'tools/cdpage.c', 'if (c == \'<\' && starts_ci(p + i + 1, n - i - 1, "script") && i + 7 < n && delim(p[i + 7])) { st = DESC;', 'if (c == \'<\' && starts_ci(p + i + 1, n - i - 1, "script") && i + 7 < n) { st = DESC;', NOSCRIPT, 'FAIL the parse check and Chrome disagree'),
  ('ns-arrow-unchecked', 'tools/cdpage.c', '    if (end && (!had || !blank(end + 3, bn - (size_t)(end + 3 - b))))', '    if (0 && end && (!had || !blank(end + 3, bn - (size_t)(end + 3 - b))))', NOSCRIPT, 'it would end the hiding comment): taken'),
  ('ns-module-wrapped', 'tools/cdpage.c', '      if (ty && find(ty, (size_t)(p + e.open_end - ty), "module"))', '      if (ty && 0)', NOSCRIPT, 'FAIL a module script under the fallback'),
  ('ns-default-off', 'cdom.mk', 'CDOM_NOSCRIPT ?= 1', 'CDOM_NOSCRIPT ?= 0', NOSCRIPT, 'FAIL no GIFs beside the page'),
  ('ns-shot-dpr2', 'tools/noscript.mjs', 'deviceScaleFactor: 1, mobile: false', 'deviceScaleFactor: 2, mobile: false', NOSCRIPT, 'FAIL the GIFs'),
  ('ns-no-glyph', 'tools/noscript.mjs', 'color:#000">\\u2603</div>', 'color:#000"> </div>', NOSCRIPT, 'FAIL the GIFs'),
  ('ns-animations-run', 'tools/noscript.mjs', "  await b.send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'reduce' }, { name: 'prefers-color-scheme', value: 'light' }] });\n  await b.send('Animation.enable');\n  await b.send('Animation.setPlaybackRate', { playbackRate: 0 });\n", '', NOSCRIPT, 'FAIL determinism'),
  ('ns-stamp-no-page', 'tools/noscript.mjs', "  `chrome ${chromeId}`, `python ${pv.stdout.trim()}`, `page ${sha(html)}`];", "  `chrome ${chromeId}`, `python ${pv.stdout.trim()}`];", NOSCRIPT, 'FAIL the content stamp: the app drew another word'),
  ('ns-stamp-never-matches', 'tools/noscript.mjs', "  if (old.join('\\n') === [...head, ...files, ...outs()].join('\\n')) process.exit(0); // up to date: no Chrome", "  if (false) process.exit(0);", NOSCRIPT, 'FAIL the content stamp: a page of the same bytes'),
  ('ns-gif-random', 'tools/noscript_gif.py', "        order = [c for _, c in sorted(colours, key=lambda nc: (-nc[0], nc[1]))]\n", "        order = [c for _, c in sorted(colours, key=lambda nc: (-nc[0], nc[1]))]\n        import random\n        random.shuffle(order)\n", NOSCRIPT, 'FAIL determinism'),
  ('ns-gif89a', 'tools/noscript_gif.py', "    out.save(dst, 'GIF', optimize=False, interlace=False)\n    with open(dst, 'rb') as f:\n        head = f.read(6)\n    if head != b'GIF87a':", "    out.save(dst, 'GIF', optimize=False, interlace=False, comment=b'cdom')\n    with open(dst, 'rb') as f:\n        head = f.read(6)\n    if False:", NOSCRIPT, 'FAIL the GIFs'),
  # demo/uttt, the replay viewer (tools/uttt.mjs; FOOLISH=/abs/path/to/foolish python3 tools/mutate.py uttt-...)
  ('uttt-forward-skips', 'demo/uttt/uttt_replay.c', '  else if (id == b_fwd && shown < game.n_plies) shown++;', '  else if (id == b_fwd && shown < game.n_plies) shown += 2;', UTTT, 'FAIL webassembly: > 1 times: the board is the game after 1 plies'),
  ('uttt-won-block-not-shown', 'demo/uttt/uttt_replay.c', '  if (won == UTTT_X || won == UTTT_O) {', '  if (0) {', UTTT, '  big marks: shown'),
  ('uttt-forced-not-lit', 'demo/uttt/uttt_replay.c', '  if ((legal >> b) & 1u) bg = "background-color:#ffff66";', '  if (0) bg = "background-color:#ffff66";', UTTT, '  backgrounds: shown'),
  ('uttt-play-no-stop', 'demo/uttt/uttt_replay.c', '  if (shown >= game.n_plies) set_playing(0);\n  else cd_timer_set(&timer, TICK_MS, tick, nullptr);', '  cd_timer_set(&timer, TICK_MS, tick, nullptr);', UTTT, 'FAIL webassembly: Play stops at the end'),
  ('uttt-last-not-bold', 'demo/uttt/uttt_replay.c', '        if (i == last) cd_open("b");\n        cd_text(MARK[m]);\n        if (i == last) cd_close();', '        cd_text(MARK[m]);', UTTT, '  bold: shown'),
  ('uttt-bad-code-silent', 'demo/uttt/uttt_replay.c', '    cd_set_text_node(err_txt, "That is not a game code.', '    cd_set_text_node(err_txt, "', UTTT, 'FAIL webassembly: a bad code shows the error line'),
  ('uttt-query-ignored', 'demo/uttt/uttt_replay.c', '  if (n > 1 && n < sizeof buf - 1) {', '  if (0) {', UTTT, 'FAIL webassembly: the query: the board is the game after 0 plies'),
]

def build_and_run(root, cmd, minify=False):
  # In a worker's copy, a mutant of shim/cdom.js is a mutant of the minified shim too (tests/build.sh fails
  # when they disagree); the real tree's restored runs check the committed one as it is.
  if minify:
    m = subprocess.run(['node', 'tools/minshim.mjs'], cwd=root, capture_output=True, text=True)
    if m.returncode: return 1, 'MINSHIM FAILED ' + m.stderr[-400:]
  b = subprocess.run(['tests/build.sh'], cwd=root, capture_output=True, text=True)
  if b.returncode: return 1, 'BUILD FAILED ' + b.stderr[-400:]
  # A mutant can make C loop forever (a cycle in a list it walks): the page then never answers, and
  # the run would wait on it for good. The test runs in a process group of its own (Chrome with it),
  # killed whole past TEST_TIMEOUT. That counts as caught only for an entry marked HANGS (the worker decides):
  # for any other a timeout is a test that never finished, so it says nothing about the mutant.
  t = subprocess.Popen(cmd, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
  try:
    out, _ = t.communicate(timeout=TEST_TIMEOUT)
  except subprocess.TimeoutExpired:
    os.killpg(t.pid, signal.SIGKILL)
    out, _ = t.communicate()
    return HUNG, f'HUNG: killed after {TEST_TIMEOUT} s\n' + (out or '')
  return t.returncode, out

# A mutant's test may write into the tree it runs in (mk-any-wasm makes cdom.mk's page rule apply to every
# .html, so make rebuilds shim/page.html and tests/link/page/marker.html in place). Restoring only the
# mutated file left those for every later mutant of that worker, whose pages then failed for that
# reason, not their own (seven link mutants missed). So after each mutant every tracked file goes back
# to the copy's index, which holds the real tree's files; the generated minified shim is among them.
# Files a test created outside the ignored build directories (tests/out and the rest of .gitignore stay:
# every build writes them afresh) go too, so no mutant reads what an earlier one left; the node_modules
# link copy_tree made is kept.
def restore_tree(root):
  changed = subprocess.run(['git', 'diff', '--name-only'], cwd=root, capture_output=True, text=True, check=True).stdout.split()
  if changed: subprocess.run(['git', 'checkout', '--', *changed], cwd=root, check=True)
  clean = ['git', 'clean', '-d', '-e', 'node_modules']
  made = [l.removeprefix('Would remove ') for l in subprocess.run(clean + ['-n'], cwd=root, capture_output=True, text=True, check=True).stdout.splitlines()]
  if made: subprocess.run(clean + ['-f', '-q'], cwd=root, check=True)
  return [f for f in changed if f != 'shim/cdom.min.js'] + [f + ' (new)' for f in made] # the min shim is rebuilt by every shim mutant: not news

def copy_tree(dst):
  files = subprocess.run(['git', 'ls-files', '-z'], cwd=top, capture_output=True, text=True, check=True).stdout.split('\0')
  for f in filter(None, files):
    src = top / f
    if not src.exists() and not src.is_symlink(): continue
    (dst / f).parent.mkdir(parents=True, exist_ok=True)
    if src.is_symlink(): os.symlink(os.readlink(src), dst / f) # a link as git tracks it (tests/link/apps/css/src/linked)
    else: shutil.copy2(src, dst / f)
  # A repo of its own, tracking what the real tree tracks, for the gates that read git ls-files (tools/edges.mjs).
  subprocess.run(['git', 'init', '-q'], cwd=dst, check=True)
  subprocess.run(['git', 'add', '-A'], cwd=dst, check=True)
  if (top / 'node_modules').exists(): os.symlink(top / 'node_modules', dst / 'node_modules')

args = sys.argv[1:]
jobs = max(1, (os.cpu_count() or 2) // 2)
if args[:1] == ['-j']: jobs, args = int(args[1]), args[2:]
only = set(args)
unknown = sorted(only - {m[0] for m in M})
if unknown: sys.exit('mutate.py: no mutant named ' + ', '.join(unknown)) # else a mistyped list runs nothing and passes
todo = [m for m in M if not only or m[0] in only]
# A mutant whose text is gone (the file moved on) would test nothing: stop before running any.
dead = [f'{m[0]}: the text to mutate is in {m[1]} {(top / m[1]).read_text().count(m[2])} times, not once' for m in todo
        if (top / m[1]).read_text().count(m[2]) != 1]
if dead: sys.exit('mutate.py: FAIL dead mutants\n  ' + '\n  '.join(dead))
results = {}
lock = threading.Lock()
work = queue.Queue()
for k, m in enumerate(todo): work.put((k, m))
base = pathlib.Path(tempfile.mkdtemp(prefix='cdom-mutants-'))
missed_dir = pathlib.Path(tempfile.mkdtemp(prefix='cdom-missed-'))

def worker(w):
  root = base / f'w{w}'
  copy_tree(root)
  while True:
    try: k, (name, f, old, new, cmd, want, *opts) = work.get_nowait()
    except queue.Empty: return
    hangs = HANGS in opts
    p = root / f
    src = p.read_text()
    if src.count(old) != 1:
      line, ok = f'FAIL {name}: the text to mutate is not in {f} once', False
    else:
      try:
        p.write_text(src.replace(old, new))
        code, out = build_and_run(root, cmd, minify=True)
      finally:
        p.write_text(src)
        touched = restore_tree(root)
      ok = hangs if code == HUNG else code != 0 and want in out
      why = '' if ok else f' (timed out after {TEST_TIMEOUT} s, and the entry is not marked HANGS)' if code == HUNG \
        else f' (exit {code}; ' + ('the FAIL text is there' if want in out else 'no ' + repr(want)) + ')'
      if not ok: # the whole output, kept past the run, since the tail shown here may not hold the cause
        (missed_dir / f'{name}.log').write_text(out)
        why += f' full output: {missed_dir / (name + ".log")}'
      if touched: # a mutant whose test rewrote tracked files: they are back, so the next mutant runs on the real tree
        why += ' (its test changed ' + ', '.join(touched) + '; restored)'
      line = ('caught (HUNG, killed) ' if code == HUNG and ok else 'caught ' if ok else 'MISSED ') + name + why + ('' if ok else '\n  ' + out[-600:].replace('\n', '\n  '))
    with lock:
      results[k] = ok
      print(line, flush=True)

try:
  threads = [threading.Thread(target=worker, args=(w,)) for w in range(min(jobs, len(todo)) or 1)]
  for t in threads: t.start()
  for t in threads: t.join()
finally:
  shutil.rmtree(base, ignore_errors=True)
bad = sum(1 for ok in results.values() if not ok)
print(f'{len(results) - bad} of {len(todo)} caught', flush=True)
code = 0
for label, cmd in (('unit', UNIT), ('host', HOST), ('canvas', CANVAS)):
  c, out = build_and_run(top, cmd)
  print(f'restored: {label} ' + ('ok' if c == 0 else 'FAIL\n  ' + out[-1500:].replace('\n', '\n  ')), flush=True)
  code |= c
sys.exit(1 if bad or code else 0)
