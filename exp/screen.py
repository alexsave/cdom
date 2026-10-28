#!/usr/bin/env python3
"""Experiment: the screening design. 12 configurations, each sending a random half of the ops the
benchmark executes (8 of 16) through direct imports and the rest through the op list, balanced so
every op is direct in exactly 6 of the 12 (a random 0/1 matrix with fixed row and column sums,
mixed by checkerboard swaps). Prints 'name mask' lines."""
import random
OPS = [3, 9, 13, 29, 30, 25, 26, 22, 24, 14, 10, 15, 27, 20, 23, 21]
random.seed(20261028)
R, C = 12, 16
M = [[1 if (c + r * 8) % 16 < 8 else 0 for c in range(C)] for r in range(R)]  # rows 8, cols 6
for _ in range(20000):
    r1, r2 = random.sample(range(R), 2); c1, c2 = random.sample(range(C), 2)
    if M[r1][c1] == M[r2][c2] == 1 and M[r1][c2] == M[r2][c1] == 0:
        M[r1][c1] = M[r2][c2] = 0; M[r1][c2] = M[r2][c1] = 1
assert all(sum(r) == 8 for r in M) and all(sum(M[r][c] for r in range(R)) == 6 for c in range(C))
for i, r in enumerate(M):
    print(f"xh{i:02d} {hex(sum(1 << OPS[c] for c in range(C) if r[c]))}")
