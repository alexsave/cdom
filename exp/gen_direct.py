#!/usr/bin/env python3
"""Experiment only: derives the direct-call shim from shim/cdom.js. Every case of apply()'s op
switch becomes an import d<N>(a0, a1, a2) with the same body, reading its arguments from the
parameters instead of the u32 stream, and sharing the shim's state (last, stack, nodes, ...).
Usage: gen_direct.py shim/cdom.js > out.js"""
import re, sys
src = open(sys.argv[1]).read()
body = src[src.index('switch (u8[p]) {'):src.index('default: throw')]
parts = re.split(r'\n\s*case (\d+):', body)[1:]
fns = []
for num, text in zip(parts[0::2], parts[1::2]):
    text = re.sub(r'//[^\n]*', '', text).strip()
    assert text.endswith('break;'), (num, text)
    text = text[:-len('break;')]
    out, i, k = [], 0, 0
    while i < len(text):
        if text.startswith('u32[q++]', i): out.append(f'a{k}'); k += 1; i += 8
        elif text.startswith('u32[q]', i): out.append(f'a{k}'); i += 6
        elif re.match(r'q\+\+;', text[i:]) and (i == 0 or not text[i-1].isalnum()): k += 1; i += 4
        else: out.append(text[i]); i += 1
    js = ''.join(out).strip()
    assert 'u32' not in js and 'q' not in re.sub(r'[A-Za-z_$][A-Za-z0-9_$]*q|q[A-Za-z0-9_$]+', '', js).replace('Uint', ''), (num, js)
    # wasm passes i32: values that are u32 in the op stream (ids up to 0xffffffff, app numbers) are unsigned again here
    fix = 'a0 >>>= 0; a1 >>>= 0; ' if num in ('1', '5', '11', '18', '30') else ''
    fns.append(f'    d{num}: (a0, a1, a2) => {{ let n, el; {fix}{js} }},')
gen = '\n  // Direct-call imports (experiment): one per op, same bodies as apply()\'s cases.\n  const D = {\n' + '\n'.join(fns) + '\n  };\n'
anchor = '  function value('
assert anchor in src
src = src.replace(anchor, gen + anchor, 1)
src = src.replace('cdom: { apply, value }', 'cdom: { apply, value, ...D }')
assert '...D' in src
sys.stdout.write(src)
