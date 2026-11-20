#!/usr/bin/env python3
"""Median, min and max of every (experiment, arm, metric) in a results TSV (docs/job-worker-results.tsv).
Usage: python3 tools/shrink_summary.py [file.tsv]   A one-off tool: it reads what tools/shrink.mjs wrote."""
import sys, statistics, collections

path = sys.argv[1] if len(sys.argv) > 1 else 'docs/job-worker-results.tsv'
rows = collections.OrderedDict()
for line in open(path):
    if line.startswith('#') or not line.strip():
        continue
    exp, run, arm, metric, value, unit = line.rstrip('\n').split('\t')
    rows.setdefault((exp, arm, metric, unit), []).append(float(value))
print('experiment\tarm\tmetric\tn\tmedian\tmin\tmax\tunit')
for (exp, arm, metric, unit), v in rows.items():
    print(f'{exp}\t{arm}\t{metric}\t{len(v)}\t{statistics.median(v):.2f}\t{min(v):.2f}\t{max(v):.2f}\t{unit}')
