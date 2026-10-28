#!/bin/bash
# Mutation checks for the server render and resuming: each mutant must make its test fail with
# the named assertion (tools/mutate.sh restores the file from a copy). Usage: tests/ssr_mutants.sh
set -u
cd "$(dirname "$0")/.."
bad=0
conform() { tools/mutate.sh "$1" "$2" "$3" node tests/conform.mjs || bad=1; }
resume() { tools/mutate.sh "$1" "$2" "$3" node tests/resume.mjs 0 1 1000 || bad=1; }
ident() { tools/mutate.sh "$1" "$2" "$3" tests/ssr_identical.sh || bad=1; }
H=src/cdom_html.c J=shim/cdom.js A=bench/cdom-keyed/src/app.c C=src/cdom.c
SINK='FAIL client DOM equals the parsed sink HTML'
# the C sink against the shim (tests/conform.mjs, part 1)
conform $H 's/put\(w, "<!---->", 7\);/(void)0;/' "$SINK"
conform $H 's/case \x27<\x27: e = "&lt;"; break;/case \x27<\x27: continue;/' "$SINK"
conform $H 's/case \x27&\x27: e = "&amp;"; break;/case \x27&\x27: continue;/' "$SINK"
conform $H 's/case \x27"\x27: e = "&quot;"; break;/case \x27"\x27: continue;/' "$SINK"
conform $H 's/insert\(el->parent, el, b \? id\(b, "move"\) : 0, "move"\);/insert(el->parent, el, b ? (hnode *)0 : 0, "move");/' "$SINK"
conform $H 's/for \(const hnode \*c = s->first; c; c = c->next\) insert\(n, clone\(c\), 0, "clone"\);//' 'cdom_html: walk from nothing' # the sink itself refuses the walk
conform $H 's/return m;/return 0;/' "$SINK"
conform $H 's/insert\(el->dparent, el, el->dnext, "attach"\);/insert(el->dparent, el, 0, "attach");/' "$SINK"
conform $H 's/if \(name_eq\(n->attrs\[i\]\.n, n->attrs\[i\]\.nlen, name, nlen\)\) \{/if (0) {/' "$SINK"
conform $H 's/if \(!el->tag\) \{ el->len = 0; return; \}/if (!el->tag) return;/' "$SINK"
conform $H 's/put\(w, "\\n", 1\);/;/' "$SINK"
conform $H 's/do t\[n\+\+\] = \(char\)\(\x270\x27 \+ v % 10\); while \(v \/= 10\);/t[n++] = (char)(\x270\x27 + v % 10);/' "$SINK"
conform $H 's/insert\(id\(base, op\), n, ref \? id\(ref, op\) : 0, op\);/insert(id(base, op), n, 0, op);/' 'cdom_html: attribute on a missing node' # the sink refuses a later call
# resuming over the sink's HTML (tests/conform.mjs, part 2)
RES='FAIL resumed DOM equals the client DOM exactly'
conform $H 's/else if \(!c->len\) put\(w, "<!--e-->", 8\);/else if (!c->len) {}/' 'FAIL the resumed page before any step is the client DOM' # an empty text leaves no mark
conform $J 's/if \(c\.data !== "e"\) c = c\.nextSibling;/if (0) c = c.nextSibling;/' "$RES" # a separator read as an empty text
conform $J 's/if \(s\.nodeType !== 8 \|\| s\.data === "e"\) k\+\+;/k++;/' 'FAIL resumed: a click on a row' # the trail counts separators
conform $C 's/for \(uint32_t c = b; c != VNONE; c = vt\[c\]\.next\) vt\[c\]\.idx\+\+;//' "$RES" # an insert before a node in the first render
conform $C 's/for \(uint32_t c = vt\[n\]\.next; c != VNONE; c = vt\[c\]\.next\) vt\[c\]\.idx--;//' "$RES" # a remove in the first render
conform $C 's/vt\[n\]\.src = vt\[c\]\.first != VNONE \|\| vt\[c\]\.src \? c \+ 1 : 0;/vt[n].src = 0;/' 'FAIL resume: RuntimeError' # a clone's grandchildren never copied in: a walk into one traps
conform $C 's/  if \(parent != CD_DETACHED\) touch\(parent, 1\);/  if (parent != CD_DETACHED) ensure(parent);/' "$RES" # an insert resolves the parent, not the children whose paths it shifts
conform $C 's/if \(s == VS_WALKED\) r_tree\(0\);/if (s == VS_WALKED) return;/' "$RES" # a server node named in JS is removed: its siblings' paths go stale
conform $C 's/if \(rs && rs_at\(parent\)\) \{ at_stale = 1; return; \}/if (rs \&\& rs_at(parent)) return;/' "$RES" # a text where the first render left AT goes nowhere
conform $C 's/if \(best != VNONE && \(inbox\.id < 0 \|\| bl < hd\)\)/if (best != VNONE \&\& inbox.id < 0)/' 'FAIL resumed: a click on a row' # the resolved list above wins over the row
# resuming the rows page (tests/resume.mjs)
resume $C 's/vt\[n\]\.idx = l == VNONE \? 0 : vt\[l\]\.idx \+ 1;/vt[n].idx = l == VNONE ? 0 : vt[l].idx + 2;/' 'FAIL n=0 the first click loads the wasm' # the virtual tree counts children wrong
resume $C 's/static int rs_remove\(cd_id id\) \{\n  if \(!vmode\) \{ touch\(id, 0\); return 0; \}/static int rs_remove(cd_id id) {\n  if (!vmode) return 0;/' 'FAIL n=1000 select row 4 after removing row 3' # the structural rule
resume $C 's/uint32_t want = trail\[2 \* \(L - 1 - j\)\]/uint32_t want = trail[2 * j]/' 'FAIL n=0 the first click loads the wasm' # the event path matched from the wrong end
resume $J 's/      for \(const \[t, e\] of queue\.splice\(0\)\) handle\(t, e\);\n//' 'FAIL n=0 the first click loads the wasm' # the queued click is dropped
resume $J 's/if \(queue\.push\(\[t, e\]\) > 64\) queue\.shift\(\);/if (!queue.length) queue.push([t, e]);/' 'FAIL n=1000 the second click, fired during the load' # one event queued only
resume src/ssr_page.c 's/  put\(&o, p->events, p->events_len\);/  puts_(&o, "[],[]");/' 'FAIL n=0 the first click loads the wasm' # the page listens to nothing
resume $J 's/function listen\(n\) \{ nodes\[n\] = last; last\.\$i = n; \}/function listen(n) { nodes[n] = last; }/' 'FAIL n=1 the one node with a JS id is the clicked row' # resolved listeners carry no id for the shim
resume $A 's/  if \(nrows\) render\(\);//' 'FAIL n=1 1 rows' # server and browser share the C: both lose the rows
resume $A 's/rng = h\.rng; next_id = h\.next_id;/rng = h.rng;/' 'FAIL n=1 appended ids continue'
resume $A 's/cd_intern\(\(const char \*\)src \+ o \+ 6, n\)/cd_intern((const char *)src + o + 6, n - (n > 0))/' "FAIL n=1 update appends to the model's labels"
# the server render: deterministic, and the client's DOM (tests/ssr_identical.sh)
ident $H 's/wbuf w = \{dst, cap, 0\};/wbuf w = {dst, cap, 0}; char adr[24]; put(&w, adr, (uint32_t)snprintf(adr, sizeof adr, "%lx", (unsigned long)(uintptr_t)&w));/g' 'differ'
ident $H 's/for \(uint32_t i = 0; i < n->nattr; i\+\+\) \{/for (uint32_t i = n->nattr; i-- > 0;) {/' 'FAIL n=0: client DOM innerHTML'
ident ssr/rows/server.c 's/cd_seed = SSR_SEED;/cd_seed = SSR_SEED + 2;/' 'FAIL n=1000: client DOM innerHTML'
exit $bad
