# The Mac sweep: predictions, written before the run

Written 2026-11-19, before `bench/chunked/sweep.sh` ran on Alex's Mac (arm64, Chrome and Safari).
The VM's smoke sweep in the PR body is the base.
The Mac is about 1.5 to 2 times the VM's speed on these decodes, so the times are scaled from it; memory does not depend on the machine.
"file" is `cd_image_file_cb` on the main thread, "pieces" is `cd_job_pieces_cb` (the decode in a job), "whole" is today's whole read and `cd_image_resize`, and "job" is today's `cd_job_file_cb`.
Each falsifier follows its line.

## Wasm memory (the page's, after the decode)

- M1: file at 1 MiB pieces: p12.jpg, p48.jpg and p64.jpg 3.4 to 3.9 MiB; p12.png 3.7 to 4.2 MiB.
  Falsifier: any of them outside its range by more than 2 pages.
- M2: file at 16 KiB pieces: p12.jpg, p48.jpg, p64.jpg and p12.png 2.5 to 3.0 MiB.
  Falsifier: any over 3.1 MiB.
- M3: whole: as main (p12.jpg 6.19, p64.jpg 21.5, p12.png 17.38 MiB), within 2 pages.
- M4: pieces: the app's memory 2.50 MiB at every size, and the job's memory at its end 3.4 to 4.0 MiB at 1 MiB.
  Falsifier: the app's over 2.6 MiB, or the job's outside its range by more than 2 pages.
- M5: p12p.jpg file within 1.5 MiB of whole (a progressive file is gathered whole, then decoded).
  Falsifier: file more than 1.5 MiB over whole.
- M6: Safari's wasm memory equal to Chrome's for the same arm, size and input, within 2 pages.

## Time, from the pick to the pixels (medians)

- T1: file and pieces at 1 MiB within -5% to +5% of whole for p12.jpg, p48.jpg, p64.jpg and p12.png, in both browsers.
  Falsifier: either more than 5% slower than whole.
- T2: file at 16 KiB at most 15% slower than file at 1 MiB (the cost per piece: a JS-to-wasm call and a task each).
  Falsifier: more than 15% slower.
- T3: pieces at most as slow as job (the pieces job reads while it decodes; today's job reads the whole file first) for p64.jpg and p12.png.
  Falsifier: pieces more than 3% slower than job.

## The longest main-thread task (Chrome only: Safari has no trace)

- L1: whole: 100 to 200 ms for p64.jpg and p12.png.
- L2: file at 1 MiB: 8 to 25 ms for p12.jpg, p48.jpg, p64.jpg and p12.png.
  Falsifier: any over 40 ms.
- L3: file at 16 KiB, and pieces at every size: under 6 ms for the same inputs.
  Falsifier: any over 10 ms.
- L4: p12p.jpg file: 60 to 110 ms (its scans are one task).
  Falsifier: under 40 or over 150 ms.
