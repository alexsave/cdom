#!/bin/bash
# Mutation checks for the clone, slot and event rules of tests/conform.mjs: clones appended
# inside an open element, the core's virtual tree for a resumed first render (a clone's inside,
# an insert inside a clone), the sink's marks, slots and events. Each mutant must fail the named
# assertion. Usage: tests/conform_mutants.sh
set -u
cd "$(dirname "$0")/.."
bad=0
m() { tools/mutate.sh "$1" "$2" "$3" node tests/conform.mjs || bad=1; }
S=shim/cdom.js H=src/cdom_html.c
m $S 's/\Qconst el = tpl[t].cloneNode(true); if (sp) stack[sp - 1].appendChild(el);\E/const el = tpl[t].cloneNode(true);/' 'FAIL step 0: client DOM vs sink HTML'
RES='FAIL resumed DOM equals the client DOM exactly'
C=src/cdom.c
m $C 's/\Qif (vsp) v_link(vstk[vsp - 1], n, VNONE);\E/if (vsp \&\& !tpl) v_link(vstk[vsp - 1], n, VNONE);/' "$RES" # a clone opened inside an open element is not counted
m $C 's/\Qvt[v].src = 0;\E\n  v_expand/vt[v].src = 0;\n  return;\n  v_expand/' 'FAIL resume: RuntimeError: unreachable' # a clone's children are never copied in: a name inside one has no path
m $C 's/\Qif (open) { vstk = vec(vstk, &vstkcap, vsp + 1, 4); vstk[vsp++] = n; }\E/if (open) { vstk = vec(vstk, \&vstkcap, vsp + 1, 4); vstk[vsp++] = tpl ? 0 : n; }/' 'FAIL resume: RuntimeError: unreachable' # what goes into an open clone goes into the mount
m $H 's/\Qif (sp) insert(stack[sp - 1], el, 0, "clone");\E//' 'FAIL step 0: client DOM vs sink HTML'
# template slots: the core names each slot; a resumed first render sends their last values once
m $C 's/\Qfor (uint32_t t = 0; t < CD_TPLS; t++) v_slots(t);\E//' "$RES" # later clones copy the template's first values
m src/cdom.c 's/\Qready(); j_name(slot_new[k]);\E/ready();/' 'cdom_html: no node with that id (set_text)'
# events: the mount's listeners (shim) and who listens to what (core)
m $S 's/\Q(e) => handle(t, e), capture);\E/(e) => handle(t, e));/' 'FAIL a named event that does not bubble'
m $S 's/\Qnodes[i] === n && \E//' 'FAIL a click on a node whose id was freed'
m src/cdom.c 's/\Q&& named[i].name == name) return 1;\E/\&\& (named[i].name == name || 1)) return 1;/' 'FAIL an event no node on the path asked for'
m src/cdom.c 's/  lmask\[id\] = 0;\n  free_ids/  free_ids/' 'FAIL a click on a node whose id was freed'
m src/cdom.c 's/\Qpath = path << 8 | (trail[2 * l + 1] & 255);\E/path = path << 8;/' 'FAIL a named event that does not bubble'
m $S 's/\Qif (s.nodeType === 1) m++;\E//' 'FAIL a named event that does not bubble'
m $S 's/\Qif (!(t in on)) { on[t] = 0; nodes[0]\E/if (1) { on[t] = 0; nodes[0]/' "FAIL resumed: a click on the unresolved list itself" # resume's listener and C's for one type: posted twice
exit $bad
