#!/bin/bash
# Mutation checks for the clone rules of tests/conform.mjs (milestone 3, part B): clones
# appended inside an open element, the hydrator matching a clone's template against the
# server's subtree (separators, empty text, <!--i--> parked inserts, the clone's cursor),
# and the sink's marks. Each mutant must fail the named assertion. Usage: tests/conform_mutants.sh
set -u
cd "$(dirname "$0")/.."
bad=0
m() { tools/mutate.sh "$1" "$2" "$3" node tests/conform.mjs || bad=1; }
S=shim/cdom.js H=src/cdom_html.c
m $S 's/\Qif (sp) stack[sp - 1].appendChild(el); }\E/}/' "Cannot read properties of null (reading 'firstChild')" # the walk into the unplaced clone
m $S 's/\Qif (txt) { if (!c || c.nodeType !== 8) bad(c, "<!---->");\E/if (0) { if (!c || c.nodeType !== 8) bad(c, "<!---->");/' 'FAIL hydration with verify'
m $S 's/\Qwhile (c && c.nodeType === 8 && c.data === "i") {\E/while (0) {/' 'FAIL hydration with verify'
m $S 's/\Qif (m && m.nodeType === 8 && m.data === "i") { c.remove(); c = m; }\E//' 'FAIL hydration with verify'
m $S 's/\Qif (!sp && ref && c.q && c.q.length) {\E/if (0) {/' 'FAIL hydration with verify'
m $S 's/ if \(hy\) while \(last && last\.\$x\) last = last\.nextSibling; break; \/\/ NEXT/ break; \/\/ NEXT/' 'FAIL hydration with verify'
m $S 's/\Qif (c || root || hy.has(s))\E/if (c || hy.has(s))/' 'FAIL hydrated DOM equals the client DOM'
m $S 's/ n\.\$x = 0;//' 'FAIL hydration with verify'
m $H 's/\Qif (before && !member(c, p)) put(w, "<!--i-->", 8);\E//' 'FAIL hydration with verify'
m $H 's/\Q!c->len && c->next && !member(c->next, p)) put(w, "<!---->", 7);\E/!c->len \&\& c->next \&\& !member(c->next, p)) {}/' 'FAIL step 0: hydrated DOM vs client DOM'
m $H 's/\Qif (sp) insert(stack[sp - 1], el, 0, op);\E//' 'cdom_html: down from nothing' # the sink's walk into the unplaced clone
exit $bad
