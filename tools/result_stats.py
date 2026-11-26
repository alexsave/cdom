#!/usr/bin/env python3
"""result_stats.py: keeps the statistics of benchmark runs, so their raw result files can be deleted.

Usage: python3 -I tools/result_stats.py RUN_DIR... [--out OUT_DIR]     (default OUT_DIR results/stats)

A RUN_DIR holds js-framework-benchmark result files, itself or under results/ (as tools/pair.sh lays a run out).
Writes OUT_DIR/<name>.tsv, one row per framework, benchmark and metric, and OUT_DIR/<name>.loads.tsv, the load at each
start, when the run has one. Stops on a result file that yields no row, and on a RUN_DIR with no result file."""
import glob, json, os, shutil, sys

args = sys.argv[1:]
out = "results/stats"
if "--out" in args:
    i = args.index("--out")
    out = args[i + 1]
    del args[i:i + 2]
if not args:
    sys.exit(__doc__)
FIELDS = ("median", "mean", "stddev", "min", "max")
os.makedirs(out, exist_ok=True)
for run in args:
    name = os.path.basename(os.path.normpath(run))
    files = sorted(glob.glob(os.path.join(run, "*.json")) + glob.glob(os.path.join(run, "results", "*.json")))
    if not files:
        sys.exit(f"{run}: no result files")
    rows = ["framework\tbenchmark\ttype\tmetric\tn\t" + "\t".join(FIELDS)]
    for path in files:
        with open(path) as f:
            r = json.load(f)
        if not r["values"]:
            sys.exit(f"{path}: no values")
        for metric, v in r["values"].items():
            rows.append("\t".join([r["framework"], r["benchmark"], r["type"], metric, str(len(v["values"]))]
                                  + ["-" if v.get(k) is None else f"{v[k]:.6g}" for k in FIELDS]))
    with open(os.path.join(out, name + ".tsv"), "w") as f:
        f.write("\n".join(rows) + "\n")
    loads = os.path.join(run, "loads.tsv")
    if os.path.exists(loads):
        shutil.copyfile(loads, os.path.join(out, name + ".loads.tsv"))
    print(f"{name}: {len(files)} result files, {len(rows) - 1} rows")
