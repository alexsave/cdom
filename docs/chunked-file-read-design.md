# Chunked file reading: the image decoders never hold a picked file whole

A design, written 2026-11-19.
Nothing in src/ or shim/ is changed by it.
A throwaway native prototype (bench/chunked/) answers the questions that were cheap to answer; its predictions were committed before it was built (bench/chunked/PREDICTIONS.md, commit dd23b94).
Every number below is either a measurement, with the command that made it, or a prediction, with its falsifier.

The machine for every measurement here: a Linux x86-64 cloud VM (4 cores of an "Intel Xeon @ 2.30GHz"), Ubuntu clang 18.1.3 (native and wasm32), headless Chromium 153.0.8010.12 through tools/cdp.mjs.
These figures are indicative only; the cdom manager re-measures on Alex's Mac.
Timing on this VM spreads by about 20% from run to run, so where a small difference matters the design leans on instruction counts (valgrind's cachegrind), which the noise does not move.

## The decision, in short

- One resumable decoder: C hands the image module the file's bytes in order, a piece at a time, and the module decodes as far as each piece goes.
  The same decoder runs on the main thread (one piece per event) and inside a cd_job worker (pieces pumped by the worker's few lines of JS).
- The C API is a push of pieces into room the decoder names: `cd_image_begin`, `cd_image_room`, `cd_image_put`, `cd_image_take`, `cd_image_end`.
  The file side is a sequential reader: `cd_file_open`, `cd_file_next_cb`, `cd_file_close`.
  A short explicit glue, `cd_image_file_cb`, ties the two for an app that wants one call.
- The shim reads with `file.stream()` (measured: the cheapest way, cheaper even than today's whole read), fills C's room up to 1 MiB before answering, and keeps one stream read in flight ahead.
- JPEG uses libjpeg-turbo's suspending source; PNG uses a chunk walker that takes any split.
  The prototype decodes every test input to the same bytes as today's whole-file decode, at pieces of 1, 7, 4093, 65536 and 262144 bytes.
- Pieces are 1 MiB: the measured extra work is 0.1% of instructions on a 64 MP baseline JPEG (against 1.5% at 256 KiB), and the shim's cost per piece falls with size.
- Predicted wasm memory after one photo on the main thread: 3.4 to 4.3 MiB for the 15 MB PNG and the 64 MP baseline JPEG, against 17.1 and 21.4 MiB today (measured).
  The main-thread freeze becomes one task per piece, predicted 12 to 35 ms on this VM, against one task of 234 to 277 ms today.

## What is held today (measured)

`bench/image/run.mjs 5 900 C-js p12.jpg,p12p.jpg,p64.jpg,p12.png` on a fresh page per run (results: bench/chunked/results/wasm-today.tsv).
p64.jpg is new: 9216 x 6912 (63.7 MP, under the 64 MP cap), baseline 4:2:0 at quality 90, the bench's own scene (bench/chunked/p64.py, 19,834,192 bytes).

| input | file | wasm memory after one decode | beyond the file | decode, median of 5 |
| --- | --- | --- | --- | --- |
| p12.jpg (12 MP baseline) | 3.6 MiB | 6.1 MiB | 2.5 MiB | 77.8 ms |
| p12p.jpg (12 MP progressive) | 3.4 MiB | 40.8 MiB | 37.4 MiB | 234.1 ms |
| p64.jpg (64 MP baseline) | 18.9 MiB | 21.4 MiB | 2.5 MiB | 257.5 ms |
| p12.png (12 MP RGB) | 14.5 MiB | 17.1 MiB | 2.6 MiB | 277.0 ms |

Outside progressive JPEG the file is 60% to 88% of the memory.
The decode time above leaves out the read: the bench copies the bytes in before it starts the clock.
cd_file_read's own read of the file adds 17.2 to 19.4 ms (measured below, "whole").

## 1. The C API

### The choice: a push of pieces into room the decoder names

Three shapes were weighed.

- **A pull callback, `read(ctx, dst, max)`, answered at once.**
  The decoders barely change (libjpeg's ordinary source, png-fast's `more`), and the decode stays one call.
  But a browser has no synchronous read of a File on the main thread: only a worker has one (`FileReaderSync`).
  So it would serve the worker only, and there it costs more: reading 15 MB through `FileReaderSync` in 1 MiB slices added 48.3 ms to a stand-in decode, against 20.2 ms for an async stream in the same worker (bench/chunked/results/pieces-p12png.txt, "overlap").
  JSPI (a wasm call that waits on a promise) would lift the main-thread limit, but it is not in every browser cdom serves, and never in wasm2js.
  Rejected.
- **Offset and length (`read_at(at, dst, len)`), pushed or pulled.**
  It lets a decoder skip a segment without reading it, but every skip in these two formats is small (a JPEG segment is at most 65,535 bytes; PNG's ancillary chunks are usually small), and offsets force `File.slice`, which the browser serves at about twice the per-byte cost of a stream (below).
  Rejected: the decoders count skipped bytes and drop them as they arrive.
- **A push of pieces, in order, into room the decoder names: chosen.**
  The decoder says where the next bytes go and how many it can take; the caller puts them and the decoder decodes as far as they go, then says again.
  Both decoders can be fed this way (libjpeg's suspension, tinfl's `HAS_MORE_INPUT`), it serves the main thread and the worker alike, and the bytes cross into C once, straight into the decoder's own buffer.

### cdom_image.h: a decoder that is fed

```c
#define CD_IMAGE_UNREADABLE 7 /* cd_image_file_cb only: the file could not be read to its end (gone, changed, cut short) */
#define CD_IMAGE_MORE 8       /* cd_image_put: not done; ask cd_image_room again */

typedef struct cd_image_dec cd_image_dec;   /* opaque, in cdom's heap */

/* A decoder for a file of size bytes, to fit max_w x max_h as cd_image_resize does. OK, BAD_BOX or NO_ROOM. */
uint32_t cd_image_begin(uint32_t size, uint32_t max_w, uint32_t max_h, cd_image_dec **d);
/* Where the next bytes of the file go, and *max, the most it takes now (at most CD_IMAGE_PIECE).
 * *max 0: it has work that needs no bytes; call cd_image_put(d, 0). */
uint8_t *cd_image_room(cd_image_dec *d, uint32_t *max);
/* n bytes (0 to *max) were written at the room. Decodes as far as they go and answers MORE, or the final status,
 * which is cd_image_resize's: OK, NOT_IMAGE, TOO_BIG, UNSUPPORTED, CORRUPT or NO_ROOM. After a final status,
 * put is not called again. */
uint32_t cd_image_put(cd_image_dec *d, uint32_t n);
/* After OK: the image, as cd_image_resize answers it (the caller frees *rgba with cd_free). */
void cd_image_take(cd_image_dec *d, uint8_t **rgba, uint32_t *w, uint32_t *h);
/* Frees everything the decoder holds. Any time, in any state: this is how a decode is cancelled. */
void cd_image_end(cd_image_dec *d);

#define CD_IMAGE_PIECE (1u << 20)
```

- The end of the file is known from `size`: once `size` bytes went in, the decoder treats any further want as the file ending there (libjpeg's two-byte EOI, as the memory source does today; PNG's stream cut short), so the statuses are today's.
  A reader that ends early is the reader's error (UNREADABLE), not the image's, so it never reaches put.
- `cd_image_put(d, 0)` with `*max` 0 is how a long stretch that needs no input (a progressive JPEG's output pass) is cut into steps: the decoder stops every 64 output rows and answers MORE with no room wanted.
- `cd_image_resize(p, len, ...)` and `cd_image_probe` stay as they are for bytes already in memory (an HTTP answer, a test).
  `cd_image_resize` becomes `begin`, one internal put of the whole buffer used where it lies (no copy: the decoder's source points at the caller's bytes when the room is empty and the bytes are all of the rest of the file), `take` and `end`, so there is one decoder and today's callers see the same bytes and the same statuses.
- No globals, as today: every state is in the `cd_image_dec` block.

### cdom_file.h: a reader in order

```c
/* A reader on file index of input's files list: *reader for cd_file_next_cb, *size the file's size.
 * Answers OK, NO_FILE, UNSUPPORTED (no FileReader, the IE 9 tier) or TOO_BIG (4 GiB or more), at once: it reads
 * nothing, only looks at the list, as cd_get_num(input, "files.0.size") does. The shim keeps the File itself,
 * so picking another file in the input does not change what an open reader reads. */
uint32_t cd_file_open(cd_id input, uint32_t index, uint32_t *reader, uint32_t *size);
/* The next bytes, at most max, written at dst; then fn(ctx, status, n) from the event loop:
 *   OK, n 1 to max: n bytes at dst (fewer than max only for the last piece);
 *   OK, n 0: the end, all size bytes were given;
 *   UNREADABLE: the browser could not read on, or the file ended before its size (it changed on disk); nothing more.
 * dst stays the caller's until the answer or cd_file_close. One next at a time per reader: a second traps. */
typedef void (*cd_file_piece_fn)(void *ctx, uint32_t status, uint32_t n);
void cd_file_next_cb(uint32_t reader, uint8_t *dst, uint32_t max, cd_file_piece_fn fn, void *ctx);
/* Ends the reader, any time. After it returns nothing is written at dst and fn is not called: the shim drops the
 * File and cancels its stream, and answers the pending callback's token, which cdom_file.c frees (cd_cb_take). */
void cd_file_close(uint32_t reader);
```

`cd_file_open` answers at once, which bends cd_file_read's rule that every answer comes from the event loop.
It reads nothing, so there is nothing to wait for, and the alternative (an open callback) adds an event and a state to every user for no gain; it is open question 4.
`cd_file_read` and `cd_file_read_cb` stay: a small file read whole (a JSON-free settings file, a CSV) is still one call.

### The glue: one call for an app

```c
/* The picked file decoded and fitted, read a piece at a time: fn(ctx, status, rgba, w, h) once, from the event
 * loop, with cd_image_resize's statuses plus UNREADABLE, which also stands for cd_file_open's NO_FILE, TOO_BIG and
 * UNSUPPORTED (no file to read). *job names it for cd_image_file_stop. */
typedef void (*cd_image_file_fn)(void *ctx, uint32_t status, uint8_t *rgba, uint32_t w, uint32_t h);
void cd_image_file_cb(cd_id input, uint32_t index, uint32_t max_w, uint32_t max_h, cd_image_file_fn fn, void *ctx,
                      uint32_t *job);
/* Cancels it: the reader closed, the decoder ended, fn never called. */
void cd_image_file_stop(uint32_t job);
```

It lives in its own file, src/cdom_image_file.c (linked when an app has both `file` and `image`), and is about 50 lines of explicit calls with no hidden wiring:

```c
static void step(void *v, uint32_t st, uint32_t n) {
  job *j = v;
  if (st != CD_FILE_OK) { finish(j, CD_IMAGE_UNREADABLE); return; }
  uint32_t s = cd_image_put(j->d, n);
  if (s != CD_IMAGE_MORE) { finish(j, s); return; }
  uint32_t max;
  uint8_t *at = cd_image_room(j->d, &max);
  if (max) cd_file_next_cb(j->reader, at, max, step, j);
  else cd_timer_cb(0, again, j);   /* work with no bytes: yield, then cd_image_put(d, 0) */
}
```

An app that needs more (a progress bar from the bytes taken, its own read source) writes these lines itself against the two APIs.

### Errors, the end, cancel

| what | where it shows | status |
| --- | --- | --- |
| the read fails (file gone, permission) | the shim's stream rejects | the piece answers UNREADABLE; the glue ends the decoder and answers UNREADABLE |
| the file is shorter than its size (changed on disk) | the stream ends before `size` bytes | UNREADABLE, as above (the prototype's "short file" case) |
| the file is cut short but its size says so | the decoder runs out at `size` | CORRUPT, as today (the prototype compares the status to whole's at every cut tested) |
| a damaged image | the decoder | CORRUPT, at the piece where libjpeg or the walker sees it |
| over a limit | the header walk, before libjpeg allocates | TOO_BIG or UNSUPPORTED, as today's probe |
| cancel | the app calls `cd_image_file_stop` (or `cd_file_close` and `cd_image_end` itself) | no answer; the heap is back where it was (the prototype checks it after cancelling at the third piece) |

## 2. The shim

Measured in headless Chromium 153 (bench/chunked/pieces.mjs, a real file picked from disk through CDP `DOM.setFileInputFiles`, 5 rounds, medians; results in bench/chunked/results/):

| way | p12.png, 15.2 MB: total ms (pieces) | p64.jpg, 19.8 MB | per piece |
| --- | --- | --- | --- |
| whole (`FileReader`, today) | 17.2 (1) | 19.4 (1) | |
| `slice().arrayBuffer()`, 64 KiB | 159.1 (232) | 215.7 (303) | 0.69 to 0.71 ms |
| `slice().arrayBuffer()`, 256 KiB | 54.6 (58) | 77.6 (76) | 0.94 to 1.02 ms |
| `slice().arrayBuffer()`, 1 MiB | 32.6 (15) | 45.6 (19) | 2.2 to 2.4 ms |
| `FileReader` on slices, 1 MiB | 33.6 (15) | 38.9 (19) | 2.0 to 2.2 ms |
| `file.stream()` reader | 14.1 (229 pieces, at most 128 KiB) | 19.9 (265, at most 320 KiB) | 0.06 to 0.08 ms |
| `FileReaderSync` on slices in a worker, 1 MiB | 41.6 (15) | 45.2 (19) | 2.4 to 2.8 ms |

And with a stand-in decode on the reading thread (a busy loop of 30 ms per MiB taken, roughly this VM's wasm decode rate), the next piece asked for before the current one is decoded:

| way | p12.png: added by reading | p64.jpg |
| --- | --- | --- |
| whole, then decode (today) | 17.0 ms | 22.0 ms |
| slices of 1 MiB, one ahead | 28.6 ms | 28.8 ms |
| stream, one read ahead | 13.1 ms | 15.8 ms |
| in a worker, `FileReaderSync`, 1 MiB | 48.3 ms | 59.8 ms |
| in a worker, stream, one ahead | 20.2 ms (with the worker's start) | 21.2 ms |

So:

- The shim reads with `file.stream().getReader()`, one `read()` in flight ahead of C.
  The browser picks the chunk sizes (128 to 448 KiB seen in Chromium); they live in the JS heap until copied, never in wasm memory.
- Each `cd_file_next_cb` is answered when `max` bytes are in C's room or the file has ended: the shim copies stream chunks into `dst` as they come (a fresh view of the memory each time, as `bytes()` makes it, since memory may grow between), and keeps any rest of a chunk for the next call.
  So C gets 1 MiB pieces whatever the browser's chunk size, which is what keeps libjpeg's per-piece cost at 0.1% (section 4).
- The answer is delivered as its own task (the stream's own completion, or a posted message when the bytes were already in hand), never as a microtask chained onto the previous piece, so input and paint run between pieces.
- Where `Blob.prototype.stream` is missing (Safari before 14.1, the legacy tiers' browsers), the shim falls back to `FileReader` on 1 MiB slices: 2 ms a piece, about as fast as today's whole read (33.6 against 17.2 ms on the PNG, mostly hidden behind the decode).
  No `FileReader` at all (IE 9) is UNSUPPORTED at open, as today.
- The shim's new imports: `file_open`, `file_next`, `file_close`, and the job's pump (section 5).
  Prediction: shim/cdom.min.js grows by 700 to 1,100 bytes minified, 300 to 450 brotli (-q 11).
  Falsifier: outside either range by more than 30%.

## 3. PNG

The walk over pieces replaces today's walk over the whole file (`next_chunk` over `p, len`) with a state machine that takes any number of bytes:

- `HEAD`: a chunk's length and type, gathered in an 8-byte array across as many pieces as it takes.
  A chunk that runs past `size` ends the walk, exactly as `next_chunk` does today; this needs the size up front, which `cd_image_begin` has.
- `DATA`: an IDAT's bytes go straight from the piece to the inflate, wherever the piece ends; PLTE (at most 768 bytes) and tRNS (the first 256 kept) are gathered into small arrays and take effect at the chunk's end, before the first IDAT only, as today; every other chunk is counted past and never stored (a 200,000-byte tEXt costs no memory).
- `CRC`: four bytes counted past (not checked, as today).
- IHDR is the first 33 bytes, gathered the same way and checked by today's `read_ihdr`.

**tinfl** (main's inflate) is already resumable: it is called with `TINFL_FLAG_HAS_MORE_INPUT` on whatever part of an IDAT a piece holds, and its 32 KB window and the row assembly carry across calls as they carry across IDATs today.
The prototype does exactly this (bench/chunked/proto.c, `png_feed`), byte-equal at 1-byte pieces, so every split of every chunk is covered, including a split inside a length, a type, a CRC and a zero-length chunk.

**cdom's inflate** (png-fast, PR #56, not merged) pulls its input through `more(ctx, &p, &n)` and keeps its bit buffer, block state and sink in locals, so it cannot stop when a piece runs out.
It needs one change, whichever of the two lands second:

- The state that lives in locals between symbols (the 64-bit bit buffer and its count, the block's kind, a stored block's bytes left, the final-block flag, the adler32) moves into `cd_inflate`, beside the tables already there.
- Input is pushed: `cd_inflate_put(z, p, n, last)` decodes while at least `CD_INFLATE_LOOK` (1 KiB) bytes are ahead of it or `last` is set, and otherwise returns `CD_INFLATE_MORE` at a symbol or block boundary, leaving the unread tail (under 1 KiB) for the walker to put again in front of the next bytes.
  1 KiB covers the longest dynamic block header (at most 554 bytes) and the longest symbol with its extra bits (48 bits), so no header or symbol is ever cut and nothing inside them needs to resume.
- The fast loop is untouched (it already requires room for its worst case); the checked loop gains one compare per symbol, and runs only for the last 1 KiB of each piece.
- The walker keeps a carry of up to 1 KiB plus one IDAT's start, so the bytes it puts are contiguous across an IDAT boundary inside a piece.

Prediction: png-fast's p12.png in wasm, fed 1 MiB pieces, is within 1% of its whole-file time (median of 15 interleaved rounds).
Falsifier: 2% or more slower.

## 4. JPEG

libjpeg-turbo's suspending source works, with every part cdom uses, and the prototype shows it at every piece size down to one byte:

- `fill_input_buffer` returns FALSE when no more bytes are in; libjpeg then backs up to the start of the marker or MCU it was reading and returns: `jpeg_read_header` answers `JPEG_SUSPENDED`, `jpeg_start_decompress` FALSE, `jpeg_read_scanlines` 0.
  The next put calls the same function again.
  The decoder is a stage number (header, start, rows, done) in its state block, and each put runs `switch (stage)` from where it stopped (proto.c, `push_step`).
- The bytes from `next_input_byte` on are kept: before the next piece they move to the front of the decoder's buffer, and the room starts after them.
  libjpeg holds back at most one marker segment (65,535 bytes after FF and the code) or one MCU (a few KB), so the buffer is the piece plus 68 KiB (`JPEG_KEEP` in proto.c); a JPEG whose largest segment is the maximum (e-com.jpg, a 65,533-byte COM) decodes equal to whole at 1-byte pieces.
- `skip_input_data` past what is buffered (an APPn or COM libjpeg does not keep) counts the rest, and the next pieces pay it off before anything is kept.
- **The scaled decode**: `scale_num` and `scale_denom` are set between `jpeg_read_header` and `jpeg_start_decompress`, as today; suspension does not touch them.
  Every bench input decodes at its scale (p64.jpg at 1/8) equal to whole.
- **Baseline**: input is taken during `jpeg_read_scanlines`, a piece at a time; rows flow to the scaler as today.
- **Progressive** (and non-interleaved): `jpeg_start_decompress` absorbs every scan into the coefficient arrays, suspending at each piece's end; then the output pass reads no input (only the EOI at the end), and is cut into steps of 64 output rows (section 6).
  p12p.jpg, scans500.jpg and noninter.jpg decode equal to whole at 1-byte pieces.
  Reading the whole file only for progressive was considered and rejected: the coefficient arrays are the big part (37 MiB at 12 MP), the file would add 3.4 MiB to it, and suspension costs nothing measurable there (instructions +0.04% at 1 MiB).
- **The way out of a libjpeg error**: each put runs its libjpeg calls inside `cd_image_guard`, as the one call does today.
  Nothing of libjpeg's lives on the stack between puts (cinfo, the source and the scaler are in the heap block), so a throw from any put unwinds only that put's frames; the guard puts the stack pointer back (the js way) and the put answers CORRUPT, TOO_BIG or NO_ROOM, and `cd_image_end` runs `jpeg_destroy_decompress`.
  The prototype runs this with the native setjmp guard; the js and eh ways have the same contract (one guard per call, no state on the stack), and the test plan runs them in Chrome.
  The cost is one guard per put: about 20 for a 20 MB file, where today there is one.
- **The header walk**: today's probe and `cd_image_orientation` read the whole file in place.
  Here they become one incremental walker (the same rules: fill bytes, segment lengths checked against `size`, the first `Exif\0\0` APP1 up to SOS, the first SOF), fed every byte before libjpeg sees it.
  It keeps only the Exif segment (at most 64 KiB) and the SOF; it skips everything else by count.
  Because it sees each byte first, its verdict (UNSUPPORTED for SOF3 or 12-bit, TOO_BIG for the pixels) is in before libjpeg could fail on the same segment, and before `jpeg_start_decompress` allocates anything large, so the statuses stay today's.
  The prototype took a shortcut here (it read a head of 64 KiB, doubling, and probed that); e-icc-exif.jpg (Exif behind a 65,533-byte APP2) still decodes equal, but the walker is what the build would ship.

**Cost**: the prototype counts instructions natively (bench/chunked/count.sh, cachegrind; results/instructions.txt):

| p64.jpg, whole = 1,429,778,547 instructions | pull | push (suspending) |
| --- | --- | --- |
| 65,536-byte pieces | +2.06% | +2.11% |
| 262,144-byte pieces | +1.47% | +1.48% |
| 1,048,576-byte pieces | | +0.12% |

The suspending source costs what an ordinary pull source costs; the cost is per piece (about 340,000 instructions each), and comes from libjpeg-turbo's Huffman decoder taking its slow path for the last `512 x blocks_in_MCU` bytes before every refill (jdhuff.c, `usefast`), not from suspension.
That is why pieces are 1 MiB.

## 5. cd_job

cd_job (PR #50, branch job-worker, not merged) runs a job module in a throwaway worker; `cd_job_file_cb` posts the File to the worker, which streams it whole into the job's memory before `run`.
With pieces, the job takes the file the same way the main thread does, and neither the app's memory nor the job's ever holds it whole.

New, beside `cd_job_file_cb`:

```c
/* cdom_job.h: as cd_job_file_cb, but the job takes the file in pieces (its module defines the pieces_* entries). */
void cd_job_pieces_cb(const char *name, cd_id input, uint32_t index, const void *args, uint32_t args_len,
                      cd_job_fn fn, void *ctx);

/* cdom_job_mod.h: what such a job module exports, in place of run. */
#define CD_JOB_PIECES_MORE 0x100u   /* pieces_put: not done */
uint32_t pieces_begin(uint32_t size, const uint8_t *args, uint32_t args_len);   /* 0, or a CD_JOB_RUN_* failure */
uint8_t *pieces_room(void);   /* where the next bytes go */
uint32_t pieces_max(void);    /* the most it takes now; 0: call pieces_put(0) */
uint32_t pieces_put(uint32_t n);   /* CD_JOB_PIECES_MORE, or the job's end: CD_JOB_RUN_OK (it called cd_job_answer) or a failure */
```

The worker's pump is the same loop as the main thread's, with no yielding (nothing else runs in the worker): read stream chunks into the job's room until `pieces_max` bytes are in, `pieces_put`, repeat.
An image job is the app's own few lines (its module links cdom_image like any C):

```c
static cd_image_dec *d;   /* the job's own state: the instance is used once */
uint32_t pieces_begin(uint32_t size, const uint8_t *a, uint32_t n) { (void)n; return cd_image_begin(size, rd16(a), rd16(a + 2), &d) ? 2 : 0; }
uint8_t *pieces_room(void) { uint32_t m; return cd_image_room(d, &m); }
uint32_t pieces_max(void) { uint32_t m; cd_image_room(d, &m); return m; }
uint32_t pieces_put(uint32_t n) {
  uint32_t s = cd_image_put(d, n);
  if (s == CD_IMAGE_MORE) return CD_JOB_PIECES_MORE;
  if (s != CD_IMAGE_OK) return s == CD_IMAGE_NO_ROOM ? CD_JOB_RUN_NO_ROOM : 2;
  uint8_t *rgba; uint32_t w, h;
  cd_image_take(d, &rgba, &w, &h);
  cd_job_answer(rgba, w * h * 4);   /* the app's callback gets the pixels; the job's memory goes with the worker */
  return CD_JOB_RUN_OK;
}
```

The app's memory sees only the answer (2.43 MB for a 900 x 675 output, through `cd_job_room`), never the file.

## 6. The async shape, and the main-thread freeze

The decode today is one synchronous call; with pieces it is a decoder block plus one `cd_image_put` per piece, each from its own event.
`cd_image_resize` keeps its one-call form for bytes in memory; the new forms are the decoder (section 1) and the glue's completion callback.

On the main thread, outside a worker, the freeze changes shape:

- Today: one task the length of the whole decode (234 to 277 ms on this VM for the three big inputs; on the Mac image-design.md's figures).
- Chunked: one task per 1 MiB piece, each the decode of that piece: about 13.6 ms per MiB for baseline JPEG and 19 ms per MiB for PNG on this VM (from the table at the top).
  A progressive JPEG absorbs its scans cheaply per piece, then cuts its output pass into steps of 64 rows.
- Between the tasks the page handles input and paints; the decode's total CPU is the same (section 4), and the reading hides behind the decode (section 2).
- A page that must not stutter at all still wants cd_job; this makes the main-thread path usable, not free.

## 7. Predictions

Each falsifier follows its row.
"VM" is this machine with Chromium 153 and the build at cdom.mk's flags; the Mac numbers are the manager's to take, and the ratios are what should carry over.

**Wasm memory** (the page's `memory.buffer.byteLength` after one decode on a fresh page, its peak since wasm memory never shrinks):

| case | today (measured, VM) | chunked, main thread (predicted) | chunked, in cd_job: the job's memory at its end | the app's memory growth with cd_job |
| --- | --- | --- | --- | --- |
| M1: p12.png (15.2 MB file) | 17.1 MiB | 3.5 to 4.3 MiB | 3.3 to 4.3 MiB (today's cd_job_file_cb: 17.0 to 17.6) | the 2.43 MB output and 0 to 128 KiB more |
| M2: p64.jpg (19.8 MB, 64 MP baseline) | 21.4 MiB | 3.4 to 4.2 MiB | 3.2 to 4.2 MiB (today's: 21.3 to 21.9) | as M1 |
| M3: p12p.jpg (progressive) | 40.8 MiB | 38.2 to 39.2 MiB | 38.0 to 39.2 MiB | as M1 |
| M4: p12.jpg | 6.1 MiB | 3.4 to 4.2 MiB | 3.2 to 4.2 MiB | as M1 |

Why: what today holds beside the file (2.5 to 2.6 MiB measured), plus the 1 MiB piece, plus 68 KiB of JPEG keep or 1 KiB of PNG carry, rounded up to 64 KiB pages; the native prototype's heap peaks agree (2.8 MB at 256 KiB pieces for every input but progressive; bench/chunked/results/time-native.txt).
Falsifier for each: a measurement outside its range by more than 2 pages (128 KiB).
The native heap peak (prototype, 1 MiB pieces) is also predicted: 3.5 to 3.7 MB for p64.jpg and p12.png; falsifier over 4 MB.

**Time** (VM; 15 rounds, interleaved, fresh page per run, as bench/image/run.mjs):

- T1, baseline JPEG, the required one: the decode's CPU (the sum of the put calls) for p64.jpg chunked over today's `cd_image_resize`: 1.000, within 0.99 to 1.01.
  Falsifier: a median ratio above 1.02 (outside both spreads), or the instruction count (native, cachegrind) more than 0.5% over whole at 1 MiB pieces (measured now: +0.12%).
- T2: the same for p12.png: within 0.99 to 1.01 (instructions now: +0.04% at 1 MiB); with png-fast's inflate, see section 3.
- T3: from the pick to the pixels (the read included), on the main thread: within -5% to +3% of today's (today pays 17 to 22 ms of whole read before the decode starts; chunked pays 13 to 16 ms, mostly hidden).
  Falsifier: more than 3% slower.
- T4: the longest main-thread task while decoding (CDP Tracing, devtools.timeline): p64.jpg 12 to 25 ms, p12.png 15 to 30 ms, p12p.jpg 15 to 35 ms, against 257, 277 and 234 ms today.
  Falsifier: any over 40 ms.
- T5, in cd_job: wall time with `cd_job_pieces_cb` within 3% of today's `cd_job_file_cb` for p64.jpg and p12.png (today's job reads the stream whole before `run`; the pieces job overlaps the two).
  Falsifier: more than 3% slower.
- T6: shim size, as in section 2.

## 8. The test plan

Native (tests/image/test_image.c, ASan and UBSan, the counted heap), the decoder against `cd_image_resize` as the reference, same status and same bytes:

1. Byte-exact: every oracle input (tests/image/oracle.py's 52 input files, 59 cases with their boxes) and every bench input, at piece sizes 1, 2, 3, 7, 4093, 65536 and 1 MiB, and at a seeded sequence of random sizes from 1 to 70,000 (one named seed).
   libjpeg's warning count must match too, except where libjpeg re-emits a warning on re-reading a marker after a suspension (scans500.jpg and scans501.jpg at pieces under 4 KiB, measured), so a skipped segment that is fed as data (a lost skip) still fails the test even when the pixels come out right.
2. A split PNG chunk: bench/chunked/edge.py's PNGs (zero-length chunks before, between and after the IDATs and an empty IDAT; IDATs of 1 to 7 bytes; a 200,000-byte tEXt; a palette with tRNS in 5-byte IDATs) at every size in 1.
   Every split point is covered by the 1-byte run; the 7-byte and random runs cover splits inside a length, a type and a CRC at once.
3. A JPEG marker split across pieces: edge.py's JPEGs (the largest COM; 5,000 fill bytes before every marker; Exif behind a full APP2; restart markers every MCU row), and for each oracle JPEG, every split point in its first 4 KiB (put i bytes, then the rest, for every i).
4. A file shorter than claimed (the reader ends early): the glue answers UNREADABLE, the heap is back to its start; and a reader that answers 0 bytes before `size` is UNREADABLE at once, never a loop (the driver must make progress or stop).
5. A read error at 1/16, 1/2 and 15/16 of the file: UNREADABLE, the heap back to its start.
6. Cancel mid-file: `cd_image_end` after k puts, for every k from 0 to the last, on p12.jpg, p12p.jpg and p12.png at 64 KiB: the heap back to its start, and ASan sees no use after free.
7. A file cut short whose size says so: the same status as whole at half the file and at every cut in its last 16 bytes (which catches a walker that takes a chunk running past the file).
8. No room at every allocation (the counted heap's room swept), through the decoder: NO_ROOM and nothing left, as test_no_room does today.
9. The output pass in steps: p12p.jpg answers MORE with `*max` 0 at least 10 times, and the pixels are equal.

Browser (tools/file.mjs and tools/image.mjs, Chrome through tools/cdp.mjs):

10. `cd_file_next_cb` on a 20 MB patterned file at max 1, 4093 and 1 MiB: the pieces put end to end equal the file; n 0 once at the end; a second next while one is pending traps; `cd_file_close` with a piece pending: no callback, and canary bytes on both sides of `dst` untouched; the file deleted from disk mid-read, and replaced by a shorter one: UNREADABLE.
11. `cd_image_file_cb` on every oracle input picked from disk, both `CDOM_IMAGE_SJLJ` ways: equal to `cd_image_resize`'s bytes; 300 corrupt files in a row grow the memory by 0 pages after the first (as today's error test); the legacy tier (no `Blob.stream`) through slices.
12. The predictions of section 7 (memory, the longest task, time), with their commands committed beside the results.
13. `cd_job_pieces_cb`: equal bytes, the job's memory, the app's memory growth.

Mutation check (tools/mutate.py, each mutant must fail a test above):

- the prototype's eight (bench/chunked/mutate.sh, results/mutate.txt): a skip past the buffer dropped, a skip one byte long, a held-back byte lost, a zero-length chunk's CRC taken as 3 bytes, a chunk past the file taken, the PLTE buffer one byte short, a short read ignored (pull and push);
- in the shim: the last piece answered twice; one byte dropped where a stream chunk is split across two `dst`s; `cd_file_close` not releasing the callback slot; the answer chained as a microtask;
- in C: the room's max one more than the buffer holds; the header walker not fed before libjpeg; a put not inside the guard; `cd_image_end` not destroying libjpeg on a decoder that suspended.

## The prototype's results against its predictions

Commands: `bench/chunked/run.sh` (build, check, time), with `EDGE=dir` (edge.py's inputs) and `ORACLE=dir` (oracle.py's inputs); `bench/chunked/count.sh FILE...`; `bench/chunked/mutate.sh`.
Results: bench/chunked/results/ (check.txt, time-native.txt, instructions.txt, mutate.txt).

| | predicted | measured | verdict |
| --- | --- | --- | --- |
| B1 | every mode and piece size equal to whole | equal, status and bytes, on the 52 oracle input files, 8 edge inputs and 6 bench inputs, at 1, 7, 4093, 65536 and 262144 (the 48 and 64 MP files at the last three); the failure cases too | confirmed |
| B2 | pull 256 KiB on p64.jpg within 1% | time -1.5% (VM spread about 20%: no signal); instructions +1.47% | refuted by instructions: the per-piece slow path, which also sets the 1 MiB choice |
| B3 | push 256 KiB +0.5% to 2% | instructions +1.48%, time -0.6% | confirmed, though the cause is the slow Huffman path at each refill, not suspension (pull costs the same) |
| B4 | push 64 KiB +1% to 5% | instructions +2.11%, time +0.1% | confirmed |
| B5 | p64.jpg peak: whole 22.5 to 23.5 MB, chunked 2.6 to 4 MB | whole 22.33 MB, chunked 2.76 (pull) and 2.83 MB (push) | whole refuted by 0.17 MB: the prediction assumed a 20.26 MB file, and p64.jpg was remade at 19.83 MB to stay under the 64 MP cap (file plus 2.5 MB holds); chunked confirmed |
| B6 | p12.png peak whole 17.4 to 18.4 MB, chunked 2.6 to 4 MB, time within 1% | 17.73 MB, 2.81 MB, instructions +0.51% at 256 KiB (+0.04% at 1 MiB) | confirmed |
| B7 | 1-byte pieces finish on p12.jpg and p12p.jpg | 1.9 s and 0.63 s (push), equal | confirmed |

Not predicted, found: libjpeg re-emits a warning when it re-reads a warning marker after a suspension, so warning counts are compared only where no warning marker meets a piece edge (test 1 says where).
All eight of the prototype's mutants are killed (results/mutate.txt); the one that ignores a short read in the push driver makes no progress and is killed by the run's 600 s timeout, so the build's driver needs a test that a reader making no progress ends (test 4).
And one mutant of the walker (dropping the check that 12 bytes are left before a chunk's header) is equivalent: the check after the header catches the same files, so the prototype dropped the line.

## 9. Open questions for Alex

1. Names: `cd_image_begin/room/put/take/end` and `cd_file_open/next_cb/close`, and does cdom ship the one-call glue `cd_image_file_cb` (about 50 lines), or does each app write it?
2. png-fast's inflate: accept moving its state into `cd_inflate` and a push entry with a 1 KiB look-ahead (section 3), in whichever of the two PRs lands second?
3. cd_job: add the four `pieces_*` entries beside `run`, and a `cd_job_stop` to cancel a job (today an app can only ignore a late answer)?
4. `cd_file_open` answers at once (it reads nothing): fine, or must it answer from the event loop like every other file call?
5. Piece size fixed at 1 MiB in the header, or a parameter (a phone app might trade 0.1% of time for 768 KiB of memory)?
6. Browsers without `Blob.stream` (Safari before 14.1, the legacy tiers): the slice fallback (about 30 lines of shim), or UNSUPPORTED and the app uses today's whole read?
