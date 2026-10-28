#!/bin/bash
# Mutation checks for tests/host.mjs (the chart demo and the cdv decoders in Chrome; one Chrome,
# port 8705). Each mutant must fail the test with the named assertion; tools/mutate.sh restores
# the file from a copy. The test rebuilds apps/chart, so a mutant of the shim reaches the page's
# inlined minified copy. Usage: tests/host_mutants.sh
set -u
cd "$(dirname "$0")/.."
bad=0
m() { tools/mutate.sh "$1" "$2" "$3" node tests/host.mjs || bad=1; }
J=shim/cdom.js C=src/cdom.c H=src/cdom_html.c A=apps/chart/app.c
# the shim's host(): PROP, SET's dotted path, UNMOUNT's delete
m $J 's/\Qif (kind === 3) { el[name] = v; return; }\E/if (kind === 3) return;/' 'FAIL Toggle smooth: the checkbox follows the state through PROP'
m $J 's/\Qfor (let i = 0; i < p.length - 1; i++) o = o[p[i]]; o[p[p.length - 1]] = v;\E/o[name] = v;/' 'FAIL the chart holds N+1 = 6 points'
m $J 's/\Qif (kind === 4) delete el.\E\$h;//' 'FAIL Destroy: Chart.getChart(canvas) is undefined and $h is gone'
# the shim's cdv decoder
m $J 's/\Qnew Uint32Array(a.buffer).set(u32.subarray(vq, vq += 2 * n));\E/new Uint32Array(a.buffer).set(u32.subarray(vq + 1, (vq += 2 * n) + 1));/' 'FAIL cdv case 9'
m $J 's/\Qcase 4: return i32[vq++];\E/case 4: return u32[vq++];/' 'FAIL cdv case 4'
# a named event's record carries the name's id; names are heard in the capture phase
m $J 's/\Qo[0] < 12 ? e.keyCode | 0 : o[1]\E/o[0] < 12 ? e.keyCode | 0 : 0/' 'FAIL a cdping CustomEvent on the paragraph is counted by C'
m $J 's/\Qfor (const t of names) hear(t, true);\E/for (const t of names) hear(t, false);/' 'FAIL one on another node and another name are not; a non-bubbling one on it is'
# the core: host ops on server nodes wait for the end of the first render, then run
m $C 's/\Q  if (!v_virt(hv[0])) return 0;\E\n  hdef/  return 0;\n  hdef/' 'FAIL Add point: the wasm loads and the click is delivered'
m $C 's/\Qfor (uint32_t o = 0; o < nhdef; o += 1 + hdef[o]) {\E/for (uint32_t o = 0; o < 0; o += 1 + hdef[o]) {/' 'FAIL Add point: the wasm loads and the click is delivered'
# the sink writes a PROP checked as the attribute
m $H 's/\Qif (truthy(a, at)) set_attr(n, BOOLS[i]\E/if (!truthy(a, at)) set_attr(n, BOOLS[i]/' 'FAIL the server HTML has the checkbox checked'
# cdtc dispatches a named event by its name's string id
m tools/cdtc.c 's/\Qout(c, "      if ((cd_str)e->key == cdt_s[%d]) "\E/out(c, "      if ((cd_str)e->key != cdt_s[%d]) "/' 'FAIL a cdping CustomEvent on the paragraph is counted by C'
# the page: eager boots at once
m src/ssr_page.c 's/\Qp->eager ? ");cdom.boot();<\E/p->eager ? ");</' 'FAIL eager: the chart appears with no interaction'
# the app: Add point sends the new data
m $A 's/\Q    set_begin(K_SET_DATA); v_data(s); cd_host_end();\E\n//' 'FAIL the chart holds N+1 = 6 points'
exit $bad
