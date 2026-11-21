# The Ultimate Tic-Tac-Toe replay viewer

The cdom twin of uttt.live, and the first real app for the frames tier (docs/frames-design.md: Netscape 2 to 4, IE 3 and 4).
It opens a game code, or a whole `https://uttt.live/<code>` link, and steps through the game: |< to the start, < back a move, Play or Pause (a move every 600 ms), > forward a move, >| to the end.
`uttt.html?<code>` opens that game at once.

The rules and the code are foolish's (`uttt/c/src/uttt.c`, `uttt_code.c`, and `shared/c/b32.c` and `mixrad.c`), compiled from a checkout of github.com/alexsave/foolish; nothing of foolish is copied here.

```
make -C demo/uttt FOOLISH=path/to/foolish     # FOOLISH defaults to ../foolish beside cdom
python3 -m http.server 8000 --directory demo/uttt/out
open http://localhost:8000/uttt.html?AJG7NYNQG4RFXXGVPGD7QR2ETSOYTWMCREAQ
```

The build needs what cdom's does (clang and wasm-ld from LLVM 23, wasm2js from binaryen 130, `npm ci` at the root and in tools/legacy).
One page serves every tier: the boot (shim/boot.js) starts out/uttt.wasm where there is WebAssembly, and on the frames tier loads out/cdom.frames.js and out/uttt.wasm.ie9.js, the words build of the same module.

## What the page is made of

Only what Netscape 2 has: a TABLE of the 3 x 3 blocks, each a 96 x 96 table of 3 x 3 cells in TT, the marks coloured, the last move in B.
A won block is one big mark; a drawn block is grey; the block (or blocks) the next move must go in is yellow; the blocks of the winning line are green.
The colours and sizes are CSS, which the frames tier writes as FONT COLOR, FONT SIZE and a one-cell TABLE's BGCOLOR (on a DIV around each block's table, since a TD's own background would be written as a table inside the row).
The events are click (the six buttons) and change (the code field), the two Netscape 2 fires.

## Two shims for foolish's sources

- `uttt_code.c` calls `strncmp`, which cdom's libc (src/libc/string.h) does not have: compat.h declares it for every file (`-include`), and uttt_replay.c defines it.
- `uttt_code.c` includes `<math.h>` for `log2`, in `uttt_ideal_bits`; libc/math.h declares it, and nothing defines it, since the replay never calls `uttt_ideal_bits` and the linker drops it (the module imports no `log2`).

## Tests

`node tools/uttt.mjs` (FOOLISH as for the build) builds the page, and runs it in Chrome on the WebAssembly tier and on the frames tier (LEGACY=frames, and its ?ns4 flavour).
The expected positions come from tools/uttt_expect.c, built natively against foolish's own code: the move count, and for every ply the 81 cells, the 9 blocks, the blocks the next move may go in and the result.
It skips with a message when FOOLISH is not a checkout; CI has no foolish and does not run it.

## Predictions

Written before measuring; out/uttt.html (28,494 bytes) and out/uttt.wasm.ie9.js (19,819 bytes) had been seen once, at the first build.

- U1, size: the frames tier's three files (out/uttt.html, cdom.frames.js, uttt.wasm.ie9.js) are at most 90,000 bytes together, and at most 30,000 gzipped (gzip -9).
- U2, the app's own share of the generated JavaScript: uttt.wasm.ie9.js is at most 12,000 bytes larger than the same build of tests/frames.wasm.
- U3, redraws: on the frames tier each step (|<, <, >, >|) and each tick of Play draws exactly once.
- U4, a step's redraw costs at most 20 ms in headless Chrome under LEGACY=frames (the board is about 230 nodes).

## Results

Measured after the predictions were committed; headless Chromium (Playwright's, build 1243) on a Linux x86-64 VM, `UTTT_MEASURE=1 node tools/uttt.mjs`, so indicative only.

| | Predicted | Measured | Verdict |
| --- | --- | --- | --- |
| U1, size | at most 90,000 bytes, 30,000 gzipped | 80,425 bytes, 30,270 gzipped (uttt.html 28,494 and 11,311; cdom.frames.js 32,112 and 10,405; uttt.wasm.ie9.js 19,819 and 8,554) | refuted, by 270 bytes gzipped |
| U2, the app's generated JavaScript | at most 12,000 bytes over tests/frames.wasm.ie9.js | 2,826 bytes over | confirmed |
| U3, redraws | one a step and one a tick | one for each of 50 > steps, 20 draws for 20 steps, and Play, 2 ticks and Pause drew 4 | confirmed |
| U4, a step's redraw | at most 20 ms | 2.75 ms (LEGACY=frames), 4.75 ms (its ?ns4 flavour), the mean of 20 | confirmed |

What the miss says: the page itself (28,494 bytes) is mostly the inlined shim/cdom.min.js and the boot, which the frames tier never runs; cdom.frames.js is the largest file, as docs/frames-design.md's P1 found.
The WebAssembly module is 10,213 bytes (5,562 gzipped).
