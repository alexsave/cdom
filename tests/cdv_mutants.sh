#!/bin/bash
# Mutation checks for tests/cdv.sh: each mutant of the builders (src/cdom.c) or the sink's
# decoder (src/cdom_html.c) must fail the named assertion. Usage: tests/cdv_mutants.sh
set -u
cd "$(dirname "$0")/.."
bad=0
m() { tools/mutate.sh "$1" "$2" "$3" tests/cdv.sh || bad=1; }
m src/cdom.c 's/\Qin_obj[depth] != keyed) __builtin_trap();\E/0) __builtin_trap();/' 'FAIL an object member without its key traps'
m src/cdom.c 's/\Qhv_put((uint32_t)(b >> 32));\E/hv_put((uint32_t)(b >> 31));/' 'FAIL case 5 decodes'
m src/cdom.c 's/\Qif (!hv_open || depth || owed[0]) __builtin_trap();\E/if (!hv_open) __builtin_trap();/' 'FAIL an array short of its items traps'
m src/cdom.c 's/\Qif (depth == CD_V_DEPTH) __builtin_trap();\E/if (depth == CD_V_DEPTH + 1) __builtin_trap();/' 'FAIL nesting past CD_V_DEPTH traps'
# reserve(1, 3, ...) in cd_host_end overruns the argument stream and crashes outright (checked by hand: Trace/BPT trap)
m src/cdom.c 's/\Qelse if (t == CD_V_F64) { A(hv[i++]); A(hv[i++]); }\E/else if (t == CD_V_F64) { A(hv[i + 1]); A(hv[i]); i += 2; }/' 'FAIL case 5 decodes'
m src/cdom_html.c 's/\Qif (k > 1) { put(w, ".", 1);\E/if (k > 2) { put(w, ".", 1);/' 'FAIL case 5 decodes'
m src/cdom_html.c 's/\Q} else if (-6 < n && n <= 0) {\E/} else if (-7 < n \&\& n <= 0) {/' 'FAIL case 5 decodes'
m src/cdom_html.c 's/\Qcase V_NULL: case V_FALSE: return 0;\E/case V_NULL: case V_FALSE: return 1;/' 'FAIL PROP writes value'
m src/cdom_html.c 's/\Qif (js) { put(w, s.p, s.len); break; }\E/if (1) { put(w, s.p, s.len); break; }/' 'FAIL case 7 decodes'
m src/cdom_html.c 's/\Qif (t == V_OBJECT) {\E/if (0) {/' 'cdom_html: argument count mismatch'
exit $bad
