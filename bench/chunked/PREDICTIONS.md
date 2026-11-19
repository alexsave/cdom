# The chunked-read prototype: predictions (written before it was built or run)

Throwaway prototype for docs/chunked-file-read-design.md.
Native x86-64, clang -O2, the image module's own files and the vendored libjpeg-turbo and miniz as cdom.mk lists them, tests/image/host_alloc.c's counted heap.
Box 900 x 900, inputs from bench/image/inputs.py plus p64.jpg (9248 x 6936 baseline, the same scene).
"whole" is today's cd_image_resize over the file in memory.
"pull N" reads pieces of N bytes through a synchronous read callback (the worker route).
"push N" hands pieces of N bytes to a resumable decoder (libjpeg's suspending source, tinfl fed per piece: the main-thread route).

- B1: every mode at every piece size (1, 7, 4093, 65536, 262144) gives the same status and the same RGBA bytes as whole.
  Falsifier: one byte or one status differs.
- B2: pull 262144 on p64.jpg is within 1% of whole (median of 7).
  Falsifier: 2% or more slower.
- B3: push 262144 on p64.jpg is 0.5% to 2% slower than whole (libjpeg's slow Huffman path near each piece's end, and one MCU decoded twice per suspension).
  Falsifier: over 3% slower.
- B4: push 65536 on p64.jpg is 1% to 5% slower than whole.
- B5: the C heap's peak on p64.jpg: whole 22.5 to 23.5 MB (the 20.26 MB file plus about 2.5 MB); pull and push 262144 2.6 to 4 MB.
  Falsifier: chunked over 5 MB.
- B6: p12.png, whole 17.4 to 18.4 MB peak (15.18 MB file plus about 2.6 MB), push 262144 2.6 to 4 MB, time within 1% of whole.
- B7: 1-byte pieces finish on p12.jpg and p12p.jpg; no time is predicted.
