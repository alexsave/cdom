# miniz 3.1.2, the inflate section only (tinfl)

cdom's image module (`src/cdom_image_png.c`, `APP_MODULES += image`) inflates PNG image data with this.

- Version: 3.1.2.
- Source: https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip
- sha256 of that archive: `f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a`.
- Licence: MIT (`LICENSE`, as shipped).
- Made by `tools/third_party.py fetch`, and checked by `tools/third_party.py check` (tests/image/run.sh), which makes the files again from the archive and fails on any difference: nothing here is edited by hand but this README.

## What is here

- `tinfl.c` is `miniz.c` lines 2278 to 3027, its inflate section, cut whole and unchanged: from its `#ifndef MINIZ_NO_INFLATE_APIS` to the matching `#endif`.
- `tinfl.h` is `miniz.h` lines 911 to 1056, the declarations of that section, cut the same way.
- `LICENSE`.

The one edit is the cut itself: the types and macros the section takes from the rest of miniz (`mz_uint8` and its kin, `MZ_MALLOC`, `MZ_READ_LE16` and so on) are cdom's, in `src/cdom_image_miniz.h`, which the build includes ahead of `tinfl.c` (`-include`), with the heap's `cd_alloc_try` for `MZ_MALLOC` and the 64-bit bit buffer on.
Nothing else of miniz is here: no compressor, no zip archive code, no zlib-style API.
cdom calls `tinfl_init` and `tinfl_decompress` only, through a 32 KB wrapping window.

## What an app that ships it must do

MIT asks that the copyright notice and the permission notice be included in all copies or substantial portions of the software: an app that ships the image module includes miniz's `LICENSE` text with its notices (beside the IJG sentence above, `../libjpeg-turbo-3.2.1/README.md`).
