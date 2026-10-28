#!/bin/bash
# Proof of why direct calls win: paired, load-gated harness sessions (tools/pair.sh, LOADMAX=3)
# of the op-list arm, the direct arm and (optionally) the split-apply arm on run1k, create10k
# and clear, with Chrome either as is (ARM=base) or through exp/chrome/<ARM>.sh (V8 flags).
# Odd sessions run the entries in the order given, even sessions reversed, so a drift within a
# session does not favour one arm. Results: results/pairs/proof/<SET, default ARM>/s<N>.
# Usage: ARM=base FWS="pf-list pf-direct pf-split" [COUNT=15] exp/proof.sh 1 2 3 ...
set -u
top="$(cd "$(dirname "$0")/.." && pwd)"
arm="${ARM:?ARM}"; fws=(${FWS:?FWS})
chrome=""; [ "$arm" = base ] || chrome="$top/exp/chrome/$arm.sh"
for s in "$@"; do
  order=("${fws[@]}")
  if [ $((s % 2)) = 0 ]; then order=(); for ((i=${#fws[@]}-1; i>=0; i--)); do order+=("${fws[$i]}"); done; fi
  echo "$(date +%T) $arm s$s: ${order[*]}"
  TAG="proof/${SET:-$arm}/s$s" FWS="${order[*]}" COUNT="${COUNT:-15}" PORT=8703 LOADMAX=3 CHROME="$chrome" "$top/tools/pair.sh" 01_ 07_ 09_ > /dev/null
done
echo PROOF_DONE
