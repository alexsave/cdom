# libjpeg-turbo 3.2.1, the decoder only

cdom's image module (`src/cdom_image_jpeg.c`, `APP_MODULES += image`) decodes JPEG with this.

- Version: 3.2.1.
- Source: https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.1/libjpeg-turbo-3.2.1.tar.gz
- sha256 of that archive: `c27ad72a68da151177929aa969ae773a2da5ac2af9ce946079314fdac8cece7f`.
- Licence: the IJG licence (`README.ijg`, for the libjpeg API library, which is all that is here) and the modified BSD licence (`LICENSE.md`, for the TurboJPEG API and the build system, none of which is here), both as shipped.
- Made by `tools/third_party.py fetch`, and checked by `tools/third_party.py check` (tests/image/run.sh), which makes the files again from the archive and fails on any difference: nothing here is edited by hand but this README.

## What is here

The files the 8-bit lossy Huffman decoder (baseline and progressive) compiles and includes, from the archive's `src/`, and its `LICENSE.md` and `README.ijg`.
Compiled (cdom.mk, `CDOM_IMAGE_VENDOR_SRC`): `jcomapi.c jdapimin.c jdhuff.c jdinput.c jdmarker.c jdmaster.c jdphuff.c jmemmgr.c jpeg_nbits.c` and the ten 8-bit wrappers in `src/wrapper/` (`jdapistd-8.c` and the rest), which include the precision-dependent files (`jdapistd.c` and the rest) as upstream builds them.
Everything else here is included by those.

Not here: SIMD, the encoder, arithmetic coding, lossless JPEG, 12 and 16-bit samples, the stdio and memory data sources (`src/cdom_image_jpeg.c` reads the caller's bytes itself), `jerror.c` (its messages are text cdom never shows; `src/cdom_image_jpeg.c` has its own error manager), `jmemnobs.c` (`src/cdom_image_jpeg.c` is the memory layer, over `cd_alloc_try`, with a budget), colour quantization, merged upsampling, and the float and fast IDCTs.

## The edits

1. `src/jmorecfg.h`: seven features switched off, each line marked `cdom: off`: `DCT_IFAST_SUPPORTED`, `DCT_FLOAT_SUPPORTED`, `D_LOSSLESS_SUPPORTED`, `SAVE_MARKERS_SUPPORTED`, `UPSAMPLE_MERGING_SUPPORTED`, `QUANT_1PASS_SUPPORTED`, `QUANT_2PASS_SUPPORTED`; and one line in its copyright header saying so.
2. `src/jconfig.h` is the archive's `src/jconfig.h.in` filled in as CMake would for this build: `JPEG_LIB_VERSION` 80, version 3.2.1, and every `#cmakedefine` off (no arithmetic coding, no SIMD).
3. `src/jconfigint.h` is `src/jconfigint.h.in` filled in the same way: `BUILD` "cdom", clang's `visibility("hidden")` and `always_inline`, no thread-local storage, `SIZEOF_SIZE_T` from the compiler, and `HAVE_BUILTIN_CTZL` on (the one `#cmakedefine` that is).

Built with `-DNO_GETENV -DNO_PUTENV` (the library's own switches: there is no environment) and `-w`, with this `src/` on the include path of the module's files only (cdom.mk, `CDOM_IMAGE_PARTS`).
The six 12-bit entry points the decoder names (`j12init_*`) are `src/cdom_image_jpeg.c`'s, and are never reached: the module refuses a 12-bit frame from its own header read.

## What an app that ships it must do

An app's module (its `.wasm`, or the page that bundles it) is executable code that contains libjpeg, so the app's documentation must state: "This software is based in part on the work of the Independent JPEG Group" (`README.ijg`, condition 2; `LICENSE.md`, item 2.1).
The BSD notice is asked only of a program that includes the TurboJPEG API, cjpeg or djpeg (`LICENSE.md`, item 2.2), and none is here.
Neither the IJG's name nor libjpeg-turbo's may be used to promote the app (`LICENSE.md`, item 3).
