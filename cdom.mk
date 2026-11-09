# cdom.mk - what an app's Makefile includes to build on cdom (README.md, "Building an app"):
#
#   CDOM := path/to/cdom
#   include $(CDOM)/cdom.mk
#
# Included once, before or after the app sets APP_*: it defines variables and the rules for the files
# APP_WASM and APP_PAGE name, which read APP_* when make builds, not where cdom.mk is included (see the
# rules below). It never changes the app's default goal. Plain make, awk and clang: no Node on this path.
#
#   CDOM_CFLAGS   the compile flags every file of a cdom module needs
#   CDOM_LDFLAGS  the link flags of a cdom module
#   CDOM_SRC      the core sources every app links (cdom.c, and mini.c, the program's libc)
#   CDOM_NET_SRC, CDOM_STORE_SRC, CDOM_RANDOM_SRC, CDOM_AUDIO_SRC, CDOM_URL_SRC, CDOM_LOCALE_SRC,
#   CDOM_SHARE_SRC, CDOM_FONTS_SRC
#                 one optional module each (src/cdom_<name>.h), for an app's own rules (APP_MODULES below
#                 adds them to the APP_WASM rule)
#   CDOM_SHIM     the minified shim (shim/cdom.min.js) a page inlines
#   CDOM_OPT      the optimization level of the APP_WASM rule (-Oz unless the app sets it)
#   WASM_CC       a clang that targets wasm32 (Homebrew's llvm unless set; Apple's clang does not)
#
#   APP_WASM, APP_SRC, APP_MODULES, APP_CFLAGS, APP_LDFLAGS
#                 the app's module: $(APP_WASM) is built from $(APP_SRC), $(CDOM_SRC) and the optional
#                 modules APP_MODULES names (net store random audio url locale share fonts: their
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
#                 $(call cdom_page,in.html,out.html) inlines the shim in a recipe, and
#                 $(call cdom_page,in.html,out.html,module.wasm) bundles that module as well.
#
# The default page (shim/page.html), the contract other repos may name as "cdom's default page": a
# doctype, <meta charset="utf-8">, the viewport meta (width=device-width, initial-scale=1), a <title> of
# APP_TITLE (HTML-escaped; the base name of APP_WASM unless set), a <body> that holds only
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
# its app needs and this include, src/app.c and a .gitignore: nothing this file could own.
#
# The flags: wasm32 with no C library but cdom's own (-nostdlib, then -isystem $(CDOM)/src/libc for
# string.h, whose memcpy, memmove and memset are bulk-memory instructions, hence -mbulk-memory), cdom's
# headers on the include path, a module with no entry point (cdom's exports are its entries), the
# memory exported (the shim reads and writes C memory through it), and a 64 KB stack placed first, below
# the data, so an overflow traps instead of overwriting globals. -ffreestanding is not in the set (it
# turns off clang's builtins and costs cdom code size); string.h works under it, so an app may add it.
# Optimization, warnings and -Wl,--strip-all are the app's choice (APP_CFLAGS, APP_LDFLAGS, CDOM_OPT).
CDOM ?= $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
WASM_CC ?= /opt/homebrew/opt/llvm/bin/clang
CDOM_OPT ?= -Oz

CDOM_CFLAGS := --target=wasm32 -nostdlib -mbulk-memory -isystem $(CDOM)/src/libc -I$(CDOM)/src
CDOM_LDFLAGS := -Wl,--no-entry -Wl,--export-memory -Wl,--stack-first -Wl,-z,stack-size=65536

CDOM_SRC        := $(CDOM)/src/cdom.c $(CDOM)/src/mini.c
CDOM_NET_SRC    := $(CDOM)/src/cdom_net.c
CDOM_STORE_SRC  := $(CDOM)/src/cdom_store.c
CDOM_RANDOM_SRC := $(CDOM)/src/cdom_random.c
CDOM_AUDIO_SRC  := $(CDOM)/src/cdom_audio.c
CDOM_URL_SRC    := $(CDOM)/src/cdom_url.c
CDOM_LOCALE_SRC := $(CDOM)/src/cdom_locale.c
CDOM_SHARE_SRC  := $(CDOM)/src/cdom_share.c
CDOM_FONTS_SRC  := $(CDOM)/src/cdom_fonts.c
CDOM_HEADERS    := $(wildcard $(CDOM)/src/*.h $(CDOM)/src/libc/*.h)
CDOM_SHIM       := $(CDOM)/shim/cdom.min.js

CDOM_PAGE       := $(CDOM)/shim/page.html

# $(call cdom_page,in.html,out.html[,module.wasm]): the page with the shim inlined at its /*CDOM_SHIM*/
# line and, when a module is named, that module bundled at its /*CDOM_WASM*/ (tools/page.awk); out.html
# is written only when the page holds its markers as the APP_PAGE rule needs them (the header above).
cdom_page = $(call cdom_mk_page,$(1),$(2),$(3))
# $(call cdom_mk_page,in,out,module to bundle,URL for /*CDOM_WASM*/,title for /*CDOM_TITLE*/): the recipe
# of cdom_page and the APP_PAGE rule. base64's output is joined into one line (macOS's base64 writes one,
# GNU's wraps at 76 columns). cdom_mk_q quotes a value for the shell.
cdom_mk_q = '$(subst ','\'',$(1))'
cdom_mk_page = $(if $(3),base64 < $(call cdom_mk_q,$(3)) | tr -d '\n\r' | )CDOM_WASM_URL=$(call cdom_mk_q,$(4)) \
  CDOM_TITLE=$(call cdom_mk_q,$(5)) awk -v shim=$(call cdom_mk_q,$(CDOM_SHIM)) -v wasm=$(if $(3),bundle,$(if $(4),url)) \
  $(if $(5),-v title=1 )-f $(call cdom_mk_q,$(CDOM)/tools/page.awk) $(call cdom_mk_q,$(1)) > $(call cdom_mk_q,$(2).tmp) \
  && mv $(call cdom_mk_q,$(2).tmp) $(call cdom_mk_q,$(2)) || { rm -f $(call cdom_mk_q,$(2).tmp); exit 1; }

CDOM_MODULES := net store random audio url locale share fonts

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
%.wasm: $$(call cdom_mk_for,$$(APP_WASM),$$@,$$(cdom_mk_known)$$(cdom_mk_app_src) $$(CDOM_HEADERS))
	@mkdir -p $(@D)
	@$(cdom_mk_toolchain)
	$(WASM_CC) $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) $(CDOM_LDFLAGS) $(APP_LDFLAGS) -o $@ $(cdom_mk_app_src)

# The page: APP_PAGE_SRC or the default page, the module it bundles (unless APP_WASM_SEPARATE), else the
# URL its /*CDOM_WASM*/ names, and the default page's title.
cdom_mk_page_src = $(or $(APP_PAGE_SRC),$(CDOM_PAGE))
cdom_mk_bundled = $(if $(APP_WASM_SEPARATE),,$(APP_WASM))
cdom_mk_wasm_url = $(if $(APP_WASM_SEPARATE),$(or $(APP_WASM_URL),$(notdir $(APP_WASM))))
cdom_mk_title = $(if $(APP_PAGE_SRC),,$(or $(APP_TITLE),$(basename $(notdir $(APP_WASM)))))
%.html: $$(call cdom_mk_for,$$(APP_PAGE),$$@,$$(cdom_mk_page_src) $$(CDOM_SHIM) $$(CDOM)/tools/page.awk $$(cdom_mk_bundled))
	$(if $(APP_PAGE_SRC)$(APP_WASM),,$(error cdom.mk: the default page starts APP_WASM, and APP_WASM is unset))
	@mkdir -p $(@D)
	$(call cdom_mk_page,$(cdom_mk_page_src),$@,$(cdom_mk_bundled),$(cdom_mk_wasm_url),$(cdom_mk_title))

# The toolchain check before the APP_WASM rule compiles: a one-function module through WASM_CC, its
# wasm32 target and wasm-ld. When that fails (Apple's clang finds no wasm-ld; WASM_CC is not there), make
# stops with the command that installs a toolchain, not at a linker error.
cdom_mk_toolchain = printf 'void f(void) {}\n' | $(WASM_CC) --target=wasm32 -nostdlib -Wl,--no-entry -x c -o '$@.probe' - \
  2> /dev/null && rm -f '$@.probe' || { rm -f '$@.probe'; \
  echo "cdom.mk: $(WASM_CC) cannot build a wasm32 module (that takes clang with the wasm32 target, and wasm-ld)" >&2; \
  echo "cdom.mk: install them with: brew install llvm lld   (Debian or Ubuntu: apt install clang lld, and set WASM_CC := clang)" >&2; \
  exit 1; }

# The sources, named. A pattern rule whose prerequisite neither exists nor is named by any rule does not
# apply: with a source missing, make would keep a module built before ("Nothing to be done") or say "No
# rule to make target <the app's .wasm>". Named here (an explicit rule's prerequisites, expanded as
# late), the rule still applies and make stops at the missing source, naming it. APP_WASM is named too:
# a bundled page is built from it through the two pattern rules, and make deletes such an intermediate
# file once the page is made unless a rule names it. Nothing builds this target, and one that starts with
# a dot is never the default goal.
.cdom-mk-sources: $$(if $$(APP_WASM),$$(APP_WASM) $$(cdom_mk_app_src)) $$(if $$(APP_PAGE),$$(cdom_mk_page_src))
