#!/bin/bash
# The static v128 instruction count of a wasm module (docs/image-simd.md, V1 to V4): the instructions llvm-objdump -d
# prints whose mnemonic is v128.* or a lane shape (i8x16.* i16x8.* i32x4.* i64x2.* f32x4.* f64x2.*), and the module's
# instructions in all. Usage: bench/image/v128.sh a.wasm [b.wasm ...]
for f in "$@"; do
  llvm-objdump -d "$f" 2>/dev/null | awk -F'\t' -v f="$f" '
    /^ *[0-9a-f]+:/ { n++; if ($2 ~ /^(v128|i8x16|i16x8|i32x4|i64x2|f32x4|f64x2)\./) v++ }
    END { printf "%s\tinstructions %d\tv128 %d\n", f, n, v }'
done
