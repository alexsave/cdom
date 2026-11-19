# SIMDe 0.8.2, one header

libjpeg-turbo's Arm NEON intrinsics (`third_party/libjpeg-turbo-3.2.1/simd/arm/`) are built for wasm32 over this: SIMDe turns each NEON intrinsic into wasm simd128 (`CDOM_IMAGE_SIMD=simde`, cdom.mk, docs/image-simd.md).

- Version: 0.8.2 (tag v0.8.2).
- Source: https://github.com/simd-everywhere/simde/releases/download/v0.8.2/simde-amalgamated-0.8.2.tar.xz
- sha256 of that archive: `59068edc3420e75c5ff85ecfd80a77196fb3a151227a666cc20abb313a5360bf`.
- Licence: MIT; the licence text is at the head of `simde/arm/neon.h`, as the archive ships it.
- Made by `tools/third_party.py fetch`, and checked by `tools/third_party.py check` (tests/image/run.sh): nothing here is edited by hand but this README.

## What is here

`simde/arm/neon.h`, copied whole and unchanged from the archive's `arm/neon.h` (the release is amalgamated: that one file holds the NEON code, SIMDe's wasm simd128 and portable code, and everything it includes).
Headers only; no source file of SIMDe is compiled.
Not here: the rest of the archive (the x86, MIPS and SVE headers, and the wasm headers, which `neon.h` already holds).

## Local changes

None to the file.
The build sets, for libjpeg-turbo's NEON files only (cdom.mk, `CDOM_IMAGE_VENDOR_CFLAGS`): `-isystem third_party/simde-0.8.2` (so `#include <simde/arm/neon.h>` finds it) and `-DSIMDE_FLOAT16_API=1` (SIMDe's own switch for its portable half-float type, since clang 18 has no `_Float16` on wasm32 and SIMDe's detection picks it).
libjpeg-turbo defines `SIMDE_ENABLE_NATIVE_ALIASES` itself (`simd/arm/neon-compat.h`).
