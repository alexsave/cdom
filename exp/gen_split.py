#!/usr/bin/env python3
"""Experiment only (proof of why direct calls win, arm "split"): the op list exactly as
shim/cdom.js encodes and sends it, but apply() dispatches each op through a table of small
per-op functions instead of one big switch. Each case body moves unchanged into its own
function; the argument cursor q and the scratch n, el move to the closure so the bodies read
the same u32 stream. If the big switch's tiering or inlining is why the list arm loses, this
arm closes the gap. Usage: gen_split.py shim/cdom.js > out.js"""
import re, sys
src = open(sys.argv[1]).read()
start = src.index('  function apply(')
end = src.index('\n  // Direct-call imports') + 1 if '// Direct-call imports' in src else src.index('  function value(')
old = src[start:end]
body = old[old.index('switch (u8[p]) {'):old.index('default: throw')]
parts = re.split(r'\n\s*case (\d+):', body)[1:]
fns = {}
for num, text in zip(parts[0::2], parts[1::2]):
    text = re.sub(r'//[^\n]*', '', text).strip()
    assert text.endswith('break;'), (num, text)
    fns[int(num)] = text[:-len('break;')].strip()
top = max(fns)
table = ',\n'.join(f'    () => {{ {fns[i]} }}' if i in fns else '    bad' for i in range(top + 1))
new = f'''  // Split apply (experiment): one small function per op, dispatched through a table.
  let q = 0, n, el;
  function bad() {{ throw new Error("cdom: bad op"); }}
  const F = [
{table}
  ];
  function apply(nops, nargs, nstr, nbytes) {{
    if (buf !== mem.buffer) views();
    if (nstr) decode(nstr, nbytes);
    q = ARGS >> 2;
    for (let p = OPS, e = OPS + nops; p < e; p++) F[u8[p]]();
    tmp.length = 0;
  }}

'''
src = src[:start] + new + src[end:]
sys.stdout.write(src)
