#!/bin/bash
# Builds the keyed-diff fuzz page, the unit page and the click demo into tests/out (served by
# tools/fuzz.mjs and tools/unit.mjs, which taps the demo too) and the canvas and host pages (tools/canvas.mjs,
# tools/host.mjs).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
if [ "$(uname -s)" = Darwin ]; then CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"; else CC="${WASM_CC:-clang}"; fi
legacy="$here/../tools/legacy/build.mjs"
[ -d "$here/../tools/legacy/node_modules" ] || (cd "$here/../tools/legacy" && npm ci --no-audit --no-fund)
mkdir -p "$here/out"
# cdom.mk's flag set, the one apps build with (tools/cdomflags.sh), and its gate: no build script of
# cdom's spells a flag of that set itself.
. "$here/../tools/cdomflags.sh"
cdom_flags_gate
W=(-Wall -Wextra -Werror)
wasm() { # wasm <opt> <out.wasm> <sources and extra flags...>: a cdom module, as cdom.mk builds an app's
  local opt="$1" o="$2"; shift 2
  "$CC" "$opt" "${CDOM_CFLAGS[@]}" "${W[@]}" "${CDOM_LDFLAGS[@]}" -o "$o.linked" "$@"
  "$CDOM_LITS" "$o.linked" "$o" "$o.lits" # the string table out of the module, into o.lits, as cdom.mk does
  rm -f "$o.linked"
}
# shim/cdom.min.js, which apps inline, must be what cdom.js makes now (tools/minshim.mjs).
node "$here/../tools/minshim.mjs" check
wasm -O2 "$here/out/listfuzz.wasm" "$here/listfuzz.c" "$here/../src/cdom.c" "$here/../src/mini.c"
wasm -O2 "$here/out/unit.wasm" "$here/unit.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_store.c" "$here/../src/cdom_audio.c" "$here/../src/cdom_random.c" "$here/../src/cdom_url.c" "$here/../src/cdom_locale.c" "$here/../src/cdom_share.c" "$here/../src/cdom_fonts.c" "$here/../src/cdom_ui.c" "$here/../src/cdom_file.c" "$here/../src/cdom_image_sjlj.c" -DCDOM_IMAGE_SJLJ_JS "$here/../src/mini.c"
wasm -Oz "$here/out/click.wasm" -Wl,--strip-all "$here/../demo/click.c" "$here/../src/cdom.c" "$here/../src/cdom_audio.c" "$here/../src/mini.c"
# Every page inlines the unminified shim at its /*CDOM_SHIM*/ line (tools/cdpage), so the tests run the
# readable shim/cdom.js that tools/mutate.py mutates.
shim="$here/../shim/cdom.js"
# page <in.html> <out.html> <module.wasm>: the string table of that module goes before the shim.
page() { "$CDOM_PAGE_TOOL" --page "$1" --out "$2" --shim "$shim" --lits "$3.lits"; }
page "$here/unit.html" "$here/out/unit.html" "$here/out/unit.wasm"
# The same cases in cd_state's check build (-DCD_STATE_CHECK), whose trap cases unit.mjs opens on unit_check.html.
wasm -O2 "$here/out/unit_check.wasm" -DCD_STATE_CHECK "$here/unit.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_store.c" "$here/../src/cdom_audio.c" "$here/../src/cdom_random.c" "$here/../src/cdom_url.c" "$here/../src/cdom_locale.c" "$here/../src/cdom_share.c" "$here/../src/cdom_fonts.c" "$here/../src/cdom_ui.c" "$here/../src/cdom_file.c" "$here/../src/cdom_image_sjlj.c" -DCDOM_IMAGE_SJLJ_JS "$here/../src/mini.c"
page "$here/unit.html" "$here/out/unit_check.html" "$here/out/unit_check.wasm"
page "$here/../demo/click.html" "$here/out/click.html" "$here/out/click.wasm"
# The fuzz page and the legacy page go through the boot, so LEGACY=1 runs them on the wasm2js build, and
# LEGACY=ie9 on its words build.
page "$here/listfuzz.html" "$here/out/listfuzz.shim.html" "$here/out/listfuzz.wasm"
node "$legacy" page "$here/out/listfuzz.shim.html" "$here/out/listfuzz.html"
node "$legacy" shim "$here/out/cdom.legacy.js"
node "$legacy" shim-ie9 "$here/out/cdom.legacy-ie9.js"
node "$legacy" shim-ie6 "$here/out/cdom.legacy-ie6.js"
node "$legacy" app "$here/out/listfuzz.wasm" "$here/out/listfuzz.wasm.js"
node "$legacy" app-ie9 "$here/out/listfuzz.wasm" "$here/out/listfuzz.wasm.ie9.js"
# The _try allocations at a small cap, and on an app's own allocator (tools/alloc.mjs, in Node).
wasm -O2 "$here/out/alloc_try.wasm" -DCDOM_HEAP_PAGES_MAX=40 "$here/alloc_try.c" "$here/../src/cdom.c" "$here/../src/mini.c"
# The same cases with no cap of cdom's but a memory whose maximum is 40 pages: memory.grow itself refuses.
wasm -O2 "$here/out/alloc_try_max.wasm" -Wl,--max-memory=2621440 "$here/alloc_try.c" "$here/../src/cdom.c" "$here/../src/mini.c"
wasm -O2 "$here/out/alloc_own.wasm" -DOWN_ALLOC "$here/alloc_try.c" "$here/../src/cdom.c" "$here/../src/mini.c"
# The heap's large blocks and an answer's bytes, uncapped (tools/alloc.mjs counts the pages each case takes).
wasm -O2 "$here/out/alloc_mem.wasm" "$here/alloc_mem.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_store.c" "$here/../src/cdom_url.c" "$here/../src/cdom_file.c" "$here/../src/mini.c"
wasm -O2 "$here/out/canvas.wasm" "$here/canvas.c" "$here/../src/cdom.c" "$here/../src/mini.c"
page "$here/canvas.html" "$here/out/canvas.html" "$here/out/canvas.wasm"
# cd_file_read (tools/file.mjs), and the same with the heap capped at 12.5 MB, for NO_ROOM.
wasm -O2 "$here/out/file.wasm" "$here/file.c" "$here/../src/cdom.c" "$here/../src/cdom_file.c" "$here/../src/mini.c"
page "$here/file.html" "$here/out/file.html" "$here/out/file.wasm"
wasm -O2 "$here/out/file_cap.wasm" -DCDOM_HEAP_PAGES_MAX=200 "$here/file.c" "$here/../src/cdom.c" "$here/../src/cdom_file.c" "$here/../src/mini.c"
page "$here/file.html" "$here/out/file_cap.html" "$here/out/file_cap.wasm"
wasm -O2 "$here/out/host.wasm" "$here/host.c" "$here/../src/cdom.c" "$here/../src/mini.c"
page "$here/host.html" "$here/out/host.html" "$here/out/host.wasm"
wasm -O2 "$here/out/legacy.wasm" "$here/legacy.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_file.c" "$here/../src/cdom_job.c" "$here/../src/cdom_image_sjlj.c" -DCDOM_IMAGE_SJLJ_JS "$here/../src/mini.c"
node "$legacy" app "$here/out/legacy.wasm" "$here/out/legacy.wasm.js"
node "$legacy" app-ie9 "$here/out/legacy.wasm" "$here/out/legacy.wasm.ie9.js"
# The page starts the pair [legacy.wasm, legacy.simd.wasm], as an APP_SIMD page does: the twin is the same sources with
# -msimd128, the same string table (as cdom.mk requires), and the fallback takes the scalar one's wasm2js build.
wasm -O2 "$here/out/legacy.simd.wasm" -msimd128 "$here/legacy.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_file.c" "$here/../src/cdom_job.c" "$here/../src/cdom_image_sjlj.c" -DCDOM_IMAGE_SJLJ_JS "$here/../src/mini.c"
cmp "$here/out/legacy.wasm.lits" "$here/out/legacy.simd.wasm.lits"
page "$here/legacy.html" "$here/out/legacy.shim.html" "$here/out/legacy.wasm"
node "$legacy" page "$here/out/legacy.shim.html" "$here/out/legacy.html"
# Float to integer casts on every tier (tools/fptoint.mjs): clang's non-trapping conversions through wasm2js.
wasm -O2 "$here/out/fptoint.wasm" "$here/fptoint.c" "$here/../src/cdom.c" "$here/../src/mini.c"
node "$legacy" app "$here/out/fptoint.wasm" "$here/out/fptoint.wasm.js"
node "$legacy" app-ie9 "$here/out/fptoint.wasm" "$here/out/fptoint.wasm.ie9.js"
page "$here/fptoint.html" "$here/out/fptoint.shim.html" "$here/out/fptoint.wasm"
node "$legacy" page "$here/out/fptoint.shim.html" "$here/out/fptoint.html"
# The frames tier (docs/frames-design.md, tools/frames.mjs): its shim, and a page whose app runs from the wasm2js output
# rewritten to JavaScript 1.0 (app-js1, docs/js1-transform-design.md). The words build is made too, for tools/frames.mjs's
# measurements (FRAMES_APP=ie9 serves it in the transformed file's place).
node "$legacy" shim-frames "$here/out/cdom.frames.js"
wasm -O2 "$here/out/frames.wasm" "$here/frames.c" "$here/../src/cdom.c" "$here/../src/mini.c"
node "$legacy" app-js1 "$here/out/frames.wasm" "$here/out/frames.wasm.js1.js"
node "$legacy" app-ie9 "$here/out/frames.wasm" "$here/out/frames.wasm.ie9.js"
# The transform's CPU test (tests/js1cpu.c): its page on the frames tier, both builds, and the memory forms through it
# (tools/js1.mjs runs those against WebAssembly).
wasm -O2 "$here/out/js1cpu.wasm" "$here/js1cpu.c" "$here/../src/cdom.c" "$here/../src/mini.c"
node "$legacy" app-js1 "$here/out/js1cpu.wasm" "$here/out/js1cpu.wasm.js1.js"
node "$legacy" app-ie9 "$here/out/js1cpu.wasm" "$here/out/js1cpu.wasm.ie9.js"
page "$here/js1cpu.html" "$here/out/js1cpu.shim.html" "$here/out/js1cpu.wasm"
node "$legacy" page "$here/out/js1cpu.shim.html" "$here/out/js1cpu.html"
page "$here/frames.html" "$here/out/frames.shim.html" "$here/out/frames.wasm"
node "$legacy" page "$here/out/frames.shim.html" "$here/out/frames.html"
sed 's|</head><div id="m">|</head><body><div id="m">|' "$here/out/frames.html" > "$here/out/frames-body.html"
grep -q '<body><div id="m">' "$here/out/frames-body.html"
# The two pages again with a string table Netscape 3 parses (tools/js1table.mjs), for the XP VM.
node "$here/../tools/js1table.mjs" "$here/out/frames.html" "$here/out/frames-ns.html"
node "$here/../tools/js1table.mjs" "$here/out/js1cpu.html" "$here/out/js1cpu-ns.html"
# The script engine probe (docs/ie5-design.md), served beside the pages for the XP VM's browsers.
cp "$here/probe.html" "$here/out/probe.html"
# The hold that keeps the Utilu IE 5.01 and 5.5 builds from crashing at a page's Done (tools/ie5hold.py) is the test
# rig's alone: no shipped file and no built page may name its port.
if grep -l "10.0.2.2:8096" "$here/../shim/"*.js "$here/../tools/legacy/"*.js "$here/"*.html "$here/out/cdom.legacy"*.js \
  "$here/out/"*.wasm.js "$here/out/"*.ie9.js "$here/out/"*.js1.js "$here/out/legacy.html" "$here/out/cdom.frames.js" "$here/out/frames.html"; then
  echo "build.sh: the IE 5 test hold (port 8096) is in a shipped or built file" >&2; exit 1
fi
# Every memory access form as wasm, as wasm2js output and through the words pass, for tools/words.mjs to run side
# by side: no cdom in it, so no string table either (no cdlits).
"$CC" -O2 "${CDOM_CFLAGS[@]}" "${W[@]}" "${CDOM_LDFLAGS[@]}" -o "$here/out/memforms.wasm" "$here/memforms.c"
node "$legacy" app "$here/out/memforms.wasm" "$here/out/memforms.wasm.js"
node "$legacy" app-ie9 "$here/out/memforms.wasm" "$here/out/memforms.wasm.ie9.js"
node "$legacy" app-js1 "$here/out/memforms.wasm" "$here/out/memforms.wasm.js1.js"
# cd_job (tools/job.mjs): the app, and the synthetic job module built as cdom.mk builds one (APP_JOBS): cdom's heap alone
# (-DCDOM_HEAP_ONLY), mini.c, cdom_job_mod.c, no string table. The cap build has the heap at 100 pages (6.4 MB), for NO_ROOM.
jobwasm() { # jobwasm <opt> <out.wasm> <sources and extra flags...>
  local opt="$1" o="$2"; shift 2
  "$CC" "$opt" "${CDOM_CFLAGS[@]}" "${W[@]}" "${CDOM_LDFLAGS[@]}" -DCDOM_HEAP_ONLY -o "$o" "$@"
}
jobwasm -Oz "$here/out/shrink_job.wasm" "$here/shrink_job.c" "$here/../src/cdom.c" "$here/../src/mini.c" "$here/../src/cdom_job_mod.c"
jobwasm -Oz "$here/out/shrink_job_cap.wasm" -DCDOM_HEAP_PAGES_MAX=100 "$here/shrink_job.c" "$here/../src/cdom.c" "$here/../src/mini.c" "$here/../src/cdom_job_mod.c"
wasm -O2 "$here/out/job.wasm" "$here/job.c" "$here/../src/cdom.c" "$here/../src/cdom_job.c" "$here/../src/mini.c"
page "$here/job.html" "$here/out/job.html" "$here/out/job.wasm"
# docs/job-worker.md's measurements (tools/shrink.mjs): the page, built -Oz as an app is.
wasm -Oz "$here/out/shrink.wasm" "$here/shrink.c" "$here/../src/cdom.c" "$here/../src/cdom_job.c" "$here/../src/mini.c"
page "$here/shrink.html" "$here/out/shrink.html" "$here/out/shrink.wasm"
