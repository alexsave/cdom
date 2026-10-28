#!/bin/bash
# Mutation checks for tests/state.sh: each mutant of the base64 decoder in src/cdom.c must fail
# the named assertion. Usage: tests/state_mutants.sh
set -u
cd "$(dirname "$0")/.."
bad=0
m() { tools/mutate.sh src/cdom.c "$1" "$2" tests/state.sh || bad=1; }
m 's/\Qif (len & 3) return -1;\E//' 'FAIL "QUJ" followed by D is refused'
m 's/c == \x27\+\x27 \? 62 : c == \x27\/\x27 \? 63/c == \x27+\x27 ? 63 : c == \x27\/\x27 ? 62/' 'FAIL "+/+/" decodes'
m 's/\Qelse if (d < 0) return -1;\E//' 'FAIL "QU!D" is refused'
m 's/\(k == 3 \|\| s\[i \+ 3\] == \x27=\x27\)/1/' 'FAIL "QQ=A" is refused'
m 's/\Q&& i + 4 == len \E//' 'FAIL "QQ==QUJD" is refused'
m 's/\Qk >= 2 &&\E/k >= 1 \&\&/' 'FAIL "Q===" is refused'
m 's/\Qif (pad < 2) s[o++]\E/if (pad < 3) s[o++]/' 'FAIL "QQ==" decodes'
m 's/\Qint bad = n < 0 ? -1 : cd_state_load\E/int bad = n < 0 ? 0 : cd_state_load/' 'FAIL "QUJ" is refused'
exit $bad
