#!/usr/bin/env python3
"""Experiment: fits the screening session. Per benchmark operation, the harness's per-iteration
script times of the all-list arm, the all-direct arm (a) and the 12 random-half configurations
are modelled as baseline + sum of the deltas of the ops that config sends direct (only ops the
operation executes). Prints each delta in ms with its standard error, and the best assignment.
Usage: exp/fit.py results/pairs/screen [metric=script]"""
import json, os, sys
import numpy as np
d = sys.argv[1]; metric = sys.argv[2] if len(sys.argv) > 2 else 'script'
OPS = {3: 'CLOSE', 9: 'LISTEN', 13: 'CLONE', 29: 'TPL_TEXT', 30: 'TPL_TEXT_NUM', 25: 'DETACH', 26: 'ATTACH', 22: 'FREE',
       24: 'CLEAR', 14: 'DOWN', 10: 'VALUE', 15: 'NEXT', 27: 'SEL', 20: 'SET_CLASS', 23: 'MOVE', 21: 'REMOVE'}
EXEC = {'01_run1k': [3, 9, 13, 29, 30, 25, 26], '02_replace1k': [3, 9, 13, 29, 30, 22, 24, 25, 26],
        '03_update10th1k_x16': [14, 10, 15, 27], '04_select1k': [20], '05_swap1k': [23], '06_remove-one-1k': [21],
        '07_create10k': [3, 9, 13, 29, 30, 25, 26], '08_create1k-after1k_x2': [3, 9, 13, 29, 30], '09_clear1k_x8': [22, 24]}
here = os.path.dirname(os.path.abspath(__file__))
masks = {'xlist': 0, 'xall': 0x7ffffffe}
for l in open(os.path.join(here, 'screen.txt')):
    n, m = l.split(); masks[n] = int(m, 16)
res = os.path.join(d, 'results')
def vals(fw, b):
    for f in os.listdir(res):
        if f.startswith(fw + '-') and f.endswith('_' + b + '.json'):
            return json.load(open(os.path.join(res, f)))['values'][metric]['values']
    return None
best = 0
for b, ops in EXEC.items():
    X, y = [], []
    for fw, m in masks.items():
        v = vals(fw, b)
        if not v: continue
        for t in v:
            X.append([1] + [(m >> o) & 1 for o in ops]); y.append(t)
    X, y = np.array(X, float), np.array(y, float)
    beta, *_ = np.linalg.lstsq(X, y, rcond=None)
    r = y - X @ beta; dof = max(1, len(y) - X.shape[1])
    cov = (r @ r / dof) * np.linalg.pinv(X.T @ X)
    se = np.sqrt(np.diag(cov))
    print(f'{b}: baseline (all list) {beta[0]:.3f} ms +- {se[0]:.3f}  (n={len(y)})')
    for i, o in enumerate(ops, 1):
        flag = '  direct wins' if beta[i] + 2 * se[i] < 0 else ('  list wins' if beta[i] - 2 * se[i] > 0 else '')
        print(f'    {OPS[o]:<13} direct - list = {beta[i]:+.3f} ms +- {se[i]:.3f}{flag}')
        if beta[i] + 2 * se[i] < 0: best |= 1 << o
print(f'best assignment (ops whose direct delta is below zero by 2 SE): mask {hex(best)} = ' + ', '.join(OPS[o] for o in OPS if best >> o & 1))
