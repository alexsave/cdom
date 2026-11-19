#!/usr/bin/env python3
"""A JPEG's scan script, and the file cut after each scan (docs/image-progressive-speed.md, the per-scan profile).

Usage: python3 -I bench/image/scans.py FILE [OUT_DIR]

Prints one line per scan: its components, spectral range Ss..Se, successive-approximation bits Ah and Al, and the bytes
of its entropy-coded data. With OUT_DIR, also writes OUT_DIR/<name>-s<k>.jpg for every scan k: the file up to the end
of scan k, then an EOI, so that the time of scan k is the time of -s<k> less the time of -s<k-1>. A bench tool, never
shipped; no randomness.
"""
import os, sys

def walk(d):
    """(header end, [(start, end, comps, Ss, Se, Ah, Al)]): each scan from its SOS marker to the marker after its data."""
    assert d[:2] == b"\xff\xd8"
    i, out, comp_ids = 2, [], []
    while i + 4 <= len(d):
        while i < len(d) and d[i] != 0xff: i += 1
        while i < len(d) and d[i] == 0xff: i += 1
        if i >= len(d): break
        m = d[i]; i += 1
        if m == 0xd9: break
        if 0xd0 <= m <= 0xd7 or m == 0x01: continue
        n = d[i] << 8 | d[i + 1]
        if m in (0xc0, 0xc1, 0xc2):
            comp_ids = [d[i + 8 + 3 * k] for k in range(d[i + 7])]
        if m == 0xda:
            ns = d[i + 2]
            comps = [comp_ids.index(d[i + 3 + 2 * k]) for k in range(ns)]
            ss, se, a = d[i + 3 + 2 * ns], d[i + 4 + 2 * ns], d[i + 5 + 2 * ns]
            j = i + n
            while j + 1 < len(d) and not (d[j] == 0xff and d[j + 1] not in (0x00, 0xff) and not 0xd0 <= d[j + 1] <= 0xd7): j += 1
            out.append((i - 2, j, comps, ss, se, a >> 4, a & 15))
            i = j
            continue
        i += n
    return out

def main():
    path = sys.argv[1]
    d = open(path, "rb").read()
    sc = walk(d)
    for k, (s, e, comps, ss, se, ah, al) in enumerate(sc):
        print(f"scan {k}\tcomponents {','.join(map(str, comps))}\tSs {ss}\tSe {se}\tAh {ah}\tAl {al}\tdata bytes {e - s}")
    if len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        base = os.path.splitext(os.path.basename(path))[0]
        for k, (s, e, *_r) in enumerate(sc):
            open(os.path.join(sys.argv[2], f"{base}-s{k}.jpg"), "wb").write(d[:e] + b"\xff\xd9")

if __name__ == "__main__":
    main()
