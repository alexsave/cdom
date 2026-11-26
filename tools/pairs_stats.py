#!/usr/bin/env python3
"""pairs_stats.py: keeps the statistics of tools/pair.sh runs, so the raw runs under results/pairs/ can be deleted.

Usage: python3 -I tools/pairs_stats.py [PAIRS_DIR] [OUT_DIR]     (default results/pairs results/pairs-stats)

Writes OUT_DIR/<tag>.tsv, one row per framework, benchmark and metric of each result file, and OUT_DIR/<tag>.loads.tsv,
the load at each start. Stops if a result file yields no row."""
import glob, json, os, shutil, sys

src = sys.argv[1] if len(sys.argv) > 1 else "results/pairs"
out = sys.argv[2] if len(sys.argv) > 2 else "results/pairs-stats"
FIELDS = ("median", "mean", "stddev", "min", "max")
os.makedirs(out, exist_ok=True)
total = 0
for tag in sorted(os.listdir(src)):
    files = sorted(glob.glob(os.path.join(src, tag, "results", "*.json")))
    if not files:
        continue
    rows = ["framework\tbenchmark\ttype\tmetric\tn\t" + "\t".join(FIELDS)]
    for path in files:
        with open(path) as f:
            r = json.load(f)
        if not r["values"]:
            sys.exit(f"{path}: no values")
        for metric, v in r["values"].items():
            rows.append("\t".join([r["framework"], r["benchmark"], r["type"], metric, str(len(v["values"]))]
                                  + ["-" if v.get(k) is None else f"{v[k]:.6g}" for k in FIELDS]))
    with open(os.path.join(out, tag + ".tsv"), "w") as f:
        f.write("\n".join(rows) + "\n")
    loads = os.path.join(src, tag, "loads.tsv")
    if os.path.exists(loads):
        shutil.copyfile(loads, os.path.join(out, tag + ".loads.tsv"))
    total += len(files)
    print(f"{tag}: {len(files)} result files, {len(rows) - 1} rows")
print(f"{total} result files in all")
