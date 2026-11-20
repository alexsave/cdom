# The Mac sweep: results against the predictions

`bench/chunked/sweep.sh 9` on Alex's Mac (arm64; Chrome for the CDP runs, Safari 27.0.1 through safaridriver), 2026-11-20 at about 1am New York time.
That is 720 runs per browser: 9 interleaved rounds of 5 inputs x 4 arms x 4 piece sizes, all status OK.
The tables are sweep-chrome.tsv and sweep-safari.tsv; the predictions are mac-predictions.md, committed before the first run (4019ddf).

## What the first sweep found

The first sweep, on the branch as it came from #74, had Safari's p12p.jpg through `cd_image_file_cb` at 2 to 39 seconds, against 125 ms read whole.
Its times clustered at about 4 s, which is timer throttling, not decoding.
The progressive output pass is cut into steps (design test 9), and each step yielded through `cd_timer_cb(0)`, a `setTimeout`.
Safari throttles timeouts by seconds in a window that is not in front, and every browser clamps a nested 0 ms timeout to 4 ms.
An experiment (3 rounds, the shim's 0 ms timer as a posted task) gave 127 to 131 ms, so the fix is in the shim: a 0 ms timer is now the next task (1290403).
That first sweep's 1,440 runs are kept outside the repository, in the dispatcher's review notes; the tables here are the second sweep, after the fix.
The fix helped Chrome as well: p12p.jpg's file arm went from 146.9 ms to 119.0 ms, against 119.8 ms whole.

## Medians, 1 MiB pieces (ms; wasm memory in MiB)

| input | browser | whole | file (main thread) | pieces (cd_job) | job (today's cd_job_file_cb) |
| --- | --- | --- | --- | --- | --- |
| p12.jpg | Chrome | 64.9, 6.19 | 64.2, 3.63 | 69.3, 2.50 (job 3.56) | 70.2, 2.50 (job 6.13) |
| p12.jpg | Safari | 66, 6.19 | 68, 3.63 | 73, 2.50 | 72, 2.50 |
| p48.jpg | Chrome | 185.8, 17.19 | 179.7, 3.63 | 184.3, 2.50 (3.56) | 190.3, 2.50 (17.13) |
| p48.jpg | Safari | 184, 17.19 | 186, 3.63 | 189, 2.50 | 189, 2.50 |
| p64.jpg | Chrome | 238.7, 21.50 | 230.2, 3.63 | 235.0, 2.50 (3.56) | 243.3, 2.50 (21.44) |
| p64.jpg | Safari | 236, 21.50 | 239, 3.63 | 239, 2.50 | 240, 2.50 |
| p12.png | Chrome | 193.5, 17.69 | 187.0, 3.94 | 191.8, 2.50 (3.88) | 198.4, 2.50 (17.63) |
| p12.png | Safari | 237, 17.69 | 239, 3.94 | 241, 2.50 | 242, 2.50 |
| p12p.jpg | Chrome | 119.8, 26.19 | 119.0, 27.25 | 123.5, 2.50 (27.19) | 124.3, 2.50 (26.13) |
| p12p.jpg | Safari | 125, 26.19 | 128, 27.25 | 130, 2.50 | 130, 2.50 |

The longest main-thread task in Chrome, whole against file at 1 MiB: p64.jpg 226.3 against 17.5 ms, p12.png 183.3 against 17.0, p48.jpg 175.9 against 16.7, p12.jpg 61.6 against 18.5.
At 16 KiB, and in every pieces run, it is 4.5 to 5.0 ms.

## Verdicts

| id | prediction | measured | verdict |
| --- | --- | --- | --- |
| M1 | file at 1 MiB: JPEGs 3.4 to 3.9, p12.png 3.7 to 4.2 MiB | 3.63, 3.94 | confirmed |
| M2 | file at 16 KiB: 2.5 to 3.0 MiB | 2.63 to 2.94 | confirmed |
| M3 | whole as main, within 2 pages | p12.png 17.69 against the 17.38 assumed (the VM's figure for this branch) | refuted by 5 pages: the assumption that memory does not depend on the machine was wrong for the whole read; not measured against main here |
| M4 | pieces: the app's 2.50, the job's 3.4 to 4.0 MiB at 1 MiB | 2.50; 3.56 and 3.88 | confirmed |
| M5 | p12p.jpg file within 1.5 MiB of whole | 27.25 against 26.19 | confirmed |
| M6 | Safari's wasm memory equal to Chrome's | equal in every cell | confirmed |
| T1 | file and pieces at 1 MiB within 5% of whole | file: within 3.5% everywhere; pieces: p12.jpg +6.8% (Chrome) and +10.6% (Safari), the rest within 3% | refuted for pieces on p12.jpg: a job costs about 5 to 7 ms of worker start whatever the size (today's job arm costs the same), which a 65 ms decode shows |
| T2 | file at 16 KiB at most 15% slower than at 1 MiB | Chrome 4% to 8%; Safari 28% to 38% | confirmed in Chrome, refuted in Safari: about 0.07 ms a piece there (p64.jpg: 1,210 pieces, +90 ms) |
| T3 | pieces at most 3% slower than job | 0.4% to 3.4% faster in every cell | confirmed |
| L1 | whole's longest task 100 to 200 ms | p12.png 183.3, p64.jpg 226.3 | refuted for p64.jpg (over) |
| L2 | file at 1 MiB under 25 ms (8 to 25) | 16.7 to 18.5 | confirmed |
| L3 | 16 KiB file, and pieces, under 6 ms | 4.5 to 5.0 | confirmed |
| L4 | p12p.jpg file 60 to 110 ms | 95.3 | confirmed |

## What it means for an app

- The picked file is never held: wasm memory after a 64 MP photo falls from 21.5 MiB to 3.6 MiB on the main thread, and to 2.5 MiB in the app with the decode in a job (the job's own 3.6 MiB goes when it ends).
- The longest main-thread task falls from 176 to 226 ms to about 17 ms at the default 1 MiB, and to about 5 ms at 16 KiB.
- The time from pick to pixels is unchanged at 1 MiB, in both browsers.
- 1 MiB stays the default: in Safari, smaller pieces cost about 0.07 ms each.
- A progressive file is still gathered whole (its coefficients are most of its memory anyway) and its scans are one task of about 95 ms in Chrome; making the progressive decoder suspend mid-scan is what would cut that.
