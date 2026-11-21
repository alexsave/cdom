#!/usr/bin/env python3
"""Prints the table of docs/descent.md's "The three code paths in the real browsers" from docs/descent-marks-3.txt.

The marks are tools/js1bench.mjs's pages' (/mark/<tag>/p-<n>-<run>-<escaped value>): a run writes "start", then
"<ms>ms r=<answer> ok|WANT <wanted> trap=<flag>".  A run with a start and no value died (a run-time error or a stack
overflow): "died".  A run with no mark at all did not run ("-").
Usage: js1bench_table.py <marks> <tag> ...  (columns in that order; a tag is <browser><path>, e.g. ns2t, ie4c)"""
import re
import sys
import urllib.parse


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    tags = sys.argv[2:]
    rows, order = {}, []
    for line in open(sys.argv[1], errors="replace"):
        m = re.search(r'"GET /mark/([^/ ]+)/p-(\d+)-([A-Za-z0-9_]+)-(\S*) HTTP', line)
        if not m or m.group(1) not in tags:
            continue
        tag, n, name, v = m.group(1), int(m.group(2)), m.group(3), urllib.parse.unquote(m.group(4), encoding="latin-1")
        if name in ("ua", "done"):
            continue
        d = rows.setdefault(name, {})
        if name not in order:
            order.append((n, name))
        if v == "start":
            d.setdefault(tag, "died")
        else:
            v = re.sub(r" trap=\S*$", "", v).replace(" r=", " ").replace(" ok", "")
            d[tag] = re.sub(r"^(\d+ms) \d+$", r"\1", v)  # a right answer: the time alone
    names = []
    for n, name in sorted(set(order)):
        if name not in names:
            names.append(name)
    print("| run | " + " | ".join(tags) + " |")
    print("|" + "---|" * (len(tags) + 1))
    for name in names:
        print("| " + name + " | " + " | ".join(rows[name].get(t, "-") for t in tags) + " |")


main()
