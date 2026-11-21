# The math bench's vendored libms

Each directory holds only the files the bench's functions need, as fetched, with one change noted below.
Each has a SHA256SUMS of its files as vendored here, and keeps its licence file.
None of this is cdom's library: it is built only by bench/math/Makefile, as the bench's arms (docs/math-design.md).

## musl/ (MIT)

- musl 1.2.6, the release tarball musl-1.2.6.tar.gz.
- Upstream URL: https://musl.libc.org/releases/musl-1.2.6.tar.gz (with musl-1.2.6.tar.gz.asc, the release signature).
- musl.libc.org answered 403 through this VM's proxy, so the tarball came from Ubuntu's archive, which carries upstream's tarball under its source package's name: http://archive.ubuntu.com/ubuntu/pool/universe/m/musl/musl_1.2.6.orig.tar.gz (and its .asc beside it).
- sha256 of the tarball: d585fd3b613c66151fc3249e8ed44f77020cb5e6c1e635a616d3f9f82460512a
- sha256 of its .asc: 8a696228404cd4bae6379cf7538fc5d18c133b0ada54a41c1c6cef31d3c22ab6 (the signature was not checked here: musl's key could not be fetched).
- Licence: COPYRIGHT (MIT).
- Taken: src/math/ (the files of the 21 functions, their float twins, their helpers and data tables, fma.c and fmaf.c, scalbn.c and scalbnf.c), src/internal/libm.h, and arch/generic/fp_arch.h (as src/internal/fp_arch.h, where libm.h finds it).
- Unchanged.

## msun/ (BSD-style)

- FreeBSD's msun (lib/msun/src), the fdlibm descendant, at the tag release/15.1.0.
- Upstream: https://github.com/freebsd/freebsd-src/tree/release/15.1.0/lib/msun/src
- There is no tarball of msun alone, and GitHub's archive of the tag answered 403 through this VM's proxy, so each file was fetched from https://raw.githubusercontent.com/freebsd/freebsd-src/release/15.1.0/lib/msun/src/<file>; SHA256SUMS lists each.
- Licence: COPYRIGHT (FreeBSD's, from the tree's root; each file carries its own Sun or BSD notice).
- Taken: the e_, s_ and k_ files of the 21 functions and their float twins, k_exp.c and k_expf.c (cosh and sinh call __ldexp_exp), s_scalbn.c and s_scalbnf.c, e_sqrt.c and e_sqrtf.c, math_private.h, k_log.h, k_logf.h, k_sincos.h and k_sincosf.h.
  FreeBSD's math.h is not taken (bench/math/compat/math.h stands in for it).
- One change, in math_private.h: nan_mix_op adds 0.0 where FreeBSD adds 0.0L, so mixing two NaNs is done in double.
  On wasm32, long double is IEEE binary128 in software, which needs compiler-rt's soft-float routines (__addtf3 and others) that a cdom module does not link.
  The result is still a NaN, as Annex F asks; only which NaN's payload wins may differ.
  sha256 of the file as fetched: c33130c3a7ecd511074a8b5a7ca3e403661717dbd28c2080771ae13683ba0f2f; as vendored: in SHA256SUMS.

## core-math/ (MIT)

- CORE-MATH, the correctly rounded library of Paul Zimmermann and others, at upstream commit 85430b3e, as vendored by the Rust crate core-math-sys 1.4.0 (its vendor/ directory is the upstream tree at that commit).
- Upstream: https://gitlab.inria.fr/core-math/core-math (gitlab.inria.fr was not reachable from this VM).
- The crate: https://static.crates.io/crates/core-math-sys/core-math-sys-1.4.0.crate
- sha256 of the crate (a gzipped tarball): 07fcc675783e2866181bf63060b7983973bd6e93f042fb0f8ba544e84c26c3d4, the same as crates.io's index records for it.
- Licence: LICENSE (MIT).
- Taken: src/binary64/<f>/<f>.c and src/binary32/<f>/<f>f.c for the 20 functions it has (it has no fmod), and the headers they include (tint.h, dint.h, pow.h, qint.h).
- Unchanged.
  CORE-MATH names its functions cr_sin and so on; the Makefile builds them as sin (-Dcr_sin=sin).
  It calls fma, which wasm lacks: its arm links musl's fma.c and fmaf.c, and in wasm the three 128-bit integer helpers of bench/math/compat/int128.c.
