#!/bin/bash
# descent_table2.sh: the tables of docs/descent.md's "The second probe", from docs/descent-marks-2.txt (the serve.py log
# lines of the runs, tags normalised to one per browser; ns2c is Netscape 2's run of the follow-up page probe-js1c.html).
# IE 4 stops running a page's scripts at its first stack overflow (depth 400), so its deeper depths are "n/c".
cd "$(dirname "$0")/.." || exit 1
python3 tools/probe_marks.py docs/descent-marks-2.txt ns2 ns304 ns408 ie4 --md --page tests/probe-js1b.html \
  --na ie4=depth0_512,depth1_512,depth3_512,depth0_600,depth1_600,depth3_600
echo
python3 tools/probe_marks.py docs/descent-marks-2.txt ns2c --md --page tests/probe-js1c.html
