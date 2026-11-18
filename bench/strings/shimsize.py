#!/usr/bin/env python3
"""P7's attribution: copies of shim/cdom.js without the read-only cache (noro) and without cd_tmp_ahead
(noahead), minified as tools/minshim.mjs does, brotli sizes printed. Line numbers are those of 886cf91.
Usage: python3 bench/strings/shimsize.py   (from the repo root; writes bench/strings/out/shimsize)"""
import os, subprocess
L = open('shim/cdom.js').read().split('\n')
o = 'bench/strings/out/shimsize'; os.makedirs(o, exist_ok=True)
def cut(ranges):
    drop = set()
    for a, b in ranges: drop.update(range(a - 1, b))
    return '\n'.join(l for i, l in enumerate(L) if i not in drop)
f = lambda r: 175 + r  # str() starts at line 178: r is the line within the listing from line 176
variants = {'noro': cut([(61, 65), (f(7), f(35)), (1377, 1377)]), 'noahead': cut([(f(46), f(53)), (f(56), f(83))]),
            'full': '\n'.join(L)}
for k, v in variants.items():
    open(f'{o}/{k}.js', 'w').write(v)
    subprocess.run(['node', 'bench/strings/minify.mjs', f'{o}/{k}.js', f'{o}/{k}.min.js'], check=True)
    b = subprocess.run(['brotli', '-q', '11', '-c', f'{o}/{k}.min.js'], capture_output=True).stdout
    print(k, len(b))
