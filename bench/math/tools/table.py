#!/usr/bin/env python3
"""The math bench's tables (docs/math-design.md): reads one run's results (run.sh writes <prefix>-native.tsv,
-node.tsv, -chrome.tsv, -safari.tsv, -accuracy.tsv, -sizes.tsv) and prints them as Markdown tables: ns per call
(the median over the rounds, with the spread: half of max minus min, as a percentage of the median) per function and
arm, per host, double and float; the wasm tax (each arm's Chrome median over its native twin's); the max ulp error;
and the bytes each arm adds. Usage: python3 -I bench/math/tools/table.py results/<date>-<name>"""
import os, statistics, sys

FNS = 'sqrt sin cos tan asin acos atan atan2 exp exp2 expm1 log log2 log10 log1p pow sinh cosh tanh cbrt hypot fmod'.split()
TRIG = {'sin', 'cos', 'tan'}
WASM_ARMS = ['musl', 'msun', 'core', 'ours', 'js1', 'js1w', 'jsn']
NATIVE_ARMS = ['musl', 'msun', 'core', 'ours', 'glibc', 'applelibm']
ACC_ARMS = ['musl', 'msun', 'core', 'ours', 'glibc', 'applelibm', 'v8']

def read_times(path):
    """{(entry, domain, arm): [ns, ...]} from a timing TSV, and its # lines."""
    t, notes = {}, []
    if not os.path.exists(path): return None, notes
    for line in open(path):
        f = line.rstrip('\n').split('\t')
        if line.startswith('#'): notes.append(line.strip()); continue
        if len(f) != 7 or f[0] == 'env' or not f[1].isdigit(): continue
        t.setdefault((f[2], f[3], f[4]), []).append(float(f[5]))
    return t, notes

def cell(v):
    if not v: return '-'
    m = statistics.median(v)
    spread = (max(v) - min(v)) / 2 / m * 100 if m else 0
    return f'{m:.1f} ±{spread:.0f}%'

def med(t, key):
    v = t.get(key)
    return statistics.median(v) if v else None

def timing(title, t, arms, prec, domain):
    rows = [f'#### {title}', '', '| function | ' + ' | '.join(arms) + ' |', '|---|' + '---|' * len(arms)]
    for fn in FNS:
        if domain != 'typical' and fn not in TRIG: continue
        e = fn + ('f' if prec == 'float' else '')
        rows.append(f'| {e} | ' + ' | '.join(cell(t.get((e, domain, a))) for a in arms) + ' |')
    return '\n'.join(rows) + '\n'

def tax(chrome, native):
    arms = ['musl', 'msun', 'core', 'ours']
    rows = ['#### The wasm tax: Chrome over the native twin (median over median), typical domain', '',
            '| function | ' + ' | '.join(f'{a}' for a in arms) + ' |', '|---|' + '---|' * len(arms)]
    for prec in ('double', 'float'):
        for fn in FNS:
            e = fn + ('f' if prec == 'float' else '')
            out = []
            for a in arms:
                c, n = med(chrome, (e, 'typical', a)), med(native, (e, 'typical', a))
                out.append(f'{c / n:.2f}x' if c and n else '-')
            rows.append(f'| {e} | ' + ' | '.join(out) + ' |')
    return '\n'.join(rows) + '\n'

def accuracy(path, prec, col='max_ulp'):
    if not os.path.exists(path): return ''
    acc = {}
    for line in open(path):
        f = line.rstrip('\n').split('\t')
        if f[0] == 'arm' or len(f) < 9: continue
        acc[(f[1], f[2], f[0])] = f
    what = 'max ulp' if col == 'max_ulp' else 'results not correctly rounded, per million'
    n = next(iter(acc.values()))[3] if acc else '?'
    arms = [a for a in ACC_ARMS if any(k[2] == a for k in acc)]
    rows = [f'#### Accuracy ({prec}): {what}, {n} inputs per function and domain', '',
            '| function | domain | ' + ' | '.join(arms) + ' |', '|---|---|' + '---|' * len(arms)]
    for fn in FNS:
        e = fn + ('f' if prec == 'float' else '')
        for d in (('small', 'typical', 'large') if fn in TRIG else ('typical',)):
            out = []
            for a in arms:
                f = acc.get((e, d, a))
                if not f: out.append('-'); continue
                if col == 'max_ulp':
                    v = float(f[4])
                    out.append('wrong' if v >= 1e8 else f'{v:.3f}')
                else: out.append(f'{int(f[6]) * 1e6 / int(f[3]):.0f}')
            rows.append(f'| {e} | {d} | ' + ' | '.join(out) + ' |')
    return '\n'.join(rows) + '\n'

def sizes(path):
    if not os.path.exists(path): return ''
    s = {}
    for line in open(path):
        f = line.split()
        if f[0] == 'arm': continue
        s[(f[0], f[1])] = (int(f[2]), int(f[3]))
    rows = ['#### Size: bytes added to an empty module (after wasm-opt -O3), raw and brotli', '',
            '| arm | sin | sin, brotli | all | all, brotli |', '|---|---|---|---|---|']
    for a in ('musl', 'msun', 'core', 'ours', 'js1'):
        if (a, 'empty') not in s: continue
        e = s[(a, 'empty')]
        d = [s[(a, k)][i] - e[i] for k in ('sin', 'all') for i in (0, 1)]
        rows.append(f'| {a} | {d[0]} | {d[1]} | {d[2]} | {d[3]} |')
    return '\n'.join(rows) + '\n'

def main():
    p = sys.argv[1]
    out = [f'### Tables of {os.path.basename(p)}', '']
    if os.path.exists(p + '-env.txt'): out += ['```', open(p + '-env.txt').read().strip(), '```', '']
    hosts = {}
    for env in ('chrome', 'safari', 'node', 'native'):
        t, notes = read_times(f'{p}-{env}.tsv')
        if t is None: continue
        hosts[env] = t
        arms = [a for a in (NATIVE_ARMS if env == 'native' else WASM_ARMS) if any(k[2] == a for k in t)]
        out += [f'### {env}: ns per call, median of the rounds ±spread', ''] + [f'    {n}' for n in notes if not n.startswith('# sum')] + ['']
        out.append(timing(f'{env}, double, typical domain', t, arms, 'double', 'typical'))
        out.append(timing(f'{env}, float, typical domain', t, arms, 'float', 'typical'))
        for d in ('small', 'large'):
            out.append(timing(f'{env}, sin cos tan, {d} domain (double)', t, arms, 'double', d))
            out.append(timing(f'{env}, sin cos tan, {d} domain (float)', t, arms, 'float', d))
    if 'chrome' in hosts and 'native' in hosts: out.append(tax(hosts['chrome'], hosts['native']))
    out.append(accuracy(p + '-accuracy.tsv', 'double'))
    out.append(accuracy(p + '-accuracy.tsv', 'float'))
    out.append(accuracy(p + '-accuracy.tsv', 'double', 'not_cr'))
    out.append(sizes(p + '-sizes.tsv'))
    print('\n'.join(out))

main()
