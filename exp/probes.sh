#!/bin/bash
# The proof's CDP probes (tools/clickprobe.mjs): GC, interrupt and compile events over 20 measured
# clicks per arm and benchmark (with the harness's forced GC, and without it on run1k, as
# tools/onetrace.mjs measured), and the allocation profile per click. Each start waits for the
# 1-minute load to be under 3 (bounded). Output: results/proof/probe_<mode>_<entry>_<bench>.txt
set -u
top="$(cd "$(dirname "$0")/.." && pwd)"; out="$top/results/proof"; mkdir -p "$out"
gate() { local w=0; until awk -v l="$(sysctl -n vm.loadavg | awk '{print $2}')" 'BEGIN{exit !(l < 3)}' || [ $w -ge 120 ]; do sleep 10; w=$((w+1)); done; }
for b in run1k create10k clear; do for e in pf-list pf-direct pf-split pf-dswitch; do
  gate; MODE=trace OUT="$out/probe_trace_${e}_$b.jsonl" node "$top/tools/clickprobe.mjs" "$e" "$b" 20 > "$out/probe_trace_${e}_$b.txt" 2>&1
done; done
for e in pf-list pf-direct; do gate; NOGC=1 MODE=trace node "$top/tools/clickprobe.mjs" "$e" run1k 20 > "$out/probe_trace_nogc_${e}_run1k.txt" 2>&1; done
for b in run1k create10k clear; do for e in pf-list pf-direct pf-split; do
  gate; MODE=alloc node "$top/tools/clickprobe.mjs" "$e" "$b" 20 > "$out/probe_alloc_${e}_$b.txt" 2>&1
done; done
echo PROBES_DONE
