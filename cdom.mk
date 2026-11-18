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
#   CDOM_SHARE_SRC, CDOM_FONTS_SRC, CDOM_UI_SRC, CDOM_FILE_SRC
#                 one optional module each (src/cdom_<name>.h), for an app's own rules (APP_MODULES below
#                 adds them to the APP_WASM rule)
#   CDOM_SHIM     the minified shim (shim/cdom.min.js) a page inlines
#   CDOM_OPT      the optimization level of the APP_WASM rule (-Oz unless the app sets it)
#   WASM_CC       a clang that targets wasm32 (Homebrew's llvm on macOS unless set, since Apple's clang does not;
#                 clang elsewhere)
#   HOST_CC       the C compiler for cdom's build tools (tools/cdlits.c, tools/cdpage.c), cc unless set
#   CDOM_LITS     the built tools/cdlits, which every link goes through: it strips the string table out of
#                 the module and writes it beside it as $(APP_WASM).lits, for the page
#   CDOM_PAGE_TOOL the built tools/cdpage, which writes every page
#
#   APP_WASM, APP_SRC, APP_MODULES, APP_CFLAGS, APP_LDFLAGS
#                 the app's module: $(APP_WASM) is built from $(APP_SRC), $(CDOM_SRC) and the optional
#                 modules APP_MODULES names (net store random audio url locale share fonts ui file: their
#                 CDOM_<NAME>_SRC) with $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) $(CDOM_LDFLAGS) $(APP_LDFLAGS)
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
# app's stylesheets (empty when src/ has none), a <body> that holds only
# <div id="app"></div>, the app's mount, and one <script>: the shim and the one start call,
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
# The APP_WASM rule first checks that WASM_CC builds a wasm32 module at all, and when it does not (Apple's
# clang, or no wasm-ld), stops with the command that installs one, instead of a linker error.
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
# Homebrew's llvm on macOS (Apple's clang has no wasm32), the clang on the PATH elsewhere (Linux
# distributions' clang targets wasm32; wasm-ld comes with lld).
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

# $(call cdom_lits,linked.wasm,out.wasm): the step after a link, for an app's own rules; it writes
# out.wasm and out.wasm.lits. Make the rule depend on $(CDOM_LITS).
cdom_lits = $(CDOM_LITS) $(1) $(2) $(2).lits

# $(call cdom_page,in.html,out.html[,module.wasm[,lits]]): the page with the string table and the shim at
# its /*CDOM_SHIM*/ line and, when a module is named, that module bundled at its /*CDOM_WASM*/ (tools/cdpage);
# out.html is written only when the page holds its markers as the APP_PAGE rule needs them (the header
# above). Make the rule depend on $(CDOM_PAGE_TOOL).
cdom_page = $(call cdom_mk_page,$(1),$(2),$(3),,,$(if $(3),$(3).lits,$(4)))
# $(call cdom_mk_page,in,out,module to bundle,URL for /*CDOM_WASM*/,title for /*CDOM_TITLE*/,lits file): the recipe
# of cdom_page and the APP_PAGE rule, one run of tools/cdpage, which writes out only once the whole page is
# made. cdom_mk_q quotes a value for the shell.
cdom_mk_q = '$(subst ','\'',$(1))'
cdom_mk_page = $(CDOM_PAGE_TOOL) --page $(call cdom_mk_q,$(1)) --out $(call cdom_mk_q,$(2)) --shim $(call cdom_mk_q,$(CDOM_SHIM)) \
  --lits $(call cdom_mk_q,$(6))$(if $(3), --wasm $(call cdom_mk_q,$(3)))$(if $(4), --wasm-url $(call cdom_mk_q,$(4)))$(if $(5), --title $(call cdom_mk_q,$(5))) \
  --css-dir src

CDOM_MODULES := net store random audio url locale share fonts ui file

# The APP_WASM and APP_PAGE rules. Their targets are the names APP_WASM and APP_PAGE hold, which an app
# may set after this include, so they are pattern rules (%.wasm and %.html: APP_WASM ends in .wasm,
# APP_PAGE in .html) whose prerequisites take a second expansion (.SECONDEXPANSION, on from here for the
# app's later rules too, so a $$ in a prerequisite is expanded twice). That expansion runs once every
# makefile is read, so it sees APP_* wherever they were set: for the file APP_WASM (APP_PAGE) names, the
# prerequisites are its sources, and the APP_MODULES check runs there; for any other .wasm (.html) it is
# a file nothing makes, so the rule does not apply and the app's own rules, or none, do.
cdom_mk_app_src = $(APP_SRC) $(CDOM_SRC) $(addprefix $(CDOM)/src/cdom_,$(addsuffix .c,$(APP_MODULES)))
cdom_mk_unknown = $(filter-out $(CDOM_MODULES),$(APP_MODULES))
cdom_mk_known = $(if $(cdom_mk_unknown),$(error cdom.mk: APP_MODULES names $(cdom_mk_unknown), not a cdom module ($(CDOM_MODULES))))
# $(call cdom_mk_for,name,target,prerequisites): the prerequisites when target is the file name holds,
# else one that nothing makes.
cdom_mk_for = $(if $(filter $(2),$(1)),$(3),$(2).cdom-mk-none)
.SECONDEXPANSION:
%.wasm: $$(call cdom_mk_for,$$(APP_WASM),$$@,$$(cdom_mk_known)$$(cdom_mk_app_src) $$(CDOM_HEADERS) $$(CDOM_LITS))
	@mkdir -p $(@D)
	@$(cdom_mk_toolchain)
	$(WASM_CC) $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) $(CDOM_LDFLAGS) $(APP_LDFLAGS) -o $@.linked $(cdom_mk_app_src)
	$(call cdom_lits,$@.linked,$@)
	@rm -f $@.linked

# The page: APP_PAGE_SRC or the default page, the module it bundles (unless APP_WASM_SEPARATE), else the
# URL its /*CDOM_WASM*/ names, and the default page's title.
cdom_mk_page_src = $(or $(APP_PAGE_SRC),$(CDOM_PAGE))
cdom_mk_bundled = $(if $(APP_WASM_SEPARATE),,$(APP_WASM))
cdom_mk_wasm_url = $(if $(APP_WASM_SEPARATE),$(or $(APP_WASM_URL),$(notdir $(APP_WASM))))
cdom_mk_title = $(if $(APP_PAGE_SRC),,$(or $(APP_TITLE),$(basename $(notdir $(APP_WASM)))))
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
%.html: $$(call cdom_mk_for,$$(APP_PAGE),$$@,$$(cdom_mk_page_src) $$(CDOM_SHIM) $$(CDOM_PAGE_TOOL) $$(APP_WASM)) $$(cdom_mk_csslist)
	$(if $(APP_PAGE_SRC)$(APP_WASM),,$(error cdom.mk: the default page starts APP_WASM, and APP_WASM is unset))
	@mkdir -p $(@D)
	$(CDOM_PAGE_TOOL) --css-list src $(call cdom_mk_q,$@.csslist)
	$(call cdom_mk_page,$(cdom_mk_page_src),$@,$(cdom_mk_bundled),$(cdom_mk_wasm_url),$(cdom_mk_title),$(APP_WASM).lits)

# The toolchain check before the APP_WASM rule compiles: a one-function module through WASM_CC, its
# wasm32 target and wasm-ld. When that fails (Apple's clang finds no wasm-ld; WASM_CC is not there), make
# stops with the command that installs a toolchain, not at a linker error.
cdom_mk_toolchain = printf 'void f(void) {}\n' | $(WASM_CC) --target=wasm32 -nostdlib -Wl,--no-entry -x c -o '$@.probe' - \
  2> /dev/null && rm -f '$@.probe' || { rm -f '$@.probe'; \
  echo "cdom.mk: $(WASM_CC) cannot build a wasm32 module (that takes clang with the wasm32 target, and wasm-ld)" >&2; \
  echo "cdom.mk: install them with: brew install llvm lld   (Debian or Ubuntu: apt install clang lld)" >&2; \
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
.cdom-mk-sources: $$(if $$(APP_WASM),$$(APP_WASM) $$(cdom_mk_app_src) $$(CDOM_LITS)) \
  $$(if $$(APP_PAGE),$$(cdom_mk_page_src) $$(CDOM_PAGE_TOOL) $$(APP_PAGE).csslist)
