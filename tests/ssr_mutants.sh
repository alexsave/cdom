#!/bin/bash
# Mutation checks for the milestone 1 tests: each mutant must make its test fail with the
# named assertion (tools/mutate.sh restores the file from a copy). Usage: tests/ssr_mutants.sh
set -u
cd "$(dirname "$0")/.."
bad=0
conform() { tools/mutate.sh "$1" "$2" "$3" node tests/conform.mjs || bad=1; }
hydrate() { tools/mutate.sh "$1" "$2" "$3" node tests/hydrate.mjs 0 1 1000 || bad=1; }
ident() { tools/mutate.sh "$1" "$2" "$3" tests/ssr_identical.sh || bad=1; }
H=src/cdom_html.c J=shim/cdom.js A=bench/cdom-keyed/src/app.c
SINK='FAIL client DOM equals the parsed sink HTML'
# the C sink against the shim (tests/conform.mjs, part 1)
conform $H 's/put\(w, "<!---->", 7\);/(void)0;/' "$SINK"
conform $H 's/case \x27<\x27: e = "&lt;"; break;/case \x27<\x27: continue;/' "$SINK"
conform $H 's/case \x27&\x27: e = "&amp;"; break;/case \x27&\x27: continue;/' "$SINK"
conform $H 's/case \x27"\x27: e = "&quot;"; break;/case \x27"\x27: continue;/' "$SINK"
conform $H 's/insert\(el->parent, el, b \? id\(b, "move"\) : 0, "move"\);/insert(el->parent, el, b ? (hnode *)0 : 0, "move");/' "$SINK"
conform $H 's/for \(const hnode \*c = s->first; c; c = c->next\) insert\(n, clone_in\(c, cid\), 0, "clone"\);//' 'cdom_html: walk from nothing' # the sink itself refuses the walk
conform $H 's/return m;/return 0;/' "$SINK"
conform $H 's/insert\(el->dparent, el, el->dnext, "attach"\);/insert(el->dparent, el, 0, "attach");/' "$SINK"
conform $H 's/if \(name_eq\(n->attrs\[i\]\.n, n->attrs\[i\]\.nlen, name, nlen\)\) \{/if (0) {/' "$SINK"
conform $H 's/if \(!el->tag\) \{ el->len = 0; return; \}/if (!el->tag) return;/' "$SINK"
conform $H 's/put\(w, "\\n", 1\);/;/' "$SINK"
conform $H 's/do t\[n\+\+\] = \(char\)\(\x270\x27 \+ v % 10\); while \(v \/= 10\);/t[n++] = (char)(\x270\x27 + v % 10);/' "$SINK"
conform $H 's/insert\(id\(base, op\), n, ref \? id\(ref, op\) : 0, op\);/insert(id(base, op), n, 0, op);/' 'cdom_html: attribute on a missing node' # the sink refuses a later call
# hydration in the shim (tests/conform.mjs, parts 2 and 3)
conform $J 's/c\.n = m\.nextSibling; m\.remove\(\); \}/c.n = m.nextSibling; }/' 'FAIL hydrated DOM equals the client DOM exactly'
conform $J 's/return p\.insertBefore\(doc\.createTextNode\(s\), n\);/return doc.createTextNode(s);/' 'FAIL hydrated DOM equals the client DOM exactly'
conform $J 's/check\(last\.getAttribute\(a\) === v,/check(true,/' 'FAIL verify refuses an attribute value'
conform $J 's/check\(last\.className === v,/check(true,/' 'FAIL verify refuses a class'
conform $J 's/if \(verify && size\(mount\) - 1 !== /if (false \&\& size(mount) - 1 !== /' 'FAIL verify refuses an extra node'
conform $J 's/\(verify && n\.localName !== tag\.toLowerCase\(\)\)/false/' 'FAIL verify refuses a tag'
conform $J 's/if \(want !== null && c\.nodeValue !== want\) bad\(c, want\); //' 'FAIL verify refuses a slot text'
# hydration of the rows page (tests/hydrate.mjs)
hydrate $J 's/function detach\(id\) \{ if \(hy\) return;/function detach(id) {/' 'FAIL n=1000 no cloneNode'
hydrate $J 's/stats\.adopted \+= fit\(el, tpl\[t\], true\);/fit(el, tpl[t], true);/' 'FAIL n=1 adopted'
hydrate $J 's/if \(\(h = into\(\)\)\) \{ el = adopt\(h, tpl/if (false) { el = adopt(h, tpl/' 'FAIL n=1 no cloneNode'
hydrate $A 's/  if \(nrows\) render\(\);//' 'FAIL n=1 1 rows' # server and browser share the C: both lose the rows
hydrate $A 's/rng = h\.rng; next_id = h\.next_id;/rng = h.rng;/' 'FAIL n=1 appended ids continue'
hydrate $A 's/cd_intern\(\(const char \*\)src \+ o \+ 6, n\)/cd_intern((const char *)src + o + 6, n - (n > 0))/' 'FAIL n=1 no error or verify mismatch'
# the server render: deterministic, and the client's DOM (tests/ssr_identical.sh)
ident $H 's/wbuf w = \{dst, cap, 0\};/wbuf w = {dst, cap, 0}; char adr[24]; put(&w, adr, (uint32_t)snprintf(adr, sizeof adr, "%lx", (unsigned long)(uintptr_t)&w));/g' 'differ'
ident $H 's/for \(uint32_t i = 0; i < n->nattr; i\+\+\) \{/for (uint32_t i = n->nattr; i-- > 0;) {/' 'FAIL n=0: client DOM innerHTML'
ident ssr/rows/server.c 's/cd_seed = SSR_SEED;/cd_seed = SSR_SEED + 2;/' 'FAIL n=1000: client DOM innerHTML'
ident ssr/rows/server.c 's/int verify = vf && len == 1 && vf\[0\] == \x271\x27;/int verify = !(vf && len == 1 && vf[0] == \x271\x27);/' 'differ'
exit $bad
