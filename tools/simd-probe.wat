;; The module shim/cdom.js's start() validates to pick the SIMD twin of a module (cdom.mk's APP_SIMD): one function that
;; returns a v128 made by i8x16.splat, a wasm SIMD instruction of the final opcode numbering (0xfd 0x0f), so a browser
;; validates it exactly when it instantiates a module built with -msimd128. Its 29 bytes are written out in cdom.js;
;; tools/pick.mjs checks they are what `wasm-as -all tools/simd-probe.wat` (binaryen) makes of this file.
(module
  (func (result v128)
    (i8x16.splat (i32.const 0))))
