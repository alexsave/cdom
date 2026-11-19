#!/usr/bin/env python3
"""Mutation check of the port-fix tests: each mutant breaks one thing, the test written for it must
fail for that reason (its message is matched), and the source is restored after every mutant, even
on an error. Mutants run in parallel, each worker in a private copy of the tracked tree (so the real
tree is never mutated, and an interrupted run leaves nothing behind); the test drivers serve on free
ports and Chrome takes a free debugging port, so the workers never collide.
Usage: python3 tools/mutate.py [-j N] [name ...]   (N workers, default half the cores)"""
import subprocess, sys, pathlib, os, shutil, tempfile, threading, queue, signal

top = pathlib.Path(__file__).resolve().parent.parent
TEST_TIMEOUT = 150 # s: unit.mjs takes about 10 on a passing tree and waits at most 60 for a broken page
HUNG = -999
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
JOB = ['node', 'tools/job.mjs']
RESIZE = ['tools/resize_ban.sh']
IMG = ['env', 'IMAGE_BIG=0', 'IMAGE_WASM=0', 'tests/image/run.sh']   # the image module, native, small inputs
IMGBIG = ['env', 'IMAGE_WASM=0', 'tests/image/run.sh']               # with the big inputs (budget, peaks)
IMGW = ['env', 'IMAGE_BIG=0', 'tests/image/run.sh']                  # and in wasm, both ways out (tools/image.mjs)
SIMD = ['tests/image/simd.sh']                                       # the SIMD loops against their scalar twins, in node
PICK = ['tests/pick/run.sh']                                         # the SIMD pick (APP_SIMD) in Chrome (tools/pick.mjs)
LEGW2J = ['env', 'LEGACY=1', 'node', 'tools/legacy.mjs']             # the legacy page as a browser without WebAssembly

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
  ('resize-size', 'shim/cdom.js', '= viewport ? innerWidth : point.clientX | 0;', '= viewport ? innerHeight : point.clientX | 0;', UNIT, 'FAIL resize'),
  ('visibility-target', 'src/cdom.c', '{ return ev == CD_EV_RESIZE ? CD_WINDOW : CD_DOCUMENT; }', '{ (void)ev; return CD_WINDOW; }', UNIT, 'FAIL visibilitychange'),
  ('viewport-dpr', 'shim/cdom.js', 'Math.round(devicePixelRatio * 100)', '100', UNIT, 'FAIL the device pixel ratio'),
  ('viewport-order', 'shim/cdom.js', '    i32[record + viewportW] = innerWidth;\n    i32[record + viewportH] = innerHeight;', '    i32[record + viewportW] = innerHeight;\n    i32[record + viewportH] = innerWidth;', UNIT, 'FAIL the viewport'),
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
  ('str-lifetime-unchecked', 'shim/cdom.js', '    if (k >= u32[tmpCount]) {', '    if (false) {', UNIT, 'FAIL a cd_tmp used after another call'),
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
  ('tmp-window-kept-after-throw', 'shim/cdom.js', '    u32[tmpSerial0] += u32[tmpCount];\n    u32[tmpCount] = 0;\n', '', UNIT, 'FAIL a cd_tmp made before a throw is dead in the next call'),
  # read-only strings: a pointer into read-only data is its own id (docs/strings-plain-c.md, option B)
  ('ro-range-data-end', 'src/cdom.c', 'extern const char __rodata_start[] __attribute__((weak)), __rodata_end[] __attribute__((weak));\ncd_str cd_tmpz(const char *s) {\n  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__rodata_end - (uintptr_t)__rodata_start)', 'extern const char __rodata_start[] __attribute__((weak)), __data_end[] __attribute__((weak));\ncd_str cd_tmpz(const char *s) {\n  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__data_end - (uintptr_t)__rodata_start)', UNIT, 'FAIL read-only strings: a mutable static'),
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
  ('store-no-flush', 'src/cdom_store.c', '((cd_store_fn)fn)(ctx, p, len);\n  cd_flush();', '((cd_store_fn)fn)(ctx, p, len);', UNIT, 'FAIL cd_store: a put'),
  ('rect-order', 'shim/cdom.js', '.set([box.left, box.top, box.width, box.height])', '.set([box.top, box.left, box.width, box.height])', UNIT, 'FAIL cd_rect'),
  ('rect-untransformed', 'shim/cdom.js', 'const box = node.getBoundingClientRect();', 'const box = { left: node.offsetLeft, top: node.offsetTop, width: node.offsetWidth, height: node.offsetHeight };', UNIT, 'FAIL cd_rect'),
  ('now-zero', 'shim/cdom.js', '    return performance.now();', '    return 0;', UNIT, 'FAIL cd_now'),
  ('input-no-prevent', 'shim/cdom.js', '    if (flags & 1) {', '    if (false) {', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-touch-point', 'shim/cdom.js', '    return event.changedTouches ? event.changedTouches[0] : event;', '    return event;', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-mask-16', 'shim/cdom.js', '  let listenMask = new Uint32Array(2048);', '  let listenMask = new Uint16Array(2048);', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-win-viewport', 'shim/cdom.js', '    const viewport = type === RESIZE || type === VISIBILITY;', '    const viewport = true;', UNIT, 'FAIL the document'),
  ('input-unwin-noop', 'shim/cdom.js', '    nodes[id].removeEventListener(EVENTS[type], listener);', '', UNIT, 'FAIL the document'),
  ('input-win-twice', 'shim/cdom.js', '    if (hostListeners[slot]) {\n      return;\n    }\n    const listener', '    const listener', UNIT, 'FAIL the document'),
  ('input-touchmove-passive', 'shim/cdom.js', 'listener, { passive: false });', 'listener);', UNIT, 'FAIL a touchmove on the document'),
  ('input-no-char', 'shim/cdom.js', 'key.length === 1 ? key.charCodeAt(0) : 0;', 'key.length === 1 ? 0 : 0;', UNIT, 'FAIL a keydown on the document'),
  ('input-no-editing', 'shim/cdom.js', '      | (editing(event.target) << 4);', ';', UNIT, 'FAIL a keydown on the document'),
  ('input-prevent-c', 'src/cdom.c', 'void cd_prevent_default(void) { inbox.flags |= CD_PREVENT; }', 'void cd_prevent_default(void) { }', UNIT, 'FAIL a mousedown and a touchstart'),
  ('input-no-time', 'shim/cdom.js', '    i32[evMs] = Math.round(event.timeStamp || 0);', '    i32[evMs] = Math.round(performance.now());', UNIT, 'FAIL a mousedown and a touchstart'),
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
  ('random-offset', 'shim/cdom.js', '    crypto.getRandomValues(bytes(bufferPtr, length));', '    crypto.getRandomValues(bytes(bufferPtr + 1, length));', UNIT, 'FAIL cd_random_bytes: 200,000 bytes'),
  ('random-noop', 'shim/cdom.js', '    crypto.getRandomValues(bytes(bufferPtr, length));', '    bytes(bufferPtr, length);', UNIT, 'FAIL cd_random_bytes: two fills differ'),
  ('random-zero-asks', 'src/cdom_random.c', '  while (len) {', '  for (int once = 1; once || len; once = 0) {', UNIT, 'FAIL cd_random_bytes: two fills differ'),
  ('random-unavailable-ok', 'shim/cdom.js', '    if (!globalThis.crypto || !crypto.getRandomValues) {\n      return 0;', '    if (!globalThis.crypto || !crypto.getRandomValues) {\n      return 1;', UNIT, 'FAIL no crypto.getRandomValues'),
  ('random-unavailable-ignored', 'src/cdom_random.c', '    if (!j_random(p, n)) return CD_RANDOM_UNAVAILABLE;', '    j_random(p, n);', UNIT, 'FAIL no crypto.getRandomValues'),
  ('url-parts-swapped', 'shim/cdom.js', 'const URL_PARTS = ["pathname", "search", "hash"];', 'const URL_PARTS = ["pathname", "hash", "search"];', UNIT, 'FAIL cd_url_read'),
  ('url-all-path-only', 'shim/cdom.js', '    return location.pathname + location.search + location.hash;', '    return location.pathname;', UNIT, 'FAIL cd_url_read'),
  ('url-read-overflow', 'shim/cdom.js', '    bytes(destPtr, capacity).set(encoded.subarray(0, capacity));', '    bytes(destPtr, encoded.length).set(encoded);', UNIT, 'FAIL cd_url_read'),
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
  ('host-pointer-fields', 'shim/cdom.js', '    i32[evPointer] = pointer ? event.pointerId : 0;', '    i32[evPointer] = 0;', HOST, 'FAIL pointerdown with pointer id'),
  ('host-wheel-dy', 'shim/cdom.js', '    f32[evDy] = wheel ? event.deltaY : 0;', '    f32[evDy] = 0;', HOST, 'FAIL wheel deltaY'),
  ('host-no-stop', 'shim/cdom.js', '      event.stopPropagation();', '', HOST, 'FAIL click stopped at the mount'),
  ('host-stop-c', 'src/cdom.c', 'void cd_stop_propagation(void) { inbox.flags |= CD_STOP; }', 'void cd_stop_propagation(void) { }', HOST, 'FAIL click stopped at the mount'),
  ('host-flags-kept', 'src/cdom.c', '  inbox.flags = 0;\n  uint32_t id = (uint32_t)inbox.id;', '  uint32_t id = (uint32_t)inbox.id;', HOST, 'FAIL the next click is not prevented'),
  ('host-reserved-free', 'src/cdom.c', '  if (id < CD_FIRST_ID) return;\n  walk_done(); j_free(id);', '  walk_done(); j_free(id);', HOST, 'unreachable'),
  ('host-first-id', 'src/cdom.c', 'static cd_id next_id = CD_FIRST_ID;', 'static cd_id next_id = 1;', HOST, 'FAIL node(CD_WINDOW)'),
  ('host-window-unbound', 'shim/cdom.js', '    nodes[WINDOW] = window;\n', '', HOST, 'page did not start'), # cd_main listens on CD_WINDOW, so the page fails at start
  ('host-set-custom', 'shim/cdom.js', '        object.setProperty(property, value);', '        object[property] = value;', HOST, 'FAIL set custom property'),
  ('host-call-result', 'shim/cdom.js', '      nodes[RESULT] = result;\n', '', HOST, 'FAIL contains(node) read back'),
  ('host-val-obj', 'shim/cdom.js', '          object[key] = value;', '', HOST, 'FAIL scrollIntoView'),
  ('host-val-num', 'shim/cdom.js', '      case 0: return [f64[(at + valNum) >> 1], index + 1];', '      case 0: return [u32[at + valNum], index + 1];', HOST, 'FAIL set number'),
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
  ('unit-host-set-swallowed', 'shim/cdom.js', '      reportError(error);\n    }\n  }\n  // Calls the method', '    }\n  }\n  // Calls the method', UNIT, 'FAIL a set along a missing path'),
  ('unit-host-call-skipped', 'shim/cdom.js', '      const result = object[keys[keys.length - 1]](...args);', '      const result = undefined;', UNIT, 'FAIL cd_call: focus and blur'),
  ('seed-option-ignored', 'shim/cdom.js', '    if (options && options.seed !== undefined) {', '    if (false) {', UNIT, 'FAIL options.seed is cd_seed'),
  ('seed-math-random', 'shim/cdom.js', '    return crypto.getRandomValues(new Uint32Array(1))[0];', '    return (Math.random() * 4294967296) >>> 0;', UNIT, 'FAIL the shim draws no stray random'),
  ('url-step-negated', 'shim/cdom.js', 'history.go(delta)', 'history.go(-delta)', UNIT, 'FAIL cd_url_step'),
  ('canvas-blit-offset', 'shim/cdom.js', 'new Uint8ClampedArray(memory.buffer, pixelsPtr, width * height * 4)', 'new Uint8ClampedArray(memory.buffer, pixelsPtr + 4, width * height * 4)', CANVAS, 'blit: byte'),
  ('frames-double', 'src/cdom.c', '  if (frames_on && !frames_asked) {', '  if (frames_on && (frames_asked || 1)) {', CANVAS, 'frame loop doubled'),
  ('frames-backwards', 'shim/cdom.js', 'exports.cd_raf(token, time))', 'exports.cd_raf(token, -time))', CANVAS, 'frame times going backwards'),
  ('frames-no-stop', 'src/cdom.c', '  if (!frames_on) return;\n', '', CANVAS, 'frames after a stop from outside a frame'),
  ('frames-token', 'src/cdom.c', '  if (token == CD_FRAMES_TOKEN) frame(t); else', '  if (0) frame(t); else', CANVAS, 'frames: '),
  ('call-noop', 'shim/cdom.js', '      const result = object[keys[keys.length - 1]](...args);', '      const result = undefined;', CANVAS, 'call: pinged'),
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
  ('offsets-literal', 'shim/cdom.js', '    i32[evButtons] = pointer ? event.buttons : 0;', '    i32[evType + 13] = pointer ? event.buttons : 0;', UNIT, 'FAIL the shim indexes C memory by a literal offset: cdom.js:'),
  ('offsets-bare-base', 'shim/cdom.js', '    const tag = u32[at + valTag];', '    const tag = u32[at];', UNIT, 'FAIL the shim indexes C memory by a literal offset: cdom.js:'),
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
  ('mk-wasm-no-libc', 'cdom.mk', 'CDOM_CFLAGS := --target=wasm32 -std=c23 -nostdlib -mbulk-memory -isystem $(CDOM)/src/libc -I$(CDOM)/src', 'CDOM_CFLAGS := --target=wasm32 -std=c23 -nostdlib -mbulk-memory -I$(CDOM)/src', LINK, "FAIL cdom.mk's APP_WASM rule (first) does not build an app"),
  # cdlits, which the rule runs on the linked module, refuses a module with no cdom in it before run.sh can look at its exports
  ('mk-wasm-no-cdom', 'cdom.mk', 'cdom_mk_app_src = $(APP_SRC) $(CDOM_SRC)', 'cdom_mk_app_src = $(APP_SRC)', LINK, "cdlits: not a cdom module"),
  ('mk-modules-dropped', 'cdom.mk', ' $(foreach m,$(APP_MODULES),$(CDOM_$(call cdom_mk_up,$(m))_SRC))', '', LINK, "FAIL cdom.mk's APP_WASM rule (first) does not build an app"),
  ('mk-modules-unchecked', 'cdom.mk', 'cdom_mk_known = $(if $(cdom_mk_unknown),', 'cdom_mk_known = $(if ,', LINK, 'FAIL cdom.mk (first) refused APP_MODULES for another reason'),  # make still stops, at the missing file, without saying why
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
  ('mk-page-no-wasm-dep', 'cdom.mk', '$$(CDOM_PAGE_TOOL) $$(APP_WASM) $$(cdom_mk_simd_wasm))', '$$(CDOM_PAGE_TOOL) $$(cdom_mk_simd_wasm))', LINK, 'FAIL the default page (first), bundled, is up to date with a newer module'),
  ('mk-no-default-page', 'cdom.mk', 'cdom_mk_page_src = $(or $(APP_PAGE_SRC),$(CDOM_PAGE))', 'cdom_mk_page_src = $(APP_PAGE_SRC)', LINK, 'FAIL the default page (first) does not build'),
  ('mk-title-full-name', 'cdom.mk', '$(or $(APP_TITLE),$(basename $(notdir $(APP_WASM))))', '$(or $(APP_TITLE),$(notdir $(APP_WASM)))', LINK, 'FAIL the default page (first), bundled, is not shim/page.html'),
  ('mk-wasm-intermediate', 'cdom.mk', '$$(if $$(APP_WASM),$$(APP_WASM) $$(cdom_mk_app_src)', '$$(if $$(APP_WASM),$$(cdom_mk_app_src)', LINK, "FAIL the starter's page does not start its bundled module"),  # make deletes the module after the page
  ('mk-toolchain-unchecked', 'cdom.mk', '\t@$(cdom_mk_toolchain)\n\t$(WASM_CC) $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) $(CDOM_LDFLAGS)', '\t$(WASM_CC) $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) $(CDOM_LDFLAGS)', LINK, 'FAIL cdom.mk with WASM_CC=/usr/bin/clang, which cannot build wasm32, did not stop'),
  ('page-url-unchecked', 'tools/cdpage.c', '  if (a->wasm && !wasms) {', '  if (0) {', LINK, 'FAIL APP_PAGE (first) bundled into a page that loads its module by URL'),
  ('page-url-unnamed', 'tools/cdpage.c', '    if (have_loads)\n', '    if (0)\n', LINK, 'FAIL APP_PAGE (first) refused a page that loads its module by URL with another message'),
  ('page-two-wasm-taken', 'tools/cdpage.c', '      if (++wasms > 1 || find(', '      if (++wasms > 9 && find(', LINK, 'FAIL cdom_page (first) took the two_wasm page'),
  ('page-url-mode-off', 'tools/cdpage.c', '  if (url) {\n    js_string(o, url);', '  if (0) {\n    js_string(o, url);', LINK, 'FAIL the default page (first), APP_WASM_SEPARATE, does not build'),
  ('page-url-unescaped', 'tools/cdpage.c', '    if (c == \'\\\\\' || c == \'"\') { bbyte(o, \'\\\\\'); bbyte(o, c); }', "    if (c == '\\\\') { bbyte(o, '\\\\'); bbyte(o, c); }", LINK, 'FAIL the default page (first), APP_WASM_SEPARATE: APP_WASM_URL'),
  ('page-title-unescaped', 'tools/cdpage.c', '    else if (*s == \'<\') bstr(o, "&lt;");\n', '', LINK, 'FAIL the default page (first), APP_WASM_SEPARATE: not shim/page.html'),
  # tools/cdpage standing alone (tests/page/run.sh): the page from its parts
  ('cdpage-table-after-shim', 'tools/cdpage.c', '      put_lines(o, lits.p, lits.n);\n      bstr(o, "</script>\\n");\n      bput(o, script.p, script.n);\n      bbyte(o, \'\\n\');\n      if (!slurp(a->shim, &shim)) die("%s: cannot read the shim %s", page_name, a->shim);\n      put_lines(o, shim.p, shim.n);\n', '      if (!slurp(a->shim, &shim)) die("%s: cannot read the shim %s", page_name, a->shim);\n      put_lines(o, shim.p, shim.n);\n      put_lines(o, lits.p, lits.n);\n      bstr(o, "</script>\\n");\n      bput(o, script.p, script.n);\n      bbyte(o, \'\\n\');\n', PAGE, 'FAIL the default page, bundled (8-byte module), titled and escaped: the page is not its parts'),
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
  ('css-inline-comments-dropped', 'tools/cdpage.c', '        from = blank ? line_at : SIZE_MAX;', '        from = line_at;', PAGE, 'FAIL comments:'),  # a comment beside a rule goes with its line, and the rule with it
  # cdom.mk and the stylesheets (tests/link/run.sh, the fixture apps of tests/link/apps, and tests/link/css.mjs in Chrome):
  # discovery through make, rebuilding on a stylesheet change only, the first build, an own page, the cascade
  ('mk-css-dir-dropped', 'cdom.mk', ')--css-dir src\n', ')\n', LINK, 'FAIL discovery: the css app'),
  ('mk-css-walk-app-dir', 'cdom.mk', ')--css-dir src\n', ')--css-dir .\n', LINK, 'FAIL an own page that links static/app.css does not build'),
  ('mk-csslist-not-prereq', 'cdom.mk', '$$(APP_WASM) $$(cdom_mk_simd_wasm)) $$(cdom_mk_csslist)', '$$(APP_WASM) $$(cdom_mk_simd_wasm))', LINK, 'FAIL rebuilding: a.css edited, and make -q says'),
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
  ('region-no-unlink', 'src/cdom.c', '    region_unlink(r);\n    take(r, 1);', '    take(r, 1);', UNIT, 'FAIL nested regions: a child hidden alone'),
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
  ('file-no-unsupported', 'shim/cdom.js', '    if (typeof FileReader === "undefined") {', '    if (false) {', LEG, 'FAIL IE 9'),
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
  ('part-flags-leak', 'cdom.mk', '$(cdom_mk_mod_ldflags) $(APP_LDFLAGS) -o $@.linked', '$(cdom_mk_mod_ldflags) $(foreach o,$(cdom_mk_objs),$(call cdom_mk_obj_flags,$(o))) $(APP_LDFLAGS) -o $@.linked', LINK, 'FAIL cdom.mk (first): an app file sees'),
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
  ('img-box-zero', 'src/cdom_image.c', 'if (!max_w || !max_h || max_w > CD_IMAGE_BOX_MAX', 'if (max_w > CD_IMAGE_BOX_MAX', IMG, 'a box of 0'),
  ('img-box-cap', 'src/cdom_image.c', '|| max_w > CD_IMAGE_BOX_MAX || max_h > CD_IMAGE_BOX_MAX) return CD_IMAGE_BAD_BOX;', ') return CD_IMAGE_BAD_BOX;', IMG, 'CD_IMAGE_BOX_MAX'),
  ('exif-ifd-unchecked', 'src/cdom_image.c', '      if (ifd + 2 > room) return 1;\n', '', IMG, 'AddressSanitizer'),
  ('exif-9', 'src/cdom_image.c', 'return o >= 1 && o <= 8 ? o : 1;', 'return o >= 1 && o <= 9 ? o : 1;', IMG, 'orientation 9'),
  ('exif-any-type', 'src/cdom_image.c', 'if (rd16(e, le) == 0x0112 && rd16(e + 2, le) == 3) {', 'if (rd16(e, le) == 0x0112) {', IMG, 'a LONG'),
  ('exif-any-tag', 'src/cdom_image.c', 'if (rd16(e, le) == 0x0112 && rd16(e + 2, le) == 3) {', 'if (rd16(e + 2, le) == 3) {', IMG, 'another tag'),
  ('exif-big-endian', 'src/cdom_image.c', "int le = t[0] == 'I';", 'int le = 1;', IMG, 'big-endian'),
  ('exif-xmp-first', 'src/cdom_image.c', '&& at + 2 + seg <= len && !memcmp(p + at + 4, "Exif\\0\\0", 6)) {', '&& at + 2 + seg <= len) {', IMG, 'jpeg-exif-after-xmp'),
  ('exif-count-unbounded', 'src/cdom_image.c', 'i < count && ifd + 2 + 12 * (uint64_t)(i + 1) <= room;', 'i < count;', IMG, 'AddressSanitizer'),
  ('jpeg-full-size', 'src/cdom_image_jpeg.c', 'if (cw >= dw && ch >= dh) return n;', 'if (cw >= dw && ch >= dh) return 8;', IMG, 'jpeg-'),
  ('jpeg-eighth', 'src/cdom_image_jpeg.c', 'if (cw >= dw && ch >= dh) return n;', 'if (cw >= dw && ch >= dh) return 1;', IMG, 'jpeg-'),
  ('jpeg-cut-ignored', 'src/cdom_image_jpeg.c', 'x->status = x->ran_dry ? CD_IMAGE_CORRUPT : CD_IMAGE_OK;', 'x->status = CD_IMAGE_OK;', IMG, 'cut in half'),
  ('jpeg-not-destroyed', 'src/cdom_image_jpeg.c', '  jpeg_destroy_decompress(&x->cinfo);\n  cd_free(x->row);', '  cd_free(x->row);', IMG, 'left'),
  ('jpeg-destroy-after-create', 'src/cdom_image_jpeg.c', '  jpeg_destroy_decompress(&x->cinfo);\n  cd_free(x->row);', '  if (x->cinfo.src) jpeg_destroy_decompress(&x->cinfo);\n  cd_free(x->row);', IMG, 'review item 4'),
  ('jpeg-scaler-kept', 'src/cdom_image_jpeg.c', '  } else cd_image_scaler_free(&x->sc);\n  cd_free(x);', '  }\n  cd_free(x);', IMG, 'left'),
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
  ('png-alpha-dropped', 'src/cdom_image_png.c', 'else { out[0] = over(r[0], a); out[1] = over(r[1], a); out[2] = over(r[2], a); }\n        out[3] = 255;\n      }\n      break;\n    case 4:', 'else { out[0] = r[0]; out[1] = r[1]; out[2] = r[2]; }\n        out[3] = 255;\n      }\n      break;\n    case 4:', IMG, 'png-rgba'),
  ('png-over-floor', 'src/cdom_image_png.c', '255u * (255u - a) + 127u) / 255u', '255u * (255u - a)) / 255u', IMG, 'png-'),
  ('png-grey-unscaled', 'src/cdom_image_png.c', 'uint32_t g = d == 8 ? v : v * 255 / mask;', 'uint32_t g = v;', IMG, 'png-grey'),
  ('png-bit-order', 'src/cdom_image_png.c', '(r[x / per] >> (8 - d - d * (x % per)))', '(r[x / per] >> (d * (x % per)))', IMG, 'png-'),
  ('png-palette-trns', 'src/cdom_image_png.c', 'if (ctype == 3) for (uint32_t i = 0; i < n && i < 256; i++) x->f.pal[4 * i + 3] = data[i];', 'if (ctype == 3) {}', IMG, 'png-palette'),
  ('png-plte-257', 'src/cdom_image_png.c', 'if (n % 3 || n > 768)', 'if (n % 3)', IMG, 'AddressSanitizer'),
  ('png-plte-not-3', 'src/cdom_image_png.c', 'if (n % 3 || n > 768)', 'if (n > 768)', IMG, 'multiple of 3'),
  ('png-no-plte', 'src/cdom_image_png.c', 'if (ctype == 3 && !seen_plte) { st = CD_IMAGE_CORRUPT; break; }', 'if (0 && ctype == 3 && !seen_plte) { st = CD_IMAGE_CORRUPT; break; }', IMG, 'no PLTE'),
  ('png-no-checksum-end', 'src/cdom_image_png.c', 'if (st == CD_IMAGE_OK && !ended) st = CD_IMAGE_CORRUPT;', 'if (0) st = CD_IMAGE_CORRUPT;', IMG, 'png-adler-missing'),
  ('png-stops-at-last-row', 'src/cdom_image_png.c', 'if (ts == TINFL_STATUS_DONE) { ended = 1; break; }', 'if (ts == TINFL_STATUS_DONE || done == h) { ended = 1; break; }', IMG, 'png-adler-missing'),
  ('png-bad-stream', 'src/cdom_image_png.c', '        if (ts < TINFL_STATUS_DONE) { st = CD_IMAGE_CORRUPT; break; }   /* a bad stream, or its adler32 differs */\n', '', IMG, 'png-adler-mismatch'),
  ('png-interlaced', 'src/cdom_image_png.c', 'if (d[12] || bd == 16) return CD_IMAGE_UNSUPPORTED;', 'if (bd == 16) return CD_IMAGE_UNSUPPORTED;', IMG, 'interlaced'),
  ('png-16-bit', 'src/cdom_image_png.c', 'if (d[12] || bd == 16) return CD_IMAGE_UNSUPPORTED;', 'if (d[12]) return CD_IMAGE_UNSUPPORTED;', IMG, '16-bit'),
  ('png-pixels-unchecked', 'src/cdom_image_png.c', '|| (uint64_t)w * h > CD_IMAGE_PIXELS_MAX) return CD_IMAGE_TOO_BIG;', ') return CD_IMAGE_TOO_BIG;', IMG, 'PNG over'),
  ('png-side-unchecked', 'src/cdom_image_png.c', 'if (w > CD_IMAGE_SIDE_MAX || h > CD_IMAGE_SIDE_MAX || (uint64_t)w * h', 'if ((uint64_t)w * h', IMG, 'PNG side'),
  ('png-rows-kept', 'src/cdom_image_png.c', '  cd_free(x->rows);\n  cd_free(x);\n  return st;', '  cd_free(x);\n  return st;', IMG, 'left'),
  ('png-whole-image', 'src/cdom_image_png.c', 'uint64_t room = (uint64_t)(stride + 1) * 2 + (uint64_t)w * 4;', 'uint64_t room = (uint64_t)(stride + 1) * 2 + (uint64_t)w * 4 + (uint64_t)h * stride;', IMGBIG, 'png-p12: peak'),
  ('png-idat-not-entered', 'src/cdom_image_png.c', 'if (ts == TINFL_STATUS_NEEDS_MORE_INPUT && in_pos >= n) break;', 'if (ts == TINFL_STATUS_NEEDS_MORE_INPUT && in_pos >= n) { st = CD_IMAGE_CORRUPT; break; }', IMG, 'png-rgba'),
  ('png-window-wrap', 'src/cdom_image_png.c', 'dict_ofs = (dict_ofs + (uint32_t)out_n) & (TINFL_LZ_DICT_SIZE - 1);', 'dict_ofs = (dict_ofs + (uint32_t)out_n) % (TINFL_LZ_DICT_SIZE - 1);', IMG, 'png-'),
  ('png-grey-key', 'src/cdom_image_png.c', '} else if (f->key && v == f->key_g) {', '} else if (0) {', IMG, 'png-grey-key'),
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
  ('paeth-upleft', 'src/cdom_image_loops.c', '    c = b;\n', '', SIMD, 'FAIL unfilter'),  # Paeth never moves the up-left pixel along
  ('paeth-min', 'src/cdom_image_loops.c', 'wasm_i16x8_min(pb, pc)', 'wasm_i16x8_max(pb, pc)', SIMD, 'FAIL unfilter'),  # Paeth takes a when pa is at most the larger of pb and pc
  ('paeth-mask', 'src/cdom_image_loops.c', '  int m = 0xff;\n  __asm__', '  int m = 0x1ff;\n  __asm__', SIMD, 'FAIL unfilter'),  # Paeth's left value is not taken modulo 256
  # the SIMD pick (cdom.mk's APP_SIMD, shim/cdom.js start(), tools/cdpage.c, shim/boot.js; tests/pick/run.sh, tests/link/run.sh)
  ('pick-inverted', 'shim/cdom.js', 'url = url[+WebAssembly.validate(', 'url = url[+!WebAssembly.validate(', PICK, 'FAIL bundled page, readable shim: the real Chrome gets the SIMD module'),  # the scalar module where SIMD validates, and the twin where it does not
  ('pick-probe-broken', 'shim/cdom.js', '65, 0, 253, 15, 11]', '65, 0, 254, 15, 11]', PICK, 'FAIL bundled page, readable shim: the real Chrome gets the SIMD module'),  # the probe is no valid module (an opcode no browser has), so validate is false everywhere
  ('pick-bundle-order', 'tools/cdpage.c', '        module_at(o, a->wasm, a->wasm_url);', '        module_at(o, a->simd_wasm ? a->simd_wasm : a->wasm, a->wasm_url);', PICK, 'FAIL bundled page, readable shim: with no wasm SIMD'),  # the bundled pair's first entry is the twin
  ('pick-lits-unchecked', 'cdom.mk', '\t$(if $(filter $@,$(cdom_mk_simd_wasm)),@$(call cdom_mk_lits_same,$(APP_WASM),$@))\n', '', LINK, 'FAIL APP_SIMD: a twin with a string literal APP_WASM lacks builds'),  # the twin's string table is never compared with APP_WASM's
  ('pick-boot-simd', 'shim/boot.js', 'url : url[0]) + ".js"', 'url : url[1]) + ".js"', LEGW2J, 'FAIL booted'),  # the no-WebAssembly fallback loads the twin's wasm2js build, which is not there
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
  ('simd-flags-everywhere', 'cdom.mk', 'CDOM_IMAGE_OWN_SIMD_CFLAGS := $(cdom_mk_simd_flags)\n', 'CDOM_IMAGE_OWN_SIMD_CFLAGS := $(cdom_mk_simd_flags)\nCDOM_CFLAGS += $(if $(filter 1,$(CDOM_IMAGE_SIMD)),$(cdom_mk_simd_flags))\n', LINK, 'FAIL an app that does not call the image module has SIMD instructions'),  # -msimd128 for every file, not the image module's
  ('simd-default-on', 'cdom.mk', 'CDOM_IMAGE_SIMD ?= 0', 'CDOM_IMAGE_SIMD ?= 1', LINK, 'FAIL CDOM_IMAGE_SIMD:'),  # the default is SIMD, not scalar
  ('simd-emit-alpha', 'src/cdom_image.c', '    d[0] = q[0]; d[1] = q[1]; d[2] = q[2]; d[3] = 255;', '    d[0] = q[0]; d[1] = q[1]; d[2] = q[2]; d[3] = 254;', IMGW, 'FAIL wasm_js_simd: '),  # the SIMD build's emit writes the wrong alpha
  ('sjlj-sp-kept-low', 'src/cdom_image_sjlj.c', '  sp_set(sp);', '  sp_set(sp_get() + 0 * sp);', UNIT, "FAIL the image module's guard"),
  ('sjlj-eh-val-zero', 'src/cdom_image_sjlj_eh.c', 'jb->arg.val = val ? val : 1;', 'jb->arg.val = 0 * val;', IMGW, 'wasm_eh: jpeg-dqt-bad'),
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
  ('encode-quality-unchecked', 'src/cdom.c', '|| y + h > H || quality > 100) __builtin_trap();', '|| y + h > H) __builtin_trap();', UNIT, "FAIL cd_encode_image's quality"),
  # cd_job (src/cdom_job.c, src/cdom_job_mod.c, tools/job.mjs; cdom.mk's job rule and tools/cdpage's --job in tests/link and tests/page)
  ('job-no-terminate', 'shim/cdom.js', '        worker.terminate();\n        jobAnswer(token, status, output);', '        jobAnswer(token, status, output);', JOB, 'FAIL one worker, ended when it answered'),
  ('job-no-transfer', 'shim/cdom.js', 'worker.postMessage([compiled, input, args], file ? [args] : [input, args]);', 'worker.postMessage([compiled, input, args], []);', JOB, 'FAIL the input and args are transferred'),
  ('job-compile-each', 'shim/cdom.js', 'const module = jobModules[name] || (jobModules[name] = fetch(', 'const module = (jobModules[name] = fetch(', JOB, 'FAIL the module is fetched and compiled once'),
  ('job-no-blob', 'shim/cdom.js', 'URL.createObjectURL(new Blob(["(" + jobWorker + ")()"], { type: "text/javascript" }))', '"data:text/javascript," + encodeURIComponent("(" + jobWorker + ")()")', JOB, 'FAIL the worker comes from a blob: URL'),
  ('job-trap-ok', 'shim/cdom.js', '(error) => postMessage([error === 1 ? 1 : 2]));', '(error) => postMessage([0]));', JOB, 'FAIL FAILED: a job that traps'),
  ('job-room-failed', 'shim/cdom.js', 'status = code > 1 ? 2 : code;', 'status = code ? 2 : 0;', JOB, 'FAIL NO_ROOM: a job that cannot get its memory'),
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
  ('job-rule-any-name', 'cdom.mk', '$$(call cdom_mk_for,$$(cdom_mk_job_files),$$@,$$(cdom_mk_job_src) $$(CDOM_HEADERS))', '$$(cdom_mk_job_src) $$(CDOM_HEADERS)', LINK, 'FAIL cdom.mk built a .job.wasm that APP_JOBS does not name'),
  ('job-page-always-bundled', 'cdom.mk', 'cdom_mk_job_args = $(if $(APP_WASM_SEPARATE),,$(foreach', 'cdom_mk_job_args = $(if x,$(foreach', LINK, 'FAIL a page with APP_WASM_SEPARATE carries the job module'),
  ('job-page-no-jobs', 'cdom.mk', ',$(APP_WASM).lits,$(cdom_mk_job_args))', ',$(APP_WASM).lits)', LINK, 'FAIL a bundled page does not carry its job module'),
  ('job-page-name', 'tools/cdpage.c', 'bstr(o, "var cdomJobs={");', 'bstr(o, "var cdomJob={");', PAGE, 'FAIL two job modules bundled'),
  ('job-page-no-comma', 'tools/cdpage.c', '          if (j) bbyte(o, \',\');\n', '', PAGE, 'FAIL two job modules bundled'),
  ('job-page-name-raw', 'tools/cdpage.c', '          js_string(o, name);\n', '          bbyte(o, \'"\');\n          bstr(o, name);\n          bbyte(o, \'"\');\n', PAGE, 'FAIL two job modules bundled'),
  ('job-page-no-eq-check', 'tools/cdpage.c', 'if (!eq || eq == a->jobs[j]) die(', 'if (eq == a->jobs[j]) die(', PAGE, 'FAIL refuses jobname'),
  ('job-page-empty-ok', 'tools/cdpage.c', 'if (!slurp(eq + 1, &job) || !job.n) die(', 'if (!slurp(eq + 1, &job)) die(', PAGE, 'FAIL refuses jobempty'),
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
  # killed whole past TEST_TIMEOUT; a hang is a broken module, so it counts as caught (HUNG).
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
    try: k, (name, f, old, new, cmd, want) = work.get_nowait()
    except queue.Empty: return
    p = root / f
    src = p.read_text()
    if src.count(old) != 1:
      line, ok = f'SKIP {name}: the text to mutate is not in {f} once', False
    else:
      try:
        p.write_text(src.replace(old, new))
        code, out = build_and_run(root, cmd, minify=True)
      finally:
        p.write_text(src)
        touched = restore_tree(root)
      ok = code == HUNG or (code != 0 and want in out)
      why = '' if ok else f' (exit {code}; ' + ('the FAIL text is there' if want in out else 'no ' + repr(want)) + ')'
      if not ok: # the whole output, kept past the run, since the tail shown here may not hold the cause
        (missed_dir / f'{name}.log').write_text(out)
        why += f' full output: {missed_dir / (name + ".log")}'
      if touched: # a mutant whose test rewrote tracked files: they are back, so the next mutant runs on the real tree
        why += ' (its test changed ' + ', '.join(touched) + '; restored)'
      line = ('caught (HUNG, killed) ' if code == HUNG else 'caught ' if ok else 'MISSED ') + name + why + ('' if ok else '\n  ' + out[-600:].replace('\n', '\n  '))
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
