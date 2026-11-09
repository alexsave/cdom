# cdom.mk - what an app's Makefile includes to build on cdom (README.md, "Building an app"):
#
#   CDOM := path/to/cdom
#   include $(CDOM)/cdom.mk
#
# It defines variables and, only when the app sets APP_WASM or APP_PAGE (before the include, as the
# rules read them there), the rule for that file. It never changes the app's default goal. Plain make, awk and clang: no Node on this path.
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
#   APP_PAGE, APP_PAGE_SRC
#                 the app's page: $(APP_PAGE) is $(APP_PAGE_SRC) with the shim inlined at the line that
#                 holds only the marker /*CDOM_SHIM*/ (inside a <script>); it fails unless exactly one
#                 such line is there. $(call cdom_page,in.html,out.html) does the same in a recipe.
#
# The flags: wasm32 with no C library but cdom's own (-nostdlib, then -isystem $(CDOM)/src/libc for
# string.h, whose memcpy, memmove and memset are bulk-memory instructions, hence -mbulk-memory), cdom's
# headers on the include path, a module with no entry point (cdom's exports are its entries), the
# memory exported (the shim reads and writes C memory through it), and a 64 KB stack placed first, below
# the data, so an overflow traps instead of overwriting globals. -ffreestanding is not in the set (it
# turns off clang's builtins and costs cdom code size); string.h works under it, so an app may add it.
# Optimization, warnings and -Wl,--strip-all are the app's choice (APP_CFLAGS, APP_LDFLAGS, CDOM_OPT).
cdom_mk_goal := $(.DEFAULT_GOAL)

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

# $(call cdom_page,in.html,out.html): the page with the shim inlined at its /*CDOM_SHIM*/ line
# (tools/page.awk); out.html is written only when the page holds that line exactly once.
cdom_page = awk -v shim='$(CDOM_SHIM)' -f '$(CDOM)/tools/page.awk' '$(1)' > '$(2).tmp' && mv '$(2).tmp' '$(2)' || { rm -f '$(2).tmp'; exit 1; }

CDOM_MODULES := net store random audio url locale share fonts
ifdef APP_WASM
cdom_mk_unknown := $(filter-out $(CDOM_MODULES),$(APP_MODULES))
ifneq ($(cdom_mk_unknown),)
$(error cdom.mk: APP_MODULES names $(cdom_mk_unknown), not a cdom module ($(CDOM_MODULES)))
endif
cdom_mk_app_src := $(APP_SRC) $(CDOM_SRC) $(addprefix $(CDOM)/src/cdom_,$(addsuffix .c,$(APP_MODULES)))
$(APP_WASM): $(cdom_mk_app_src) $(CDOM_HEADERS)
	@mkdir -p $(@D)
	$(WASM_CC) $(CDOM_OPT) $(CDOM_CFLAGS) $(APP_CFLAGS) $(CDOM_LDFLAGS) $(APP_LDFLAGS) -o $@ $(cdom_mk_app_src)
endif

ifdef APP_PAGE
$(APP_PAGE): $(APP_PAGE_SRC) $(CDOM_SHIM) $(CDOM)/tools/page.awk
	@mkdir -p $(@D)
	$(call cdom_page,$(APP_PAGE_SRC),$@)
endif

.DEFAULT_GOAL := $(cdom_mk_goal)
