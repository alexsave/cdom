#!/usr/bin/env python3
"""The accuracy sweep's oracle (docs/math-design.md, "Accuracy"): checks MPFR's correctly rounded results, which
build/native/accuracy measures every arm against, and CORE-MATH's, against mpmath at 300 bits, on the first inputs
of each entry and domain (build/native/accuracy sample [n], TSV: entry domain x y mpfr core, in C99 hex floats).
A result is right when no neighbouring double (float, for an entry ending in f) is nearer the exact value.
Usage: build/native/accuracy sample 200 | python3 -I bench/math/tools/oracle.py   (pip install mpmath)"""
import math, struct, sys
import mpmath as mp

mp.mp.prec = 300
F = {
    'sqrt': mp.sqrt, 'sin': mp.sin, 'cos': mp.cos, 'tan': mp.tan, 'asin': mp.asin, 'acos': mp.acos, 'atan': mp.atan,
    'atan2': lambda y, x: mp.atan2(y, x), 'exp': mp.exp, 'exp2': lambda x: mp.power(2, x), 'expm1': mp.expm1,
    'log': mp.log, 'log2': lambda x: mp.log(x, 2), 'log10': mp.log10, 'log1p': mp.log1p, 'pow': mp.power,
    'sinh': mp.sinh, 'cosh': mp.cosh, 'tanh': mp.tanh, 'cbrt': lambda x: mp.sign(x) * mp.cbrt(abs(x)), 'hypot': mp.hypot, 'fmod': lambda x, y: x - y * (mp.floor(x / y) if x / y >= 0 else mp.ceil(x / y)),
}
TWO = {'atan2', 'pow', 'hypot', 'fmod'}

def neighbours(v, fl):
    if fl:
        b = struct.unpack('<I', struct.pack('<f', v))[0]
        return [struct.unpack('<f', struct.pack('<I', (b + d) & 0xffffffff))[0] for d in (-1, 1)]
    return [math.nextafter(v, -math.inf), math.nextafter(v, math.inf)]

def right(v, exact, fl):
    if math.isnan(v) or math.isinf(v): return False
    d = abs(mp.mpf(v) - exact)
    return all(math.isinf(w) or math.isnan(w) or abs(mp.mpf(w) - exact) >= d for w in neighbours(v, fl))

checked = bad_mpfr = bad_core = 0
for line in sys.stdin:
    entry, dom, xs, ys, crs, cores = line.rstrip('\n').split('\t')
    fl = entry.endswith('f') and entry[:-1] in F
    base = entry[:-1] if fl else entry
    x, y, cr, core = (float.fromhex(v) if v not in ('nan', '-nan') else math.nan for v in (xs, ys, crs, cores))
    exact = F[base](mp.mpf(x), mp.mpf(y)) if base in TWO else F[base](mp.mpf(x))
    checked += 1
    if not right(cr, exact, fl):
        bad_mpfr += 1
        print(f'MPFR wrong: {entry} {dom} x={xs} y={ys} mpfr={crs} exact={mp.nstr(exact, 25)}')
    if not math.isnan(core) and not right(core, exact, fl):
        bad_core += 1
        print(f'CORE-MATH wrong: {entry} {dom} x={xs} y={ys} core={cores} exact={mp.nstr(exact, 25)}')
print(f'oracle: {checked} inputs, MPFR wrong on {bad_mpfr}, CORE-MATH wrong on {bad_core}')
sys.exit(1 if bad_mpfr or bad_core else 0)
