#!/usr/bin/env python3
"""Reads a serve.py log and prints what each browser reported to tests/probe-js1.html (and probe-frames.html).

A mark is a request to /mark/<tag>/p-<n>-<name>-<escaped value>: the tag names the browser (the page's query string),
n counts the order, and the value is JavaScript's escape() of what the test returned.
A test block writes <name>-start, then <name>-<value>, then <name>-ok.  So, per tag and test:
  the value           the block ran to its end
  DIED(start)         the block started and never finished: a run-time error
  no run              no mark at all: the block did not parse (or the page stopped before it)
The tests are listed in the order of tests/probe-js1.html, then any other name seen.
Usage: probe_marks.py <serve.log> [tag ...] [--md] [--na tag=name,name ...] [--page tests/probe-js1b.html]
(no tags: every tag in the log; --page: the page whose test order the rows follow, probe-js1.html by default)
--na marks tests a browser's run did not collect (its page stalled), "n/c", instead of "no run".
No JSON: the log is text and so is the table."""
import re
import sys
import os
import urllib.parse


def unescape(s):
    """JavaScript's unescape: %uXXXX and %XX."""
    s = re.sub(r"%u([0-9A-Fa-f]{4})", lambda m: chr(int(m.group(1), 16)), s)
    return urllib.parse.unquote(s, encoding="latin-1")


def load(path):
    """{tag: [(order, name, value)]} in log order."""
    marks = {}
    pat = re.compile(r'"GET /mark/([^/ ]+)/p-([A-Za-z0-9]+)-([A-Za-z0-9_]+)-(\S*) HTTP')
    for line in open(path, errors="replace"):
        m = pat.search(line)
        if not m:
            continue
        tag, n, name, val = m.groups()
        marks.setdefault(tag, []).append((n, name, unescape(val)))
    # Browsers fetch images in parallel, so the log's order is not the page's: sort by n (the late marks, L1 ..., last).
    for tag in marks:
        marks[tag].sort(key=lambda m: (0, int(m[0])) if m[0].isdigit() else (1, int(m[0][1:])) if re.fullmatch(r'L\d+', m[0]) else (2, 0))
    return marks


def results(marks):
    """{name: value-or-state} for one tag."""
    started, finished, values = set(), set(), {}
    for n, name, val in marks:
        if val == "start":
            started.add(name)
        elif val == "ok" and name in started:
            finished.add(name)
        else:
            values[name] = val
    out = {}
    # In the order of the marks (sorted by n), not a set's: a set of strings iterates in an order that changes with each
    # run's hash seed, which reordered the rows of tests that are not on the page.
    seen = []
    for n, name, val in marks:
        if name not in seen:
            seen.append(name)
    for name in seen:
        if name in values:
            out[name] = values[name] if (name in finished or name not in started) else values[name] + " (no ok)"
        else:
            out[name] = "DIED(start)" if name not in finished else "ok (no value)"
    return out


def test_order(html):
    names = []
    if os.path.exists(html):
        for m in re.finditer(r'mark\("([A-Za-z0-9_]+)", "start"\)', open(html).read()):
            names.append(m.group(1))
    return names


def main():
    argv = sys.argv[1:]
    na = {}
    while "--na" in argv:
        i = argv.index("--na")
        tag, names = argv[i + 1].split("=")
        na.setdefault(tag, set()).update(names.split(","))
        del argv[i:i + 2]
    page = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "probe-js1.html")
    if "--page" in argv:
        i = argv.index("--page")
        page = argv[i + 1]
        del argv[i:i + 2]
    args = [a for a in argv if not a.startswith("--")]
    md = "--md" in argv
    if not args:
        raise SystemExit(__doc__)
    marks = load(args[0])
    tags = args[1:] or sorted(marks)
    per = {t: results(marks.get(t, [])) for t in tags}
    order = test_order(page)
    for t in tags:
        for name in per[t]:
            if name not in order:
                order.append(name)
    rows = [["test"] + tags]
    for name in order:
        row = [name]
        for t in tags:
            cell = per[t].get(name, "n/c" if name in na.get(t, ()) else "no run" if marks.get(t) else "-")
            row.append(" ".join(cell.split()))
        rows.append(row)
    if md:
        print("| " + " | ".join(rows[0]) + " |")
        print("|" + "---|" * len(rows[0]))
        for r in rows[1:]:
            print("| " + " | ".join(c.replace("|", "\\|") for c in r) + " |")
    else:
        w = [min(40, max(len(r[i]) for r in rows)) for i in range(len(rows[0]))]
        for r in rows:
            print("  ".join(c[:w[i]].ljust(w[i]) for i, c in enumerate(r)))


main()
