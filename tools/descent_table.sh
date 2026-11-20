#!/bin/bash
# descent_table.sh: the results table of docs/descent.md, from docs/descent-marks.txt (the serve.py log lines of the runs,
# tags normalised to one per browser).  "n/c": the run did not collect that test (IE 4's page stalled on a block, and the
# whole-page runs of IE 5.01 died on a deep recursion, so the recursion tests were left out of its parts).
cd "$(dirname "$0")/.." || exit 1
python3 tools/probe_marks.py docs/descent-marks.txt ns2 ns304 ns408 ie4 ie501 ie6 --md \
  --na ie4=recurse100,recurse300,recurse1000,recurse5000,regexExec,regexNew,strictEq,inOp,tryCatch,tryFinally,throw,delete,ternary,comma,withStmt,whileBreakContinue,nestedFunc,tstr,tfn,interval,speedConcat1000 \
  --na ie501=recurse100,recurse1000,recurse5000 \
  --na ns2=speedConcat,tstr,tfn,interval --na ns304=speedConcat
