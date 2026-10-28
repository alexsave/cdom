#!/usr/bin/env python3
"""Experiment only (proof of why direct calls win, arm "dswitch"): the reverse of gen_split.py.
Takes gen_direct.py's shim (one import d<N> per op, each with its own small body) and routes every
import into ONE function with a switch over the op, holding all the bodies, as apply() does:
d<N> = (a0, a1, a2) => big(N, a0, a1, a2). The calls from C stay direct and per op; only the
JS side becomes one big function again. If the big function is what costs the list arm, this
arm reopens the gap on the direct side. Usage: gen_direct.py shim/cdom.js | gen_dswitch.py /dev/stdin"""
import re, sys
src = open(sys.argv[1]).read()
start = src.index('  const D = {\n')
end = src.index('  };\n', start) + len('  };\n')
cases, wrappers = [], []
for m in re.finditer(r'    d(\d+): \(a0, a1, a2\) => \{ let n, el; (.*?) \},\n(?=    d\d+:|  \};)', src[start:end], re.S):
    num, body = m.group(1), m.group(2)
    cases.append(f'      case {num}: {body} break;')
    wrappers.append(f'    d{num}: (a0, a1, a2) => big({num}, a0, a1, a2),')
assert len(cases) == src[start:end].count('(a0, a1, a2) =>'), 'every import body must be moved'
new = ('  function big(op, a0, a1, a2) {\n    let n, el;\n    switch (op) {\n' + '\n'.join(cases) +
       '\n      default: throw new Error("cdom: bad op " + op);\n    }\n  }\n  const D = {\n' + '\n'.join(wrappers) + '\n  };\n')
sys.stdout.write(src[:start] + new + src[end:])
