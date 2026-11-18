#!/usr/bin/env python3
"""Writes the picked-file fixture of tests/file.c (tools/file.mjs) into tests/fixtures, the same bytes on every
run (no randomness, no clock). Run it again only to change it.
- data.bin: 1,000 bytes, byte i = (i * 131 + 7) mod 256."""
import pathlib

out = pathlib.Path(__file__).resolve().parent.parent / 'tests' / 'fixtures'
out.mkdir(exist_ok=True)
(out / 'data.bin').write_bytes(bytes((i * 131 + 7) % 256 for i in range(1000)))
