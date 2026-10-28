#!/bin/bash
# Mutation checks for the browser tests of compiled templates (one Chrome at a time; run after
# any benchmark run has finished). Each mutant must fail its test with the named assertion.
#  hydration of the templated rows page (SSR_APP=tpl tests/hydrate.mjs, port 8704): the server
#  and the client are the same C, so the mutants sit where they diverge: the blob and cd_main;
#  shim vs sink (tests/cdtc/conform.sh): sink calls the compiled templates lean on (a slot is set through set_text).
set -u
cd "$(dirname "$0")/../.."
bad=0
hydrate() { SSR_APP=tpl tools/mutate.sh "$1" "$2" "$3" node tests/hydrate.mjs 0 1 1000 || bad=1; }
conform() { tools/mutate.sh "$1" "$2" "$3" tests/cdtc/conform.sh "$4" || bad=1; }
A=bench/cdom-tpl/app.c C=tools/cdtc.c H=src/cdom_html.c
hydrate $A 's/\Q  page_render(&S); \/* a loaded state\E/  \/* a loaded state/' 'FAIL n=1 1 rows'
hydrate $C 's/\Qif (r) r->%s = cdt_get32(b + *o);\E/if (0) r->%s = cdt_get32(b + *o);/' 'FAIL n=1 no error or verify mismatch' # no u32 field loads: the row ids differ
hydrate $C 's/\Qif (r) r->%s = cd_intern((const char *)b + *o, k);\E/if (r) r->%s = cd_intern((const char *)b + *o, k - (k > 0));/' 'FAIL n=1 no error or verify mismatch'
conform $H 's/\Qif (n->tag) return; \/* nodeValue on an element does nothing *\/\E/if (!n->tag) return;/' 'FAIL client DOM equals the parsed sink HTML' rows
conform $H 's/\Qinsert(id(base, op), n, ref ? id(ref, op) : 0, op);\E/insert(id(base, op), n, 0, op);/' 'FAIL client DOM equals the parsed sink HTML' feat
conform $H 's/\Qset_attr(id(i, "set_class"), "class", 5, S(v), "set_class");\E/id(i, "set_class"); S(v);/' 'FAIL client DOM equals the parsed sink HTML' rows
conform $H 's/\Qhstr v = S(s); set_value(id(i, "set_text"), v.p, v.len, "set_text");\E/S(s); id(i, "set_text");/' 'FAIL client DOM equals the parsed sink HTML' rows
exit $bad
