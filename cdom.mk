# cdom.mk - what an app's Makefile includes to build on cdom (README.md, "Building an app"):
#
#   CDOM := path/to/cdom
#   include $(CDOM)/cdom.mk
#
# Included once, before or after the app sets APP_*: it defines variables and the rules for the files
# APP_WASM and APP_PAGE name, which read APP_* when make builds, not where cdom.mk is included (see the
# rules below). It never changes the app's default goal. Plain make, clang and a host C compiler
# (HOST_CC, for tools/cdlits and tools/cdpage): no Node on this path. Every link goes through tools/cdlits,
# which moves the module's string literals into $(APP_WASM).lits; tools/cdpage writes the page, that table
# in it as a script of its own before the shim (docs/strings-design.md, docs/page-tool-design.md).
#
#   CDOM_CFLAGS   the compile flags every file of a cdom module needs, -std=c23 among them: C23, so a handler
#                 can leave a parameter it does not use unnamed, static void on_reset(const cd_event_rec *, void *)
#   CDOM_LDFLAGS  the link flags of a cdom module
#   CDOM_SRC      the core sources every app links (cdom.c, and mini.c, the program's libc)
#   CDOM_NET_SRC, CDOM_STORE_SRC, CDOM_RANDOM_SRC, CDOM_AUDIO_SRC, CDOM_URL_SRC, CDOM_LOCALE_SRC,
#   CDOM_SHARE_SRC, CDOM_FONTS_SRC, CDOM_UI_SRC, CDOM_FILE_SRC, CDOM_JOB_SRC
#   CDOM_IMAGE_PARTS (own inflate vendor sjlj file) and their _SRC and _CFLAGS: the image module
#                 (src/cdom_image.h), which is parts only: cdom's files, its inflate, libjpeg-turbo, the
#                 setjmp file, and (with the file module too) the glue to a picked file
#   CDOM_IMAGE_SJLJ  how the image module leaves a fatal libjpeg error (src/cdom_image_sjlj.c): js (the default,
#                 a try and catch in the shim; plain wasm, every browser cdom runs in, and wasm2js) or eh (wasm
#                 exception handling: Chrome 95, Firefox 100, Safari 15.2, and no wasm2js)
#   CDOM_IMAGE_SIMD  0 (the default, scalar) or 1: the image module's hot loops in hand-written wasm SIMD
#                 (-msimd128: src/cdom_image_loops.c, and src/jsimd/jsimd_wasm.c through libjpeg-turbo's own SIMD
#                 dispatch); the module then needs wasm SIMD to instantiate (Chrome 91, Firefox 89, Safari 16.4)
#                 one optional module each (src/cdom_<name>.h), for an app's own rules (APP_MODULES below
#                 adds them to the APP_WASM rule); a module's list may name several files
#   CDOM_<NAME>_PARTS, CDOM_<NAME>_<PART>_SRC, CDOM_<NAME>_<PART>_CFLAGS, CDOM_<NAME>_DEPS, CDOM_<NAME>_LDFLAGS
#                 a module whose files need flags of their own (third-party code built with -w and its own
#                 -I, say) names its parts; each part's files are compiled apart, as objects, with
#                 $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) and then the part's flags, so a part's -I never
#                 reaches the app's files and an app's -Wall -Werror still links the module. Its objects go
#                 to $(APP_WASM).cdom/<name>/<part>/, rebuilt when a file of CDOM_<NAME>_DEPS changes, and
#                 the link adds CDOM_<NAME>_LDFLAGS. Within a part no two files share a base name.
#   CDOM_SHIM     the minified shim (shim/cdom.min.js) a page inlines
#   CDOM_OPT      the optimization level of the APP_WASM rule (-Oz unless the app sets it)
#   WASM_CC       a clang that targets wasm32, with wasm-ld from LLVM 23 or later (Homebrew's llvm on macOS
#                 unless set, since Apple's clang finds no wasm-ld; clang elsewhere)
#   HOST_CC       the C compiler for cdom's build tools (tools/cdlits.c, tools/cdpage.c), cc unless set
#   CDOM_LITS     the built tools/cdlits, which every link goes through: it strips the string table out of
#                 the module and writes it beside it as $(APP_WASM).lits, for the page
#   CDOM_PAGE_TOOL the built tools/cdpage, which writes every page
#
#   APP_WASM, APP_SRC, APP_MODULES, APP_CFLAGS, APP_LDFLAGS
#                 the app's module: $(APP_WASM) is built from $(APP_SRC), $(CDOM_SRC) and the optional
#                 modules APP_MODULES names (net store random audio url locale share fonts ui file image job: their
#                 CDOM_<NAME>_SRC) with $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) $(CDOM_LDFLAGS) $(APP_LDFLAGS)
#   APP_JOBS, APP_JOB_<name>_SRC, APP_JOB_<name>_MODULES, APP_JOB_CFLAGS, APP_JOB_LDFLAGS
#                 the app's job modules (src/cdom_job.h, src/cdom_job_mod.h): for each name in APP_JOBS,
#                 $(dir $(APP_WASM))<name>.job.wasm is built from $(APP_JOB_<name>_SRC) with cdom's heap alone
#                 (src/cdom.c under -DCDOM_HEAP_ONLY), src/mini.c and src/cdom_job_mod.c, with $(CDOM_OPT)
#                 $(CDOM_CFLAGS) $(APP_JOB_CFLAGS) $(CDOM_LDFLAGS) $(APP_JOB_LDFLAGS). The app names the module in
#                 cd_job_cb as "<name>.job.wasm". A bundled page (the default) carries each in var cdomJobs, written
#                 by tools/cdpage beside the string table, so the page needs no change; with APP_WASM_SEPARATE the
#                 module is the file beside the page, which the name is the URL of. A job has no string table and
#                 goes through no cdlits: it has no DOM and no string literals to move.
#                 APP_JOB_<name>_MODULES (image) links the app's own objects of those modules into the job too.
#   APP_PAGE, APP_PAGE_SRC, APP_TITLE, APP_WASM_SEPARATE, APP_WASM_URL
#                 the app's page: $(APP_PAGE) is $(APP_PAGE_SRC) (or, unset, the default page below) with
#                 the shim inlined at the line that holds only the marker /*CDOM_SHIM*/ (inside a <script>),
#                 and the module at the marker /*CDOM_WASM*/, which stands for cdom.start's first argument.
#                 By default the module is BUNDLED: /*CDOM_WASM*/ becomes "data:application/wasm;base64,..."
#                 of $(APP_WASM), so the app is one html file. APP_WASM_SEPARATE := 1 keeps the module a file
#                 of its own: /*CDOM_WASM*/ becomes its URL, APP_WASM_URL (the base name of APP_WASM, beside
#                 the page, unless set), and a page may name it itself instead (cdom.start("app.wasm", ...)).
#                 The rule fails unless exactly one /*CDOM_SHIM*/ line is there, and, bundling, unless the
#                 page has /*CDOM_WASM*/: an app's own page that names its module by URL fails with "this
#                 page loads <url>; set APP_WASM_SEPARATE := 1, or start with the bundled module (the
#                 /*CDOM_WASM*/ marker)", and is never rewritten.
#                 $(call cdom_page,in.html,out.html,,lits) inlines the shim in a recipe (lits is the module's
#                 .lits file), and $(call cdom_page,in.html,out.html,module.wasm) bundles that module as well,
#                 with the string table cdlits wrote beside it (module.wasm.lits). Both bundle src/'s
#                 stylesheets, as the APP_PAGE rule does.
#   APP_SIMD, APP_SIMD_WASM_URL
#                 APP_SIMD := 1 builds a second module from the same sources, the SIMD twin of APP_WASM (app.simd.wasm
#                 beside app.wasm), its modules' parts with their _SIMD_SRC and _SIMD_CFLAGS (the image module's, as
#                 CDOM_IMAGE_SIMD=1 builds it); APP_WASM stays the same bytes. The twin's string table must be the
#                 same bytes as APP_WASM's (the page carries one), or the build stops and removes the twin. The page's
#                 /*CDOM_WASM*/ becomes the pair [scalar, SIMD], both bundled or both URLs (the twin's APP_SIMD_WASM_URL,
#                 else APP_WASM_URL with .simd.wasm for .wasm), and cdom.start fetches only the one the browser runs
#                 (README.md, "Starting an app"). Refused: CDOM_IMAGE_SIMD=1 beside it, and an app whose APP_MODULES have
#                 no SIMD build.
#
# The stylesheets: Every file whose name ends in .css under the app's src/ directory, in any subfolder,
# is written into the page's <style>, in byte order of its path, so 00-base.css comes before
# 10-layout.css, and that before theme/dark.css; a file or folder whose name starts with a dot is
# skipped, and nothing else is.
# src/ is the directory named src where make runs (where APP_SRC := src/app.c points), and no variable
# names a file, the directory, or a way to turn this off. The files go at the line that holds only the
# marker /*CDOM_CSS*/, inside a <style> and before the /*CDOM_SHIM*/ line (the default page has that
# <style> in its <head>), written as they are but for </style, which is escaped; a comment that fills its
# lines is left out with them (one beside a rule is kept), a byte order mark is dropped, and a file that
# does not end in a newline gains one. Each file must be whole by itself: a
# comment, string or url( that never ends, a { never closed or a } never opened, an @import, a NUL or
# bytes that are not UTF-8 stop the build, naming the file and the line. An app's own page with no
# /*CDOM_CSS*/ line fails while src/ holds a .css file, so a page that loads a stylesheet of its own with
# a <link> keeps that file outside src/ (static/app.css, say). A relative url() in a stylesheet resolves
# against the page, since the sheet is inline. The sheet is the page's HTML, before every script, so it
# styles the first paint in every tier. The page is made again when a .css file is added, removed,
# renamed or edited, and for nothing else: $(APP_PAGE).csslist (tools/cdpage --css-list) lists what went
# in, with a hash of each file, and is replaced only when that text changes.
#
# The default page (shim/page.html), the contract other repos may name as "cdom's default page": a
# doctype, <meta charset="utf-8">, the viewport meta (width=device-width, initial-scale=1), a <title> of
# APP_TITLE (HTML-escaped; the base name of APP_WASM unless set), a <style> after it that holds the
# app's stylesheets (empty when src/ has none), a <body> that holds only the no-script fallback's
# /*CDOM_NOSCRIPT*/ line (below), <div id="app"></div>, the app's mount, and one <script>: the shim and the one start call,
# cdom.start(<the module>, "app"), with no env and no options. An app that needs its own <head>, env or
# options brings its own page (APP_PAGE_SRC).
#
# Bundling, the default, costs about 30% more bytes after compression (base64; measured: a hello world
# 9.5 to 12.3 KB brotli, amgi 29.9 to 39.2 KB). In exchange the page is one request, the page and its
# module can never come from two builds, and it opens from disk (file://), where a separate .wasm cannot
# be fetched. A bundled page under a Content-Security-Policy needs data: in connect-src (cdom.start
# fetches the data: URL), plus 'wasm-unsafe-eval' as any cdom page does. What needs a real origin
# (IndexedDB, so cdom's store module, in some browsers) needs the page served over HTTP. The
# no-WebAssembly fallback (shim/boot.js and the wasm2js build, made by tools/legacy/build.mjs) loads files
# beside the page, so it takes APP_WASM_SEPARATE := 1. Large apps, and hosts that cache a hashed .wasm
# for long, opt out too.
#
# The APP_WASM rule first checks that WASM_CC builds a wasm32 module with wasm-ld from LLVM 23 or later, and
# when it does not (Apple's clang, no wasm-ld, or an older one), stops with what it found and the command
# that installs one, instead of a linker error.
# A new app: make -C path/to/cdom new DIR=path/to/app (tools/new.sh) writes a Makefile of the APP_* lines
# its app needs and this include, src/app.c, src/app.css and a .gitignore: nothing this file could own.
#
# The flags: wasm32 with no C library but cdom's own (-nostdlib, then -isystem $(CDOM)/src/libc for
# string.h, whose memcpy, memmove and memset are bulk-memory instructions, hence -mbulk-memory), cdom's
# headers on the include path, a module with no entry point (cdom's exports are its entries), the
# memory exported (the shim reads and writes C memory through it), and a 64 KB stack placed first, below
# the data, so an overflow traps instead of overwriting globals. -ffreestanding is not in the set (it
# turns off clang's builtins and costs cdom code size); string.h works under it, so an app may add it.
# Optimization, warnings and -Wl,--strip-all are the app's choice (APP_CFLAGS, APP_LDFLAGS, CDOM_OPT).
CDOM ?= $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
# Homebrew's llvm on macOS (Apple's clang finds no wasm-ld), the clang on the PATH elsewhere (Linux
# distributions' clang targets wasm32 and wasm-ld comes with lld, but cdom needs LLVM 23 or later, which
# Ubuntu 24.04 and Debian 13 do not ship: apt.llvm.org's clang-23, with WASM_CC set to it).
ifeq ($(shell uname -s),Darwin)
WASM_CC ?= /opt/homebrew/opt/llvm/bin/clang
else
WASM_CC ?= clang
endif
HOST_CC ?= cc
# The host tools are C23 too, spelled -std=c2x: gcc before 14 and clang before 18 (Ubuntu 24.04's gcc 13,
# Alpine 3.20's clang 17) reject -std=c23 and take c2x, and every newer one takes both.
HOST_STD := -std=c2x
CDOM_OPT ?= -Oz

CDOM_CFLAGS := --target=wasm32 -std=c23 -nostdlib -mbulk-memory -isystem $(CDOM)/src/libc -I$(CDOM)/src
CDOM_LDFLAGS := -Wl,--no-entry -Wl,--export-memory -Wl,--stack-first -Wl,-z,stack-size=65536 \
  -Wl,--export-if-defined=__start_cd_lits -Wl,--export-if-defined=__stop_cd_lits \
  -Wl,--export-if-defined=__start_cd_chars -Wl,--export-if-defined=__stop_cd_chars \
  -Wl,--export-if-defined=cd_lits_stamp

CDOM_SRC        := $(CDOM)/src/cdom.c $(CDOM)/src/mini.c
CDOM_NET_SRC    := $(CDOM)/src/cdom_net.c
CDOM_STORE_SRC  := $(CDOM)/src/cdom_store.c
CDOM_RANDOM_SRC := $(CDOM)/src/cdom_random.c
CDOM_AUDIO_SRC  := $(CDOM)/src/cdom_audio.c
CDOM_URL_SRC    := $(CDOM)/src/cdom_url.c
CDOM_LOCALE_SRC := $(CDOM)/src/cdom_locale.c
CDOM_SHARE_SRC  := $(CDOM)/src/cdom_share.c
CDOM_FONTS_SRC  := $(CDOM)/src/cdom_fonts.c
CDOM_UI_SRC     := $(CDOM)/src/cdom_ui.c
CDOM_FILE_SRC   := $(CDOM)/src/cdom_file.c
CDOM_JOB_SRC    := $(CDOM)/src/cdom_job.c
CDOM_JOB_MOD_SRC := $(CDOM)/src/cdom_job_mod.c
# The image module: five parts, so libjpeg-turbo (third_party/, with its README) builds with -w and its own -I,
# cdom's own files with the app's flags and only libjpeg's headers' directory (as -isystem), the PNG path's
# inflate (cdom's own) with the app's flags and CDOM_IMAGE_INFLATE_CFLAGS (empty; a SIMD build adds -msimd128,
# which gives its adler32 and match copies wasm SIMD), the one file that calls setjmp
# with the flags of the CDOM_IMAGE_SJLJ way, and the glue to a picked file (src/cdom_image_file.c, cd_image_file_cb),
# which has files only when APP_MODULES has file too.
CDOM_IMAGE_SJLJ ?= js
CDOM_IMAGE_LJT  := $(CDOM)/third_party/libjpeg-turbo-3.2.1/src
CDOM_IMAGE_SRC  :=
CDOM_IMAGE_PARTS := own inflate vendor sjlj file
CDOM_IMAGE_FILE_SRC = $(if $(filter file,$(APP_MODULES)),$(CDOM)/src/cdom_image_file.c)
CDOM_IMAGE_OWN_SRC := $(CDOM)/src/cdom_image.c $(CDOM)/src/cdom_image_jpeg.c $(CDOM)/src/cdom_image_jpegp.c $(CDOM)/src/cdom_image_png.c \
  $(CDOM)/src/cdom_image_loops.c
CDOM_IMAGE_OWN_CFLAGS := -isystem $(CDOM_IMAGE_LJT) -DNO_GETENV -DNO_PUTENV
# CDOM_IMAGE_SIMD: 0 (the default) is scalar; 1 builds the hot loops with wasm SIMD (-msimd128, src/cdom_image_loops.c and
# src/jsimd/jsimd_wasm.c, which libjpeg-turbo reaches through its own SIMD dispatch, WITH_SIMD: the three headers in
# src/jsimd/simd/ answer its "../simd/" includes). A SIMD module needs a browser with wasm SIMD (Chrome 91, Firefox 89,
# Safari 16.4) and does not instantiate in an older one; the two builds are the same in every other way (docs/image-simd-hand.md).
# APP_SIMD := 1 builds both, and the shim picks one in the browser (below). The SIMD build is the parts' _SIMD_SRC and
# _SIMD_CFLAGS on top of their own (cdom_mk_part_src, cdom_mk_obj_flags).
# MULTIPLIER and ALIGN_SIZE keep libjpeg's scalar types as they are with SIMD off (WITH_SIMD would make them short and 32).
CDOM_IMAGE_SIMD ?= 0
ifeq ($(filter 0 1,$(CDOM_IMAGE_SIMD)),)
$(error cdom.mk: CDOM_IMAGE_SIMD is $(CDOM_IMAGE_SIMD), not 0 or 1)
endif
cdom_mk_simd_flags := -msimd128 -DCDOM_IMAGE_SIMD=1 -DCDOM_JSIMD -DMULTIPLIER=int -DALIGN_SIZE=8
CDOM_IMAGE_OWN_SIMD_SRC := $(CDOM)/src/jsimd/jsimd_wasm.c
CDOM_IMAGE_OWN_SIMD_CFLAGS := $(cdom_mk_simd_flags)
# jdphuff.c (libjpeg's progressive decoder) is not compiled, and jdcoefct.c is compiled under another name
# (src/cdom_image_ljt_coef.c): src/cdom_image_jpegp.c is cdom's progressive path (docs/image-progressive-speed.md).
CDOM_IMAGE_VENDOR_SRC := $(addprefix $(CDOM_IMAGE_LJT)/,jcomapi.c jdapimin.c jdhuff.c jdinput.c jdmarker.c jdmaster.c \
  jmemmgr.c jpeg_nbits.c $(addprefix wrapper/,$(addsuffix -8.c,jdapistd jdcolor jddctmgr jdmainct \
  jdpostct jdsample jidctint jidctred jutils))) $(CDOM)/src/cdom_image_ljt_coef.c
CDOM_IMAGE_VENDOR_CFLAGS := -w -I$(CDOM_IMAGE_LJT) -DNO_GETENV -DNO_PUTENV
CDOM_IMAGE_VENDOR_SIMD_CFLAGS := $(cdom_mk_simd_flags) -I$(CDOM)/src/jsimd/simd
CDOM_IMAGE_INFLATE_SRC := $(CDOM)/src/cdom_image_inflate.c
CDOM_IMAGE_INFLATE_CFLAGS ?=
CDOM_IMAGE_INFLATE_SIMD_CFLAGS := -msimd128
CDOM_IMAGE_SJLJ_SRC = $(CDOM)/src/cdom_image_sjlj.c $(if $(filter eh,$(CDOM_IMAGE_SJLJ)),$(CDOM)/src/cdom_image_sjlj_eh.c)
cdom_mk_sjlj_js := -DCDOM_IMAGE_SJLJ_JS
cdom_mk_sjlj_eh := -DCDOM_IMAGE_SJLJ_EH -mexception-handling -mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=true
CDOM_IMAGE_SJLJ_CFLAGS = $(or $(cdom_mk_sjlj_$(CDOM_IMAGE_SJLJ)),$(error cdom.mk: CDOM_IMAGE_SJLJ is $(CDOM_IMAGE_SJLJ), not js or eh))
CDOM_IMAGE_DEPS := $(wildcard $(CDOM_IMAGE_LJT)/*.h $(CDOM_IMAGE_LJT)/*.c $(CDOM)/src/jsimd/*.c $(CDOM)/src/jsimd/simd/*.h)
CDOM_HEADERS    := $(wildcard $(CDOM)/src/*.h $(CDOM)/src/libc/*.h)
CDOM_SHIM       := $(CDOM)/shim/cdom.min.js

CDOM_PAGE       := $(CDOM)/shim/page.html
CDOM_LITS       := $(CDOM)/tools/out/cdlits
CDOM_PAGE_TOOL  := $(CDOM)/tools/out/cdpage

# The tool that moves the literals out of a linked module. Built to a temporary name and renamed, since
# several apps may build it at once. A pattern rule, so it never becomes the app's default goal.
%/out/cdlits: %/cdlits.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_STD) -O2 -Wall -Wextra -Werror -o $@.tmp$$$$ $< && mv $@.tmp$$$$ $@
# The tool that writes the page (tools/cdpage.c), built the same way.
%/out/cdpage: %/cdpage.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_STD) -O2 -Wall -Wextra -Werror -o $@.tmp$$$$ $< && mv $@.tmp$$$$ $@
# The wasm to JavaScript 1.0 compiler of the frames tier (tools/js1c.c, docs/js1-compile-design.md), built the same way.
%/out/js1c: %/js1c.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_STD) -O2 -Wall -Wextra -Werror -o $@.tmp$$$$ $< && mv $@.tmp$$$$ $@

# $(call cdom_lits,linked.wasm,out.wasm): the step after a link, for an app's own rules; it writes
# out.wasm and out.wasm.lits. Make the rule depend on $(CDOM_LITS).
cdom_lits = $(CDOM_LITS) $(1) $(2) $(2).lits

# $(call cdom_page,in.html,out.html[,module.wasm[,lits[,more cdpage arguments]]]): the page with the string table and the shim at
# its /*CDOM_SHIM*/ line and, when a module is named, that module bundled at its /*CDOM_WASM*/ (tools/cdpage);
# out.html is written only when the page holds its markers as the APP_PAGE rule needs them (the header
# above). Make the rule depend on $(CDOM_PAGE_TOOL).
cdom_page = $(call cdom_mk_page,$(1),$(2),$(3),,,$(if $(3),$(3).lits,$(4)),,,$(5))
# $(call cdom_mk_page,in,out,module to bundle,URL for /*CDOM_WASM*/,title for /*CDOM_TITLE*/,lits file[,SIMD twin to bundle,its URL[,more cdpage arguments]]): the recipe
# of cdom_page and the APP_PAGE rule, one run of tools/cdpage, which writes out only once the whole page is
# made. cdom_mk_q quotes a value for the shell.
cdom_mk_q = '$(subst ','\'',$(1))'
cdom_mk_page = $(CDOM_PAGE_TOOL) --page $(call cdom_mk_q,$(1)) --out $(call cdom_mk_q,$(2)) --shim $(call cdom_mk_q,$(CDOM_SHIM)) \
  --lits $(call cdom_mk_q,$(6))$(if $(3), --wasm $(call cdom_mk_q,$(3)))$(if $(4), --wasm-url $(call cdom_mk_q,$(4)))$(if $(5), --title $(call cdom_mk_q,$(5))) \
  $(if $(7),--simd-wasm $(call cdom_mk_q,$(7)) )$(if $(8),--simd-wasm-url $(call cdom_mk_q,$(8)) )--css-dir src $(9)

CDOM_MODULES := net store random audio url locale share fonts ui file image job

# The APP_WASM and APP_PAGE rules. Their targets are the names APP_WASM and APP_PAGE hold, which an app
# may set after this include, so they are pattern rules (%.wasm and %.html: APP_WASM ends in .wasm,
# APP_PAGE in .html) whose prerequisites take a second expansion (.SECONDEXPANSION, on from here for the
# app's later rules too, so a $$ in a prerequisite is expanded twice). That expansion runs once every
# makefile is read, so it sees APP_* wherever they were set: for the file APP_WASM (APP_PAGE) names, the
# prerequisites are its sources, and the APP_MODULES check runs there; for any other .wasm (.html) it is
# a file nothing makes, so the rule does not apply and the app's own rules, or none, do.
# cdom_mk_up: a name in capitals, for the variables a module's name keys (CDOM_<NAME>_SRC); strip takes off the spaces
# the continued lines put in.
cdom_mk_up = $(strip $(subst a,A,$(subst b,B,$(subst c,C,$(subst d,D,$(subst e,E,$(subst f,F,$(subst g,G,$(subst h,H,$(subst i,I,\
  $(subst j,J,$(subst k,K,$(subst l,L,$(subst m,M,$(subst n,N,$(subst o,O,$(subst p,P,$(subst q,Q,$(subst r,R,$(subst s,S,\
  $(subst t,T,$(subst u,U,$(subst v,V,$(subst w,W,$(subst x,X,$(subst y,Y,$(subst z,Z,$(1))))))))))))))))))))))))))))
cdom_mk_app_src = $(APP_SRC) $(CDOM_SRC) $(foreach m,$(APP_MODULES),$(CDOM_$(call cdom_mk_up,$(m))_SRC))
cdom_mk_unknown = $(filter-out $(CDOM_MODULES),$(APP_MODULES))
cdom_mk_known = $(if $(cdom_mk_unknown),$(error cdom.mk: APP_MODULES names $(cdom_mk_unknown), not a cdom module ($(CDOM_MODULES))))$(cdom_mk_unique)
# APP_SIMD := 1: the SIMD twin of APP_WASM, the same name with .simd.wasm for .wasm (app.wasm, app.simd.wasm); empty without it.
cdom_mk_simd_wasm = $(if $(filter 1,$(APP_SIMD)),$(patsubst %.wasm,%.simd.wasm,$(APP_WASM)))
# $(call cdom_mk_simd_on,module.wasm): 1 when that module is a SIMD build (the twin, or APP_WASM under CDOM_IMAGE_SIMD=1).
cdom_mk_simd_on = $(if $(and $(cdom_mk_simd_wasm),$(filter $(1),$(cdom_mk_simd_wasm))),1,$(filter 1,$(CDOM_IMAGE_SIMD)))
# The modules' parts: each file of CDOM_<NAME>_<PART>_SRC (and, in a SIMD build, of CDOM_<NAME>_<PART>_SIMD_SRC) is the
# object <module.wasm>.cdom/<name>/<part>/<base>.cdom.o, so the two builds of APP_SIMD keep their objects apart.
# $(call cdom_mk_part_src,name,part,simd): the part's files; $(call cdom_mk_objs,module.wasm): that module's objects.
cdom_mk_part_src = $(CDOM_$(call cdom_mk_up,$(1))_$(call cdom_mk_up,$(2))_SRC) $(if $(3),$(CDOM_$(call cdom_mk_up,$(1))_$(call cdom_mk_up,$(2))_SIMD_SRC))
cdom_mk_objs = $(foreach m,$(APP_MODULES),$(foreach p,$(CDOM_$(call cdom_mk_up,$(m))_PARTS),\
  $(foreach f,$(call cdom_mk_part_src,$(m),$(p),$(call cdom_mk_simd_on,$(1))),$(1).cdom/$(m)/$(p)/$(basename $(notdir $(f))).cdom.o)))
# APP_SIMD: unset, 0 or 1; with 1, APP_WASM itself stays scalar (CDOM_IMAGE_SIMD is 0), and some module of APP_MODULES
# has a SIMD build (a part with _SIMD_SRC or _SIMD_CFLAGS: image), or the twin would be APP_WASM again.
cdom_mk_simd_parts = $(strip $(foreach m,$(APP_MODULES),$(foreach p,$(CDOM_$(call cdom_mk_up,$(m))_PARTS),\
  $(CDOM_$(call cdom_mk_up,$(m))_$(call cdom_mk_up,$(p))_SIMD_SRC)$(CDOM_$(call cdom_mk_up,$(m))_$(call cdom_mk_up,$(p))_SIMD_CFLAGS))))
cdom_mk_simd_ok = $(if $(filter-out 0 1,$(APP_SIMD)),$(error cdom.mk: APP_SIMD is $(APP_SIMD), not 0 or 1))$(if $(cdom_mk_simd_wasm),\
  $(if $(filter 1,$(CDOM_IMAGE_SIMD)),$(error cdom.mk: APP_SIMD := 1 builds APP_WASM scalar and $(cdom_mk_simd_wasm) with SIMD; leave CDOM_IMAGE_SIMD at 0))\
  $(if $(cdom_mk_simd_parts),,$(error cdom.mk: APP_SIMD := 1, and no module of APP_MODULES ($(APP_MODULES)) has a SIMD build (image does))))
cdom_mk_mod_deps = $(foreach m,$(APP_MODULES),$(CDOM_$(call cdom_mk_up,$(m))_DEPS))
cdom_mk_mod_ldflags = $(foreach m,$(APP_MODULES),$(CDOM_$(call cdom_mk_up,$(m))_LDFLAGS))
# cdom_mk_unique stops at a part that names two files of one base name (their objects would be one file).
cdom_mk_dup = $(strip $(foreach f,$(1),$(if $(filter-out 1,$(words $(filter %/$(notdir $(f)) $(notdir $(f)),$(1)))),$(notdir $(f)))))
cdom_mk_unique = $(foreach m,$(APP_MODULES),$(foreach p,$(CDOM_$(call cdom_mk_up,$(m))_PARTS),\
  $(if $(call cdom_mk_dup,$(call cdom_mk_part_src,$(m),$(p),1)),$(error cdom.mk: part $(p) of module $(m) names two files called $(firstword $(call cdom_mk_dup,$(call cdom_mk_part_src,$(m),$(p),1)))))))
# An object's module (the .wasm whose .cdom/ it is under), its module's name, part and source, from its path (the words
# of the path under <module.wasm>.cdom/); the source is empty for a .cdom.o that is no object of this app's modules. A SIMD
# build's object takes the part's _SIMD_CFLAGS after its _CFLAGS.
cdom_mk_obj_wasm = $(if $(and $(cdom_mk_simd_wasm),$(filter $(cdom_mk_simd_wasm).cdom/%,$(1))),$(cdom_mk_simd_wasm),$(APP_WASM))
cdom_mk_obj_words = $(subst /, ,$(patsubst $(call cdom_mk_obj_wasm,$(1)).cdom/%,%,$(1)))
cdom_mk_obj_mod = $(word 1,$(call cdom_mk_obj_words,$(1)))
cdom_mk_obj_part = $(word 2,$(call cdom_mk_obj_words,$(1)))
cdom_mk_obj_simd = $(call cdom_mk_simd_on,$(call cdom_mk_obj_wasm,$(1)))
cdom_mk_obj_src = $(if $(filter $(1),$(call cdom_mk_objs,$(call cdom_mk_obj_wasm,$(1)))),$(firstword $(filter %/$(patsubst %.cdom.o,%,$(notdir $(1))).c \
  $(patsubst %.cdom.o,%,$(notdir $(1))).c,$(call cdom_mk_part_src,$(call cdom_mk_obj_mod,$(1)),$(call cdom_mk_obj_part,$(1)),$(call cdom_mk_obj_simd,$(1))))))
cdom_mk_obj_pv = CDOM_$(call cdom_mk_up,$(call cdom_mk_obj_mod,$(1)))_$(call cdom_mk_up,$(call cdom_mk_obj_part,$(1)))
cdom_mk_obj_flags = $($(call cdom_mk_obj_pv,$(1))_CFLAGS) $(if $(call cdom_mk_obj_simd,$(1)),$($(call cdom_mk_obj_pv,$(1))_SIMD_CFLAGS))
# $(call cdom_mk_for,name,target,prerequisites): the prerequisites when target is the file name holds,
# else one that nothing makes.
cdom_mk_for = $(if $(filter $(2),$(1)),$(3),$(2).cdom-mk-none)
.SECONDEXPANSION:
# The SIMD twin (APP_SIMD) is linked from the same sources and its own objects, after APP_WASM: the page carries one string
# table, APP_WASM's, for both, so the twin's must be the same bytes, or the build stops and removes the twin.
%.wasm: $$(call cdom_mk_for,$$(APP_WASM) $$(cdom_mk_simd_wasm),$$@,$$(cdom_mk_known)$$(cdom_mk_simd_ok)$$(cdom_mk_app_src) \
  $$(call cdom_mk_objs,$$@) $$(CDOM_HEADERS) $$(CDOM_LITS) $$(if $$(filter $$@,$$(cdom_mk_simd_wasm)),$$(APP_WASM)))
	@mkdir -p $(@D)
	@$(cdom_mk_toolchain)
	$(WASM_CC) $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) $(CDOM_LDFLAGS) $(cdom_mk_mod_ldflags) $(APP_LDFLAGS) -o $@.linked $(cdom_mk_app_src) $(call cdom_mk_objs,$@)
	$(call cdom_lits,$@.linked,$@)
	@rm -f $@.linked
	$(if $(filter $@,$(cdom_mk_simd_wasm)),@$(call cdom_mk_lits_same,$(APP_WASM),$@))
# $(call cdom_mk_lits_same,scalar.wasm,simd.wasm): the two string tables are the same bytes, or the twin is removed and make stops.
cdom_mk_lits_same = cmp -s $(call cdom_mk_q,$(1).lits) $(call cdom_mk_q,$(2).lits) || { rm -f $(call cdom_mk_q,$(2)) $(call cdom_mk_q,$(2).lits); \
  echo "cdom.mk: $(2) has other string literals than $(1) (the page's one table serves both: a SIMD build may not add or change one)" >&2; exit 1; }

# The job modules (APP_JOBS): $(dir $(APP_WASM))<name>.job.wasm, each its own link of cdom's heap alone. The rule applies only
# to a file APP_JOBS names, as the APP_WASM rule does; a job's name is the target's base name before .job.wasm.
cdom_mk_job_files = $(foreach j,$(APP_JOBS),$(dir $(APP_WASM))$(j).job.wasm)
cdom_mk_job_name = $(patsubst %.job.wasm,%,$(notdir $(1)))
cdom_mk_job_src = $(APP_JOB_$(call cdom_mk_job_name,$(1))_SRC) $(CDOM)/src/cdom.c $(CDOM)/src/mini.c $(CDOM_JOB_MOD_SRC)
# A job may link cdom modules of the app's (APP_JOB_<name>_MODULES := image): the app's own objects of their parts
# (the same build, so the same flags), every part but the glue to the file module, which reads on the page. The worker
# gives such a job the image module's js way out of a libjpeg error (shim/cdom.js, jobWorker).
cdom_mk_job_mods = $(APP_JOB_$(call cdom_mk_job_name,$(1))_MODULES)
cdom_mk_job_bad = $(filter-out $(APP_MODULES),$(call cdom_mk_job_mods,$(1)))
cdom_mk_job_objs = $(if $(call cdom_mk_job_bad,$(1)),$(error cdom.mk: job $(call cdom_mk_job_name,$(1)) names $(call cdom_mk_job_bad,$(1)), not in APP_MODULES)) \
  $(foreach m,$(call cdom_mk_job_mods,$(1)),$(filter-out $(APP_WASM).cdom/$(m)/file/%,$(filter $(APP_WASM).cdom/$(m)/%,$(call cdom_mk_objs,$(APP_WASM)))))
%.job.wasm: $$(call cdom_mk_for,$$(cdom_mk_job_files),$$@,$$(cdom_mk_job_src) $$(cdom_mk_job_objs) $$(CDOM_HEADERS))
	@mkdir -p $(@D)
	@$(cdom_mk_toolchain)
	$(WASM_CC) $(CDOM_OPT) $(CDOM_CFLAGS) -DCDOM_HEAP_ONLY $(APP_JOB_CFLAGS) $(CDOM_LDFLAGS) $(APP_JOB_LDFLAGS) -o $@ $(call cdom_mk_job_src,$@) $(call cdom_mk_job_objs,$@)

# A module part's object (the variables above): its file, compiled with the app's flags and then the part's. For
# any other .cdom.o the prerequisite is a file nothing makes, so this rule does not apply.
%.cdom.o: $$(or $$(call cdom_mk_obj_src,$$@),$$@.cdom-mk-none) $$(CDOM_HEADERS) $$(cdom_mk_mod_deps)
	@mkdir -p $(@D)
	@$(cdom_mk_toolchain)
	$(WASM_CC) $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) $(call cdom_mk_obj_flags,$@) -c -o $@ $(call cdom_mk_obj_src,$@)

# The page: APP_PAGE_SRC or the default page, the module it bundles (unless APP_WASM_SEPARATE), else the
# URL its /*CDOM_WASM*/ names, and the default page's title.
cdom_mk_page_src = $(or $(APP_PAGE_SRC),$(CDOM_PAGE))
cdom_mk_bundled = $(if $(APP_WASM_SEPARATE),,$(APP_WASM))
cdom_mk_wasm_url = $(if $(APP_WASM_SEPARATE),$(or $(APP_WASM_URL),$(notdir $(APP_WASM))))
# The page's job modules, bundled (unless APP_WASM_SEPARATE): one --job name=file each.
cdom_mk_job_args = $(if $(APP_WASM_SEPARATE),,$(foreach j,$(APP_JOBS),--job $(call cdom_mk_q,$(j).job.wasm=$(dir $(APP_WASM))$(j).job.wasm)))
cdom_mk_title = $(if $(APP_PAGE_SRC),,$(or $(APP_TITLE),$(basename $(notdir $(APP_WASM)))))
# APP_SIMD: the twin, bundled too, or its URL: APP_SIMD_WASM_URL, else the scalar URL with .simd.wasm for its .wasm.
cdom_mk_simd_bundled = $(if $(APP_WASM_SEPARATE),,$(cdom_mk_simd_wasm))
cdom_mk_simd_url = $(strip $(if $(and $(APP_WASM_SEPARATE),$(cdom_mk_simd_wasm)),$(or $(APP_SIMD_WASM_URL),$(if $(filter %.wasm,$(cdom_mk_wasm_url)),\
  $(patsubst %.wasm,%.simd.wasm,$(cdom_mk_wasm_url)),$(error cdom.mk: APP_WASM_URL $(cdom_mk_wasm_url) does not end in .wasm; set APP_SIMD_WASM_URL, the SIMD twin's)))))
# The page's stylesheets, as a prerequisite: $(APP_PAGE).csslist, the list tools/cdpage --css-list writes
# of the .css files under src/ with a hash of each, which it replaces only when that text changes. A
# list of the .css files themselves could not see one removed, nor one added with an older time. It is
# brought up to date here, while make works out the page's prerequisites, so make -q answers truthfully
# (an always-run rule would never let it say "up to date"); for the APP_PAGE target only, since only the
# chosen branch of an $(if) is expanded. The tool is not there before the first build, nor the page's
# directory: then the page's recipe writes the list, and the empty rule makes a missing list a reason to
# build the page, not an error.
cdom_mk_csslist = $(if $(filter $@,$(APP_PAGE)),$(shell [ -x $(call cdom_mk_q,$(CDOM_PAGE_TOOL)) ] && [ -d $(call cdom_mk_q,$(@D)) ] \
  && $(call cdom_mk_q,$(CDOM_PAGE_TOOL)) --css-list src $(call cdom_mk_q,$@.csslist))$@.csslist)
%.csslist: ;
# The no-script fallback (docs/noscript-design.md): what a browser with no script at all shows (NCSA Mosaic, IE 1.5 and
# 2, Netscape 1.1, Lynx), at the page's /*CDOM_NOSCRIPT*/ line, which the default page has in its <body> and an app's
# own page may add. CDOM_NOSCRIPT := 0 turns it off (the marker line is then removed, and the page is as it was).
# With it on, tools/cdpage writes the block (a screenshot, Alex's paragraph, the snowman, in HTML 2.0) and wraps the
# page's scripts and style in comments, and tools/noscript.mjs then makes the two GIFs beside the page:
# <page's base name>.noscript.gif, a screenshot of the built page in headless Chrome at CDOM_NOSCRIPT_SHOT_SIZE, and
# CDOM_NOSCRIPT_SNOWMAN, U+2603 rendered at CDOM_NOSCRIPT_SNOWMAN_SIZE, each through tools/noscript_gif.py (Python 3 and
# Pillow: PYTHON). That step takes Node (CDOM_NODE), Chrome (CHROME) and Python only for a page with the block, and
# its content stamp (<page>.noscript) launches nothing when the page and what it loads are the same bytes as before.
# A failed step removes the page, so the next make tries again.
CDOM_NOSCRIPT ?= 1
CDOM_NOSCRIPT_SHOT_SIZE ?= 640x480
CDOM_NOSCRIPT_SNOWMAN ?= cdom-snowman.gif
CDOM_NOSCRIPT_SNOWMAN_SIZE ?= 96x96
CDOM_NODE ?= node
cdom_mk_noscript = $(if $(filter-out 0 1,$(CDOM_NOSCRIPT)),$(error cdom.mk: CDOM_NOSCRIPT is $(CDOM_NOSCRIPT), not 0 or 1))$(filter 1,$(CDOM_NOSCRIPT))
cdom_mk_shot_name = $(basename $(notdir $(APP_PAGE))).noscript.gif
cdom_mk_shot = $(cdom_mk_shot_name)=$(CDOM_NOSCRIPT_SHOT_SIZE)
cdom_mk_snowman = $(CDOM_NOSCRIPT_SNOWMAN)=$(CDOM_NOSCRIPT_SNOWMAN_SIZE)
cdom_mk_noscript_args = $(if $(cdom_mk_noscript),--noscript-shot $(call cdom_mk_q,$(cdom_mk_shot)) --noscript-snowman $(call cdom_mk_q,$(cdom_mk_snowman)))
cdom_mk_noscript_tools = $(if $(cdom_mk_noscript),$(CDOM)/tools/noscript.mjs $(CDOM)/tools/noscript_gif.py $(CDOM)/tools/cdp.mjs)
cdom_mk_noscript_step = if grep -qF $(call cdom_mk_q,<img src="$(cdom_mk_shot_name)") $(call cdom_mk_q,$@); then \
  $(CDOM_NODE) $(call cdom_mk_q,$(CDOM)/tools/noscript.mjs) $(call cdom_mk_q,$@) $(call cdom_mk_q,$(cdom_mk_shot)) $(call cdom_mk_q,$(cdom_mk_snowman)) \
  || { rm -f $(call cdom_mk_q,$@); exit 1; }; fi
%.html: $$(call cdom_mk_for,$$(APP_PAGE),$$@,$$(cdom_mk_page_src) $$(CDOM_SHIM) $$(CDOM_PAGE_TOOL) $$(APP_WASM) $$(cdom_mk_simd_wasm) $$(cdom_mk_job_files) $$(cdom_mk_noscript_tools)) $$(cdom_mk_csslist)
	$(if $(APP_PAGE_SRC)$(APP_WASM),,$(error cdom.mk: the default page starts APP_WASM, and APP_WASM is unset))
	@mkdir -p $(@D)
	$(CDOM_PAGE_TOOL) --css-list src $(call cdom_mk_q,$@.csslist)
	$(call cdom_mk_page,$(cdom_mk_page_src),$@,$(cdom_mk_bundled),$(cdom_mk_wasm_url),$(cdom_mk_title),$(APP_WASM).lits,$(cdom_mk_simd_bundled),$(cdom_mk_simd_url),$(cdom_mk_job_args) $(cdom_mk_noscript_args))
	$(if $(cdom_mk_noscript),$(cdom_mk_noscript_step))

# The toolchain check before the APP_WASM rule compiles: a module through WASM_CC, its wasm32 target and
# wasm-ld, that takes the address of __rodata_start, which wasm-ld defines from LLVM 23 on (src/cdom.c,
# cd_tmpz, needs it, so an older linker fails there). When the probe fails, make stops with what it found
# and the command that installs a toolchain, not at a linker error: a WASM_CC that builds no wasm32 module
# at all (Apple's clang finds no wasm-ld; WASM_CC is not there), or one whose wasm-ld is older than 23
# (Ubuntu 24.04's clang 18, Debian 13's clang 19), named with its version.
cdom_mk_install = install LLVM 23 or later: brew install llvm lld   (Debian or Ubuntu: clang-23 and lld-23 from https://apt.llvm.org, then WASM_CC=/usr/lib/llvm-23/bin/clang)
cdom_mk_probe = $(WASM_CC) --target=wasm32 -nostdlib -Wl,--no-entry -x c -o '$@.probe' - 2> /dev/null
cdom_mk_toolchain = printf 'extern const char __rodata_start[];\n__attribute__((export_name("f"))) const char *f(void) { return __rodata_start; }\n' \
  | $(cdom_mk_probe) && rm -f '$@.probe' || { \
  if printf 'void f(void) {}\n' | $(cdom_mk_probe); then rm -f '$@.probe'; \
    echo "cdom.mk: $(WASM_CC) links with $$("$$($(WASM_CC) --target=wasm32 -print-prog-name=wasm-ld)" --version 2> /dev/null | head -1), which does not define __rodata_start: cdom needs wasm-ld from LLVM 23 or later" >&2; \
  else rm -f '$@.probe'; \
    echo "cdom.mk: $(WASM_CC) cannot build a wasm32 module (that takes clang with the wasm32 target, and wasm-ld)" >&2; fi; \
  echo "cdom.mk: $(cdom_mk_install)" >&2; \
  exit 1; }

# The sources, named. A pattern rule whose prerequisite neither exists nor is named by any rule does not
# apply: with a source missing, make would keep a module built before ("Nothing to be done") or say "No
# rule to make target <the app's .wasm>". Named here (an explicit rule's prerequisites, expanded as
# late), the rule still applies and make stops at the missing source, naming it. APP_WASM is named too:
# a bundled page is built from it through the two pattern rules, and make deletes such an intermediate
# file once the page is made unless a rule names it. So are cdom's two tools, CDOM_LITS and CDOM_PAGE_TOOL,
# and the page's stylesheet list, all made by pattern rules as well: as intermediate files, make would
# remove a tool it had to build once the build is done, and not build it again while what was made from it
# is newer (so the list, which the tool brings up to date as make starts, would go stale), and it would
# remove the list after every page it writes, so the next make would always write the page again. Nothing
# builds this target, and one that starts with a dot is never the default goal.
.cdom-mk-sources: $$(if $$(APP_WASM),$$(APP_WASM) $$(cdom_mk_app_src) $$(call cdom_mk_objs,$$(APP_WASM)) $$(CDOM_LITS)) \
  $$(if $$(cdom_mk_simd_wasm),$$(cdom_mk_simd_wasm) $$(call cdom_mk_objs,$$(cdom_mk_simd_wasm))) \
  $$(if $$(APP_PAGE),$$(cdom_mk_page_src) $$(CDOM_PAGE_TOOL) $$(APP_PAGE).csslist)
