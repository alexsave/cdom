#!/bin/bash
# Mutation check of the prototype's harness: each mutant must make bench/chunked/run.sh's checks fail.
# Usage: EDGE=dir ORACLE=dir bench/chunked/mutate.sh      (proto.c is put back after each mutant)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; p="$here/proto.c"; keep="$(mktemp)"; cp "$p" "$keep"
trap 'cp "$keep" "$p"; rm -f "$keep"' EXIT
mut() {
  cp "$keep" "$p"
  python3 -c "
import sys;p=sys.argv[1];s=open(p).read();a,b=sys.argv[2],sys.argv[3]
assert s.count(a)==1,a;open(p,'w').write(s.replace(a,b))" "$p" "$1" "$2" || { echo "mutant [$3]: anchor missing"; return; }
  r=$(NOTIME=1 timeout 600 "$here/run.sh" 2>&1 | grep -c "FAIL\|Terminated"; [ "${PIPESTATUS[0]}" = 124 ] && echo 1)
  r=$(echo "$r" | paste -sd+ | bc)
  if [ "$r" -gt 0 ]; then echo "killed  [$3]: $r FAIL lines"; else echo "SURVIVED [$3]"; fi
}
mut 'x->skip += (uint64_t)n - x->src.bytes_in_buffer;' ';' "push: a skip past the buffer dropped"
mut 'x->crc_left = 4; }
    } else if (x->state == P_DATA)' 'x->crc_left = 3; }
    } else if (x->state == P_DATA)' "png: a 0-length chunk's CRC taken as 3 bytes"
mut 'memmove(x->buf, x->src.next_input_byte, keep);' 'memmove(x->buf, x->src.next_input_byte, keep); if (keep > 1) keep--;' "push: one held-back byte lost"
mut 'if (n != want) { x->unreadable = 1; cd_image_throw(x->guard); }' ';' "pull: a short read ignored"
mut 'if ((uint64_t)x->at - 8 + 12 + l > x->size) { x->state = P_END; break; }' ';' "png: a chunk past the file is taken"
mut 'uint32_t keep = x->sn < 768 ? 768 - x->sn : 0;' 'uint32_t keep = x->sn < 767 ? 767 - x->sn : 0;' "png: the PLTE buffer a byte short"
mut 'x->at = x->size - x->at < x->skip ? x->size : x->at + (uint32_t)x->skip;' 'x->at = x->size - x->at < x->skip ? x->size : x->at + (uint32_t)x->skip + (x->skip > 0);' "push: a skip one byte long"
mut 'if (n != want) { st = UNREADABLE; break; }
    pieces++;
    x->at += n;' 'pieces++;
    x->at += n;' "push: a short read ignored"
