#!/bin/bash
# The whole math bench on one machine (docs/math-design.md): builds, runs the tests, then measures, writing
# results/<date>-<name>-<part> for each part:
#   env.txt       the machine, compilers and browsers
#   tests.txt     node test.mjs (special values per arm, wasm equals native)
#   sizes.tsv     sizes.sh (make sizes: what sin and every function add, after wasm-opt -O3, raw and brotli)
#   native.tsv    build/native/bench time: every native twin and glibc (the host's libm), interleaved
#   native-O2.tsv the same with the twins at -O2 (build/native-O2/): -Oz on x86-64 leaves false dependencies
#                 (sqrtsd into a register it does not clear) that -O2 breaks, so -Oz alone would flatter wasm
#   node.tsv      run.mjs node: the wasm arms in node
#   <browser>.tsv run.mjs <browser>, for each of BROWSERS (default chrome): the wasm arms through page.html
#   accuracy.tsv  build/native/accuracy over N_ACC inputs per entry and domain (default 1000000), with V8's results
#                 (tools/v8dump.mjs) as the arm v8; oracle.txt: tools/oracle.py on the first 200 of each
# Then tools/table.py writes results/<date>-<name>-tables.md from them.
# Usage: bench/math/run.sh [name=run] (a name already used today is refused, so a run never overwrites a committed
# result). ROUNDS (9) and MS (10, the ms each timed run lasts) set the timing; ACCURACY=0 skips the sweep (it needs
# MPFR, and an hour or so on 4 cores); JOBS (4) runs that many accuracy processes at once.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here" || exit 1
day="$(date +%Y-%m-%d)"; name="${1:-run}"; out="results/$day-$name"
rounds="${ROUNDS:-9}"; ms="${MS:-10}"; browsers="${BROWSERS:-chrome}"; acc="${ACCURACY:-1}"; jobs="${JOBS:-4}"; nacc="${N_ACC:-1000000}"
mkdir -p results
ls "$out"-* > /dev/null 2>&1 && { echo "$out-* exists: give another name" >&2; exit 1; }
make -s -j"$jobs" all sizes || exit 1
make -s -j"$jobs" NATIVE_OPT=-O2 build/native-O2/bench || exit 1
[ "$acc" = 0 ] || make -s accuracy || exit 1
cc="$(make -s print-cc)"
{
  echo "date $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "host $(uname -srm)"
  if [ "$(uname -s)" = Darwin ]; then echo "cpu $(sysctl -n machdep.cpu.brand_string)"; else echo "cpu $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-) ($(nproc) cores)"; fi
  echo "clang $($cc --version | head -1) ($cc)"
  echo "wasm-opt $(wasm-opt --version 2>/dev/null)"
  echo "node $(node --version) (v8 $(node -p process.versions.v8))"
  [ "$(uname -s)" = Darwin ] || echo "libc $(ldd --version 2>&1 | head -1)"
  echo "commit $(git rev-parse --short HEAD)$(git diff --quiet HEAD -- . || echo ' (with changes)')"
} > "$out-env.txt"
cat "$out-env.txt"
node test.mjs > "$out-tests.txt" 2>&1; tail -1 "$out-tests.txt"
./sizes.sh > "$out-sizes.tsv" || exit 1
{ uptime; ./build/native/bench time "$rounds" "$ms"; uptime; } > "$out-native.tsv" 2>&1; echo "native: $(grep -c '^native' "$out-native.tsv") runs"
{ uptime; ./build/native-O2/bench time "$rounds" "$ms" | awk 'BEGIN { FS = OFS = "\t" } $1 == "native" { $1 = "native-O2" } 1'; uptime; } > "$out-native-O2.tsv" 2>&1; echo "native -O2: $(grep -c '^native-O2' "$out-native-O2.tsv") runs"
{ uptime; node run.mjs node "$rounds" "$ms"; uptime; } > "$out-node.tsv" 2>&1; echo "node: $(grep -c '^node' "$out-node.tsv") runs"
for br in $browsers; do
  { uptime; node run.mjs "$br" "$rounds" "$ms"; uptime; } > "$out-$br.tsv" 2>&1; echo "$br: $(grep -c "^$br" "$out-$br.tsv") runs"
done
if [ "$acc" != 0 ]; then
  tmp="results/tmp/$day-$name"; mkdir -p "$tmp"
  entries="$(node -e "import('./glue.js').then(async () => { const fs = await import('node:fs'); const mb = globalThis.mathBench; const i = await mb.instantiate('musl', fs.readFileSync('build/musl.wasm')); const n = []; for (let e = 0; e < i.exports.bm_n(); e++) n.push(mb.cstr(i, i.exports.bm_name(e))); console.log(n.join(' ')); })")"
  # One process per entry, JOBS at a time: V8's results first (written beside, then removed), then the sweep.
  printf '%s\n' $entries | xargs -P "$jobs" -I{} sh -c "node tools/v8dump.mjs '$tmp/{}' $nacc {} && ./build/native/accuracy $nacc {} '$tmp/{}' > '$tmp/{}.tsv'; rm -rf '$tmp/{}'"
  { head -1 "$tmp/sin.tsv"; for e in $entries; do tail -n +2 "$tmp/$e.tsv"; done; } > "$out-accuracy.tsv"
  rm -rf "$tmp"
  ./build/native/accuracy sample 200 | python3 -I tools/oracle.py > "$out-oracle.txt"; tail -1 "$out-oracle.txt"
fi
python3 -I tools/table.py "$out" > "$out-tables.md" && echo "tables: $out-tables.md"
