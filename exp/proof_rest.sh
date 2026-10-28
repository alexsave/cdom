#!/bin/bash
# The proof's remaining harness sessions, in order: the direct-into-one-switch arm against the
# direct and list arms (6 sessions), then each Chrome flag arm with the list and direct arms
# (3 sessions each). See exp/proof.sh.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
SET=dswitch ARM=base FWS="pf-list pf-direct pf-dswitch" "$here/proof.sh" 1 2 3 4 5 6
for a in noopt noim semi64 semi128 maxlazy; do ARM=$a FWS="pf-list pf-direct" "$here/proof.sh" 1 2 3; done
echo REST_DONE
