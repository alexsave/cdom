#!/usr/bin/env python3
"""The numbers each prediction of docs/math-design.md is judged on, from one run's results (run.sh's files):
the ratios of medians each one names, typical domain, double unless it says float.
Usage: python3 -I bench/math/tools/verdicts.py results/<date>-<name> [browser=chrome]"""
import os, statistics, sys

FNS = 'sin cos tan asin acos atan atan2 exp exp2 expm1 log log2 log10 log1p pow sinh cosh tanh cbrt hypot fmod'.split()

def times(path):
    t = {}
    if not os.path.exists(path): return t
    for line in open(path):
        f = line.rstrip('\n').split('\t')
        if len(f) == 7 and f[1].isdigit(): t.setdefault((f[2], f[3], f[4]), []).append(float(f[5]))
    return {k: statistics.median(v) for k, v in t.items()}

def ratios(t, a, b, fns=FNS, suffix='', u=None):
    u = u or t
    return {f: t[(f + suffix, 'typical', a)] / u[(f + suffix, 'typical', b)] for f in fns
            if (f + suffix, 'typical', a) in t and (f + suffix, 'typical', b) in u}

def show(name, r):
    s = sorted(r.values())
    print(f'{name}: median {statistics.median(s):.2f}x, range {s[0]:.2f}x to {s[-1]:.2f}x')
    print('   ' + ' '.join(f'{f} {v:.2f}' for f, v in r.items()))
    return r

def main():
    p, br = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'chrome'
    c, n, nat, o2 = times(f'{p}-{br}.tsv'), times(f'{p}-node.tsv'), times(f'{p}-native.tsv'), times(f'{p}-native-O2.tsv')
    r = show('P1 js1 / musl', ratios(c, 'js1', 'musl', ['sqrt'] + FNS))
    out = [f for f, v in r.items() if f not in ('fmod', 'sqrt') and not 1.3 <= v <= 3]
    print(f'   not fmod and at or below 1: {[f for f, v in r.items() if f not in ("fmod", "sqrt") and v <= 1]}; fmod {r.get("fmod", 0):.2f}; outside 1.3 to 3 (sqrt aside): {out}')
    r = show('P2 jsn / musl', ratios(c, 'jsn', 'musl'))
    print(f'   under 1: {sum(v < 1 for v in r.values())} of {len(r)}')
    cross = {f: c[(f, 'typical', 'js1')] - c[(f, 'typical', 'jsn')] for f in ['sqrt'] + FNS}
    w = {f: c[(f, 'typical', 'js1w')] - c[(f, 'typical', 'js1')] for f in ['sqrt'] + FNS}
    print(f'P3 js1 - jsn, ns: median {statistics.median(cross.values()):.1f}, range {min(cross.values()):.1f} to {max(cross.values()):.1f}')
    print('   ' + ' '.join(f'{f} {v:.1f}' for f, v in cross.items()))
    print(f'   js1w - js1, ns: median {statistics.median(w.values()):.1f}, range {min(w.values()):.1f} to {max(w.values()):.1f}; js1w slower for {sum(v > 0 for v in w.values())} of {len(w)}')
    r = show('P4 musl chrome / native', ratios(c, 'musl', 'musl', u=nat))
    if o2: show('   (beside P4) musl chrome / native at -O2', ratios(c, 'musl', 'musl', u=o2))
    r = show('P5 ours / musl', ratios(c, 'ours', 'musl'))
    print(f'   inside 0.7 to 1.4: {sum(0.7 <= v <= 1.4 for v in r.values())} of {len(r)}; pow {r["pow"]:.2f}')
    show('P7 core / musl (double)', ratios(c, 'core', 'musl', FNS[:-1]))
    show('P7 core / musl (float)', ratios(c, 'core', 'musl', FNS[:-1], 'f'))
    for f in ('', 'f'):
        k = [(f'sqrt{f}', 'typical', a) for a in ('musl', 'msun', 'core', 'ours')]
        v = [c[x] for x in k]
        m = statistics.median(v)
        print(f'P12 sqrt{f}: ' + ' '.join(f'{x[2]} {c[x]:.2f}' for x in k) + f'; furthest from their median {max(abs(x / m - 1) for x in v) * 100:.0f}%')
    show('P13 musl native / glibc native', ratios(nat, 'musl', 'glibc', ['sin', 'cos', 'exp', 'log', 'pow']))
    for arms in (('musl', 'msun', 'core', 'ours'), ('musl', 'msun', 'core', 'ours', 'js1', 'js1w', 'jsn')):
        keys = [k for k in c if k[2] in arms and k in n]
        within = sum(0.85 <= n[k] / c[k] <= 1.15 for k in keys)
        print(f'P14 node / chrome within 15% ({", ".join(arms)}): {within} of {len(keys)} cells ({within / len(keys) * 100:.0f}%)')

main()
