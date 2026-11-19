# The image module: decoding and fitting photos in C

The `image` module decodes a JPEG or PNG in C and resizes it to fit inside a box, for apps that resize photos before upload.

It ports raceclub PR #10 (branch photo-low-peak, head a7270e4) into cdom, with every item of the review below fixed.

## The module

- `src/cdom_image.h` is the interface: `cd_image_probe` and `cd_image_resize(p, len, max_w, max_h, &rgba, &w, &h)`, which fits the image inside the box after orientation, never enlarges it, and answers an RGBA block of cdom's heap that the caller frees with `cd_free`.
- `APP_MODULES += image` links it (cdom.mk): four parts, so libjpeg-turbo and miniz build with `-w` and their own `-I`, cdom's three files with the app's flags, and the one file that calls setjmp with the flags of its way out.
- The statuses: `OK`, `NOT_IMAGE`, `TOO_BIG`, `UNSUPPORTED`, `CORRUPT`, `NO_ROOM`, and `BAD_BOX` (a box of 0 or over `CD_IMAGE_BOX_MAX`, 8192, the caller's own mistake; one status more than the plan listed, so a bad call is never read as a bad file).
- The limits, each a named constant in the header: a side of at most 65,500, at most 64 MP, at most 96 MiB held by the JPEG decoder, at most 500 scans.

How each item of the review below is fixed:

| item | fix | test |
| --- | --- | --- |
| 1, 2 | libjpeg's memory goes through cdom's own memory layer (`jmemsys.h` in `src/cdom_image_jpeg.c`, in place of `jmemnobs.c`), every block through `cd_alloc_try`; `jpeg_mem_init` answers `CD_IMAGE_MEM_MAX` (96 MiB) as `max_memory_to_use`, so whole-image coefficient arrays that do not fit beside what is held stop at `jpeg_open_backing_store` (TOO_BIG) before any of them is allocated; the rest libjpeg holds is a few rows, bounded by the width. The pixel caps by format are gone: one pixel cap (64 MP, for time) and the budget (for memory). | `jpeg-prog444-16mp` (OK), `jpeg-prog444-over-budget` and `jpeg-noninter-35mp-over-budget` (TOO_BIG), with their peaks |
| 3 | a progress monitor that stops a file of more than `CD_IMAGE_SCANS_MAX` (500) scans (TOO_BIG) | `jpeg-scans500` (OK), `jpeg-scans501` (TOO_BIG) |
| 4 | `jpeg_destroy_decompress` always, outside the guard | `test_no_room_at_create`: every room from 0 to 64 KiB, nothing left |
| 5 | the EH runtime is clang 23's interface (`__wasm_setjmp`, `__wasm_setjmp_test`, `__wasm_longjmp`), its values in the jmp_buf, in `src/cdom_image_sjlj_eh.c` | tools/image.mjs, the `wasm_eh` page |
| 6 | every allocation is `cd_alloc_try` (the module's, libjpeg's through the memory layer, miniz's through `MZ_MALLOC`) | the native heap that stays full (`test_no_room`), and the capped wasm heap in tools/image.mjs |
| 7 | re-measured on main's heap | "The bench", below |
| 8 | a box of 0 or over 8192 is `BAD_BOX` | `png-box*` cases, `test_refusals` |
| 9 | tRNS colour keys for grey (any depth) and RGB | `png-grey-key`, `png-grey2-key`, `png-rgb-key`, `png-rgb-key-over-255` |
| 10 | the probe applies the side and pixel limits; the adler32 is checked (the stream is read to its end); no claim of no globals in the JS way (it has none either) or the EH way (none); the orientation's step is an offset made a pointer only inside the image; no `-w` on cdom's own files | `png-adler-mismatch`, `png-adler-missing`, `test_refusals` |

## The way out of a libjpeg error

libjpeg's error exit must not return, and cdom's wasm has no setjmp.
cdom builds it either way, chosen by one make variable, `CDOM_IMAGE_SJLJ`:

- `js` (the default): `cd_image_guard` calls the import `image_call`, which calls the export `cd_image_run(body, ctx)` inside a JavaScript try; the error exit calls the import `image_throw`, which throws a value only the shim's catch takes; anything else thrown (a trap) goes on as it came.
  The frames the throw unwound never put the stack pointer back, so the guard does, from the value it read before the call (two lines of inline assembly).
  Plain wasm: every browser cdom runs in, and wasm2js.
  No `-mllvm` flag: the calls are explicit, so there is no lowering, no `invoke_*` per signature, no exported table and no `__THREW__`.
  The plan named emscripten's lowering (`-enable-emscripten-sjlj`); that lowering needs the same JavaScript try plus a runtime of seven symbols and an exported function table, for a guard that only ever wraps one call, so this way does the same with less (the spike measured both; see below).
- `eh`: setjmp and longjmp lowered by clang to wasm exception handling (`-mexception-handling -mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=true`) on `cdom_image_sjlj.c`, with the runtime in `cdom_image_sjlj_eh.c` (a file of its own: the lowering renames a runtime it finds in the file it lowers), which also defines the C longjmp tag.
  Chrome 95, Firefox 100, Safari 15.2; no wasm2js.

The shim's part is the two functions above: 151 bytes minified, 50 brotli, in every page (the shim has no per-module part; the manager accepted the 50 bytes).

What the js way must hold, each with its test:

- A throw leaves the heap as it was: the guard returns into `cd_image_jpeg_resize`, which always runs `jpeg_destroy_decompress` and frees its row, its scaler and its state, so 100 fatal errors in a row grow the memory by 0 pages after the first (tools/alloc.mjs, both ways), and tools/image.mjs runs 300 more in Chrome.
- Only the sentinel is taken: a trap inside the body goes on as a trap, not as a status (tests/unit.html, `?trap=imageTrap`).
- The stack pointer is put back: 2,000 throws from 40 frames down inside one call leave it where it was (tests/unit.html; without the put-back that is 16 MB below a 64 KB stack).
- Nesting: guards may nest (a body that runs a guard of its own), and each throw lands at the innermost guard, which is the one the module names, since it throws only to the guard of the decode running (tests/unit.html, `t_sj_nested`).
  Re-entry cannot happen: a decode is one synchronous call into wasm, so nothing else runs in the module until it returns.

## Predictions (written before any measurement)

Every number below is a prediction, from reading the code, before anything was timed.

**The spike: the cost of each way out, on 12 MP.**
Both ways cost one guard per decode, around the one call that runs the whole decode (`body`); no row and no libjpeg call crosses into JavaScript or a try.

- S1: the time of `cd_image_resize` on `p12.jpg` (4032 x 3024 baseline, to a 900 box) differs between `js` and `eh` by less than 1% of its median.
  Falsifier: a difference of 2% or more, outside the spread of both.
- S2: the same for emscripten's lowering (`-enable-emscripten-sjlj`, a scratch build, not kept): within 1% too.
- S3: a fatal error (a bad DQT) costs under 1 ms either way, and leaves the stack pointer and the heap where they were (tools/image.mjs checks the second part on every run).
- S4: the `eh` module is within 300 bytes of the `js` module, raw.

**Size** (the bench's C arm at cdom.mk's -Oz, `brotli -q 11`, the module linked against the same app without it; scalar):

- Z1: the image module adds 26 to 32 KB brotli to an app (raceclub's PR measured 29.5 KB at -Oz with SIMD).
- Z2: the JPEG half 22 to 27 KB and the PNG half (with tinfl) 4 to 7 KB, each measured by an app that calls only that format's internal entry point (`cd_image_jpeg_resize` or `cd_image_png_resize`, `cdom_image_int.h`), so the linker drops the other.

**Time** (the bench, Chrome headless on this Mac, median of 9 interleaved rounds, box 900, scalar wasm):

| input | C (cd_image_resize) | B1 (createImageBitmap, resize options) | B2 (createImageBitmap, drawImage scaled) |
| --- | --- | --- | --- |
| p12.jpg | 110 to 250 ms | 40 to 150 ms | 60 to 200 ms |
| p12p.jpg | 250 to 600 ms | 80 to 300 ms | 100 to 350 ms |
| p48.jpg | 300 to 800 ms | 120 to 500 ms | 200 to 700 ms |
| p12.png | 250 to 600 ms | 150 to 450 ms | 150 to 450 ms |

Falsifier for each cell: a median outside its range.
The C arm is scalar wasm against the browser's native SIMD decoders, so C is predicted slower on every input, by 1.5 to 3 times on JPEG.

**Memory** (the C arm's wasm memory after one run on a fresh page, which is its peak since wasm memory never shrinks; the file's bytes are in it):

| input | file | predicted wasm memory |
| --- | --- | --- |
| p12.jpg | its size | the file plus 3 to 6 MB |
| p12p.jpg | its size | the file plus 38 to 43 MB (36.6 MB of coefficients at 4:2:0) |
| p48.jpg | its size | the file plus 3 to 6 MB (decoded at 1/8) |
| p12.png | its size | the file plus 2.5 to 4 MB |

Falsifier: a measured value outside its range.

**The PNG checksum**: checking the adler32 costs nothing measurable, since tinfl computes it whenever it parses a zlib header; the check only reads the stream's last bytes.
Prediction A1: `p12.png` with the check and with the stream's end not read differ by under 1%.
Falsifier: 2% or more.

## Results

### The spike (bench/image/sjlj.mjs)

`cd_image_resize` on `p12.jpg` to a 900 box, scalar wasm at -Oz, Node 26 (V8), 21 interleaved rounds, a fresh instance each, 1-minute load 2.8 to 2.9:

| way | median ms | min | max | one fatal error |
| --- | --- | --- | --- | --- |
| js (the explicit guard, the default) | 59.5 | 59.3 | 62.4 | 0.01 ms |
| eh (wasm exception handling) | 59.5 | 59.3 | 62.3 | 0.01 ms |
| ems (emscripten's lowering, bench only) | 59.4 | 59.2 | 62.3 | 0.01 ms |

- S1 and S2: confirmed; the three are within 0.2%, so js stays the default.
- S3: confirmed (0.01 ms an error; the heap and the stack pointer checked by tools/alloc.mjs, tools/image.mjs, tests/unit and tools/legacy.mjs, the last on wasm2js too).
- S4: confirmed; the eh module is 123 bytes larger raw, 37 brotli.

### Size (bench/image/sizes/run.sh, -Oz, stripped, scalar)

| build | raw | brotli |
| --- | --- | --- |
| no image code | 2,696 | 1,353 |
| both formats, js | 82,563 | 28,935 |
| both formats, eh | 82,686 | 28,972 |
| JPEG only | 72,205 | 24,515 |
| PNG only | 13,912 | 6,420 |

- Z1: the module adds 27.6 KB brotli (79.9 KB raw); confirmed (26 to 32).
- Z2: the JPEG half adds 23.2 KB and the PNG half 5.1 KB; both confirmed.
  A PNG-only app would save 22.5 KB brotli with a split module; a JPEG-only app 4.4 KB.
- The shim's two functions: 151 bytes minified, 50 brotli, on every page.

### The bench (bench/image/run.mjs)

Chrome headless on this Mac, 9 interleaved rounds, a fresh page per run, box 900, scalar wasm at cdom.mk's -Oz, 1-minute load 3.3 to 3.7 before every run; the raw runs are in bench/image/results/2026-11-19-box900.tsv.

| input | C-js ms | C-eh ms | B1 ms | B2 ms | prediction C | B1 | B2 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p12.jpg | 58.9 | 58.9 | 61.9 | 68.6 | 110 to 250: refuted (faster) | 40 to 150: confirmed | 60 to 200: confirmed |
| p12p.jpg | 139.1 | 139.8 | 122.1 | 126.7 | 250 to 600: refuted (faster) | confirmed | confirmed |
| p48.jpg | 170.0 | 170.3 | 214.0 | 263.3 | 300 to 800: refuted (faster) | confirmed | confirmed |
| p12.png | 241.6 | 240.1 | 131.1 | 137.6 | 250 to 600: refuted (slightly faster) | 150 to 450: refuted (faster) | 150 to 450: refuted (faster) |

- "C slower on every input, 1.5 to 3 times on JPEG" is refuted: scalar C beats both browser arms on baseline JPEG (5% faster than B1 at 12 MP, 21% at 48 MP), since libjpeg decodes at 1/4 and 1/8 and the browser decodes, then scales.
- The browser is ahead on progressive JPEG (14%) and on PNG (1.8 times), where no reduced decode exists; these are where SIMD (later PRs) has most to give.
- js and eh: the same within the spread on every input.

Wasm memory after one run on a fresh page (its peak, the file's bytes in it):

| input | file | wasm memory | beyond the file | prediction |
| --- | --- | --- | --- | --- |
| p12.jpg | 3.6 MiB | 6.1 MiB | 2.5 MiB | 3 to 6 MB: refuted (less) |
| p12p.jpg | 3.4 MiB | 40.8 MiB | 37.4 MiB (39.2 MB) | 38 to 43 MB: confirmed |
| p48.jpg | 14.6 MiB | 17.1 MiB | 2.5 MiB | 3 to 6 MB: refuted (less) |
| p12.png | 14.8 MiB | 17.4 MiB | 2.6 MiB | 2.5 to 4 MB: confirmed |

The review's item 7 is answered: on main's heap a 12 MP baseline JPEG needs its file plus 2.5 MiB, a progressive one its file plus 37.4 MiB, against raceclub's 59 and 63 MB on the old heap.

### Tests

- tests/image/run.sh: the vendored tree made again from the archives and equal; 9,182 native checks (ASan, UBSan), every image equal to the exact integer oracle to the byte, JPEG against djpeg 3.2.1 with SIMD off at the same scale; tools/image.mjs: the 65 cases in Chrome both ways, equal to the byte, 300 errors in a row, NO_ROOM on a capped heap and on modules whose memory.grow refuses (`--max-memory`), both ways, and demo/photo driven as a user picks a file.
- The fuzz targets, 300 s each: no crash, leak or limit (JPEG 4,213 runs, PNG 34,147).
  With the budget taken out, the item-1 file (a 8000 x 8000 baseline whose first scan has one component) stops the JPEG target at once: libFuzzer's out-of-memory on a malloc of 128,000,047 bytes; with it, the file is TOO_BIG.

## Review of raceclub PR #10

How this was checked: I read every file in src/photo, test/photo, fuzz and bench/resize/lowpeak, the PR body, and raceclub main's PERF.md, RESIZE-BENCH.md and tools/resize_inputs.py.
On the cdom side I read cdom.mk, src/libc, cdom_file.h/.c, cd_encode_image, file-read-design.md, tools/alloc.mjs, tests/link/run.sh and the `file-fit` branch.
I also did three small things beyond reading:
- Downloaded both upstream archives to scratch, checked their sha256 and diffed them against the vendored files.
- Compiled a 5-line setjmp test file with cdom's clang (Homebrew 23.1.5) to see which runtime symbols it expects. Nothing was built from the PR and nothing was benchmarked.
- Removed the read worktree afterwards; raceclub is untouched.

Labels: [V] means verified by reading or a direct check. [S] means suspected and not run.

### 1. Correctness and safety, most serious first

1. **[V] The 64 MP baseline cap can be bypassed, so a small file can make libjpeg allocate hundreds of MB.**
   - Where: `photo_jpeg.c:32-41, 55` treats only SOF2 as "holds every coefficient". In libjpeg, `jdinput.c:153` sets `has_multiple_scans` whenever the first scan has fewer components than the frame, and `jdmaster.c:680` then buffers the whole image.
   - Failing input: a 3-component SOF0 at 8000x8000 whose first scan has 1 component.
   - Result: a 4:4:4 file needs about 3 x 128 MB = 384 MB, a 4:2:0 file about 192 MB.
   - On cdom main `malloc` traps when memory cannot grow, so in the browser this kills the module rather than returning NOMEM.
   - The fuzzer cannot see it: its `-rss_limit_mb=2048` is far above this.
   - Fix: set `cinfo->mem->max_memory_to_use` to a budget after creating the decoder. Then libjpeg's no-disk memory layer (`jmemnobs`) fails with an ordinary error (`JERR_NO_BACKING_STORE`) instead of allocating. This also gives a real memory budget in place of pixel caps.
2. **[V for the arithmetic, S for impact] The 16 MP progressive cap assumes 4:2:0.**
   - Coefficients take 3 B/px at 4:2:0, 4 B/px at 4:2:2 and 6 B/px at 4:4:4.
   - A 16 MP progressive 4:4:4 file needs about 96 MB of coefficients plus the file, which is over the "stays under round 1's 101 MB" claim in `photo.h:28-32`.
   - Every bench and cap input is 4:2:0 (`resize_inputs.py:94`, `subsampling=2`).
   - The memory budget from item 1 fixes this too.
3. **[S, high] No limit on the number of progressive scans (a CPU denial of service).**
   - libjpeg-turbo has no built-in limit; its own tools and TurboJPEG add one through a progress monitor.
   - Failing input: a 16 MP progressive file made of thousands of tiny scans. Each scan walks about 250k blocks, so a 10 MB file can run for minutes on the main thread.
   - Fix: a progress monitor that raises an error once `input_scan_number` passes about 500.
4. **[V] Leak when out of memory inside `jpeg_create_decompress`.**
   - Where: `photo_jpeg.c:130-131` sets `have_cinfo = 1` only after create returns. If the first allocation inside `jinit_marker_reader` fails, `cinfo->mem` already exists and the error path never calls `jpeg_destroy`.
   - The OOM test misses it because it fails only the Nth allocation once, and libjpeg retries a failed pool allocation with a smaller size (`jmemmgr.c:310-323`). A heap that stays out of room (which is what `cd_alloc_try` gives) leaks.
   - Fix: always call `jpeg_destroy_decompress`. It is safe because create sets `mem = NULL` first and destroy checks it (`jcomapi.c:209`).
5. **[V] The wasm setjmp runtime does not match cdom's compiler, so it cannot link.**
   - `photo_sjlj_wasm.c` implements the LLVM 18 interface (`saveSetjmp`, `testSetjmp`, `getTempRet0`).
   - clang 23 emits calls to `__wasm_setjmp(env, label, id)`, `__wasm_setjmp_test(env, id)` and `__wasm_longjmp`; I confirmed this by listing the symbols of the test object.
   - Also, `__wasm_longjmp` passes a pointer to a stack local in the throwing frame. It works only because the catch block reads it right after restoring the stack pointer. wasi-libc stores these values in the `jmp_buf` instead; do the same.
6. **[V] Out-of-memory is unreachable in wasm on cdom main.**
   - `photo_*` and `jmemnobs.c` call `malloc`, which traps on cdom's heap (`cdom.h:81`).
   - The NOMEM status, the OOM tests and the "no leak" claims only hold natively.
   - Fix: send all allocations through `cd_alloc_try` (a cdom-owned replacement for `jmemnobs.c`).
7. **[V] The wasm memory figures are stale.**
   - The 59, 63, 113 and 38.1 MB figures and the cap reasoning ("power-of-two heap makes 24 MP jump to 113 MB") were measured on cdom b59e476.
   - cdom main now gives large blocks at a 4 KiB grain (`file-read-design.md`, P12/P24), so they need re-measuring.
8. **[V, low] `long_side == 0` returns OK with a 0-pixel image.**
   - `photo_target` (`photo_scale.c:6-16`) gives `dw = 0`, the scaler runs, and the caller gets `w = 0`.
   - A large `long_side` with a 64 MP file asks for a 256 MB output. The API should bound `long_side`.
9. **[V, low] PNG colour-key transparency is ignored for grey and RGB.**
   - `photo_png.c:171` reads `tRNS` only for palette images, so the key colour is not composited on white. The oracle has no such case.
10. **[V, low] Smaller inconsistencies.**
    - `photo_probe` on a JPEG does not apply the side limit, so a 65,501 to 65,535 side probes OK while resize says TOO_BIG.
    - The PNG adler32 checksum is computed but in effect never checked (`photo_png.c:200` breaks before looking at the status). The README says it is not checked, but the CPU is still spent.
    - `photo.h:7` says "no global", but the wasm runtime has two (`temp_ret0`, `next_id`).
    - `emit()` steps its pointer one past the start of the buffer for negative steps (undefined behaviour in principle, harmless in practice).
    - The wasm bench build passes `-w` to the photo code as well as to libjpeg.

Checked and found sound:
- EXIF reader: 64-bit offset checks, entry count bounded by the segment, SHORT value read correctly in both byte orders.
- PNG: chunk walker bounds, PLTE no larger than 768 bytes, unfilter edge cases, palette index within 256, a decompression bomb is bounded by the header's rows, many empty IDAT chunks end.
- Scaler: integer ranges (32-bit across sum up to 65535x255; 64-bit down sum below 2^53 so the double division is exact) and all 8 orientations. A truncated JPEG ends as CORRUPT.

### 2. Third-party code

**Pinning and hashes [V].**
- Both sha256 values in the READMEs match the upstream release archives I downloaded:
  - libjpeg-turbo 3.2.1: `c27ad72a...cece7f`
  - miniz 3.1.2: `f0446d86...140d9a`
- No script is committed that re-checks them; the check exists only as a sentence in the README.

**libjpeg-turbo files vs upstream [V].**
- Byte-identical: every vendored `src/` file, `LICENSE.md` and `README.ijg`, except the three below.
- `jmorecfg.h`: 7 features switched off (fast and float IDCT, lossless, saved markers, merged upsampling, both colour quantizers).
- `jconfig.h` and `jconfigint.h`: written by hand.
- `jversion.h`: copyright-year template filled in.
- Also vendored though the decoder never uses them: `turbojpeg.h` (2,904 lines), `transupp.h`, `tjutil.h`, `jpegapicomp.h`.

**tinfl [V].**
- `tinfl.c` is miniz.c lines 2278 to 3007 plus the licence banner.
- One edit is not documented: the opening `extern "C" {` block was removed and a closing `#endif` added. Harmless in C.
- `tinfl.h` is miniz.h lines 911 to 1056 plus hand-copied typedefs.

**Licences, and what they ask of every app.**
- libjpeg-turbo is under the IJG licence plus 3-clause BSD. A wasm binary counts as executable distribution, so each app's documentation must say "based in part on the work of the Independent JPEG Group" and reproduce the BSD notice.
- miniz is MIT and needs its notice too.
- cdom would need to document these obligations for apps.

**Size** (the PR's numbers, measured with clang 18 at -Oz with simd, not by me):

| Part | Brotli bytes |
|---|---|
| Both decoders | +29.5 KB |
| JPEG | +24.9 KB |
| PNG with tinfl | +5.3 KB |
| Shared | -0.7 KB |

Raw byte sizes are not reported. Against round 1's stb decoder (12.9 KB) the net cost is +16.6 KB.

### 3. libc and header friction, and what cdom would need

| Friction | What cdom would need |
|---|---|
| No `stdlib.h` (the app-side shim declares `malloc`, `realloc`, `free` and defines `abs` as a macro) | `src/libc/stdlib.h` declaring `malloc`, `calloc`, `realloc` and `free` (cdom.c already defines them), plus `abs` and `labs` as real inline functions. No `getenv`. |
| No `FILE` (`jpeglib.h` names it in `jpeg_stdio_src` and `jpeg_stdio_dest`) | One incomplete `typedef struct cd_no_file FILE;` in `src/libc/stdio.h`, with no functions. |
| `errno.h` | Not needed while the module builds with `-DNO_GETENV -DNO_PUTENV`. Keep those flags in the module, not in cdom's libc. |
| `limits.h`, `stdint.h`, `stddef.h` | Already provided by clang. |
| 12-bit entry points (`j12init_*`) | Six stubs owned by the module (`photo_stubs.c`). |
| Host compiler on Linux | Already fixed on cdom main at 7279030. |
| One compile command for everything (`cdom_mk_app_src` maps each module name to exactly one `src/cdom_<name>.c`, and `APP_CFLAGS` apply to every file) | A per-module source list (`CDOM_<NAME>_SRC` with many files) and per-module flags compiled as separate objects. Then vendor code gets `-w` and its `-I`, and only the one file that calls setjmp gets the exception-handling flags. Today an app with `-Wall -Werror` cannot link libjpeg, and libjpeg's headers (`jconfig.h` and the like) would leak onto the app's include path. |
| `setjmp.h` | See the setjmp section below. |

**setjmp under wasm exception handling.**
- What cdom would provide:
  - A `src/libc/setjmp.h` with a `jmp_buf` laid out as wasi-libc's.
  - Declarations for `setjmp` and `longjmp`.
  - A runtime with `__wasm_setjmp`, `__wasm_setjmp_test` and `__wasm_longjmp` that keeps its arguments in the `jmp_buf`.
  - That runtime lives in the image module's own file. It must not go in `mini.c`, because `__builtin_wasm_throw` would put exception-handling instructions into every app.
- Flags: `-mexception-handling -mllvm -wasm-enable-sjlj`, on that one file only.
- Pin legacy exception handling with `-mllvm -wasm-use-legacy-eh=true`. clang 23 still defaults to legacy try/catch; I confirmed the output, and `=false` emits the newer `try_table` form.
- Browser floors:
  - Legacy exception handling: Chrome 95, Firefox 100, Safari 15.2.
  - The newer form: Chrome 137, Firefox 131, Safari 18.4.
- Below those floors the whole module fails to validate, not just the photo code. raceclub's plan floor of Chrome 91 and Firefox 89 is not met.
- [S] wasm2js (cdom's no-WebAssembly tier) does not translate exception handling, so that tier is lost for any app that links the module. cdomBoot only checks that WebAssembly exists, so it would not fall back.

**Can libjpeg's error path avoid longjmp?**
- **An error manager that sets a flag and returns:** unsafe as it stands. libjpeg keeps running on state that was never built; the PR's own mutant showed an out-of-bounds index at `jdmarker.c:534`. Making it safe means patching 169 error sites in the 20 decoder files, and every caller of each, with the patch redone on every upgrade. Not recommended.
- **Emscripten-style JS setjmp** (`-mllvm -enable-emscripten-sjlj`): calls that may longjmp go through JS trampolines imported from the shim, wrapped in try/catch. It works on plain WebAssembly and with wasm2js. Costs: shim code, an exported function table, and one JS-to-wasm round trip per `jpeg_read_scanlines` row (an estimated few ms for 8,000 rows). Needs a spike before committing to it.
- **Arena plus trap:** route libjpeg's memory to an arena, trap on error, catch it in the shim, then reset the stack pointer and the arena. It works everywhere, but the decode has to become its own export rather than running inside `cd_file_got`. Fragile.
- **A different decoder:** tjpgd decodes baseline only, streams MCU by MCU, scales 1/2 to 1/8 and returns error codes. It could be paired with the browser path (`cd_file_fit`) for progressive files. Wuffs uses return codes but cannot scale the DCT, so it needs the full 48 MB pixel buffer.

### 4. Test quality

What is good:
- The PNG and scaler oracle is independent: numpy's exact area average and its own PNG writer covering every filter, colour type and bit depth.
- The 57 mutants are targeted, and each names the assertion it expects to fail.
- Refusal, leak and peak checks run through a counting allocator.

Weaknesses:
- **Tolerance:** the scaler is exact in integers, yet the tests allow off-by-1 per channel plus a mean bound, because the float64 oracle lands either side of exact ties. A rounding bug touching under 2% of pixels survives. Use an exact integer oracle (Python ints) and require a difference of 0.
- **JPEG oracle:** it is Pillow, i.e. the same libjpeg-turbo family, so it checks the plumbing, not the decoder. Scales Pillow cannot make (only 1/2, 1/4 and 1/8) are checked only against a PSNR floor of 28 to 38 dB; a single case covers a non-power-of-two scale (3/8).
- **OOM model:** it fails one allocation and lets the next succeed, which libjpeg's retry absorbs. This is why item 4 was missed.
- **Peak tests:** they count native malloc'd bytes, not wasm memory.
- **Missing tests:**
  - A baseline JPEG with non-interleaved scans.
  - A 4:4:4 progressive file at the cap.
  - A many-scans progressive file.
  - `long_side = 0` or a huge value.
  - Colour-key `tRNS` for grey and RGB.
  - Palette indexes beyond PLTE.
  - EXIF after an XMP APP1 segment.
  - 4:4:0 or 4:1:1 subsampling.
  - The wasm and setjmp path, beyond one manual Chromium run.
- **Fuzzing:** native only, 10 minutes per target, 200 KB maximum input, 2 GB memory limit. It cannot see items 1 to 3, and the wasm runtime is never fuzzed.

### 5. Randomness, JSON and time

- Shipped C: none of the three.
- `oracle.py` says "one random source", but it also seeds three `random.Random` generators (`ri.SEED`, `+1`, `+2`) through `resize_inputs`. They come from one named seed but are a second generator type. Test tooling only.
- `run.mjs:29,32` uses `Date.now()` for the bench driver's 25-minute deadline (not shipped).
- `app.c` uses `cd_now()` for timings (bench only).
- No JSON anywhere in the PR.
- If vendored into cdom, `third_party` will be scanned by cdom's seed gate; I found no seed or random names in it.

### 6. Proposed cdom shape (the manager decides)

**Recommendation: an `image` module.**
- Header `src/cdom_image.h`, turned on with `APP_MODULES += image`.
- Functions:
  - `uint32_t cd_image_probe(const uint8_t *p, uint32_t len, cd_image_info *info)`.
  - `uint32_t cd_image_fit(const uint8_t *p, uint32_t len, uint32_t max_w, uint32_t max_h, uint8_t **rgba, uint32_t *w, uint32_t *h)`.
- Use the same `max_w`/`max_h` box as `cd_file_fit` on the `file-fit` branch, so an app can swap the browser path and the C path.
- Statuses: `CD_IMAGE_OK`, `NOT_IMAGE`, `TOO_BIG`, `UNSUPPORTED` (interlaced and 16-bit PNG folded in), `CORRUPT`, and `NO_ROOM` (named after `CD_FILE_NO_ROOM`). The user-facing text stays in the app.
- The flow: `cd_file_read_cb`, then `cd_image_fit` in the callback, then `cd_encode_image`, then `cd_free`.
- Vendor code in `cdom/third_party/libjpeg-turbo-3.2.1/` and `cdom/third_party/miniz-3.1.2/`, trimmed to what is compiled.
- A committed fetch script that checks the sha256, extracts the exact file list and applies the documented edits, plus a test that diffs the result, so "unmodified" is enforced rather than claimed.
- Fold in the fixes for items 1 to 6: memory budget, scan limit, always destroy the decoder, `cd_alloc_try`, new runtime interface, bounded size.

**PR split:**
1. cdom libc and make: `stdlib.h`, opaque `FILE`, per-module multi-file sources and per-module flags in `cdom.mk`, with tests/link cases.
2. The setjmp decision: once the browser-floor decision is made (Alex's call, item 3), `setjmp.h` plus the runtime under the image module, with a link test proving that apps without the module carry no exception-handling instructions.
3. Vendor code plus the image module plus the ported tests, oracle, mutants and fuzz targets, with the fixes above and wasm memory re-measured on main's heap.
4. raceclub switches to cdom's module, in its own repo.

**Alternatives:**
- Keep the decoders in raceclub and have cdom take only PR 1 and the setjmp support (smallest cdom surface).
- Fold the C path into `cd_file_fit` itself, with no RGBA exposed (less flexible).
- Split into `image_jpeg` and `image_png` modules, so a PNG-only app does not pay about 25 KB brotli for JPEG.
