# The Ultimate Tic-Tac-Toe replay viewer (uttt_replay.c), built from foolish's rules and code at FOOLISH (a checkout
# of github.com/alexsave/foolish; nothing of it is copied here) into out/:
#   out/uttt.html          the page: every tier, from WebAssembly down to the frames tier (Netscape 2 to 4, IE 3 and 4)
#   out/uttt.wasm          the module the WebAssembly tier starts
#   out/uttt.wasm.ie9.js   the same module as words-build JavaScript, which the frames tier (and IE 6 to 9) runs
#   out/cdom.frames.js     the frames tier's shim
# make -C demo/uttt [FOOLISH=path/to/foolish]; serve out/ and open uttt.html, or uttt.html?<code>.
CDOM := $(abspath ../..)
FOOLISH ?= $(abspath $(CDOM)/../foolish)
UTTT := $(FOOLISH)/uttt/c/src
NODE ?= node
LEGACY_BUILD := $(CDOM)/tools/legacy/build.mjs

APP_WASM := out/uttt.wasm
APP_SRC := uttt_replay.c $(UTTT)/uttt.c $(UTTT)/uttt_code.c $(FOOLISH)/shared/c/b32.c $(FOOLISH)/shared/c/mixrad.c
APP_MODULES := url
# libc/math.h declares log2 (never called) for foolish's uttt_code.c; strncmp is cdom's (src/libc/string.h).
APP_CFLAGS := -I$(UTTT) -isystem libc -Wall -Wextra -Werror
APP_LDFLAGS := -Wl,--strip-all
APP_WASM_SEPARATE := 1
APP_PAGE_SRC := uttt.html
APP_PAGE := out/uttt.shim.html
CDOM_NOSCRIPT := 0

all: out/uttt.html out/uttt.wasm.ie9.js out/cdom.frames.js
.PHONY: all clean
include $(CDOM)/cdom.mk

$(UTTT)/uttt.c:
	@echo "demo/uttt: no foolish checkout at FOOLISH=$(FOOLISH) (git clone https://github.com/alexsave/foolish there, or set FOOLISH)" >&2; exit 1

# The boot (shim/boot.js) inlined at /*CDOM_BOOT*/: the page every tier opens.
out/uttt.html: out/uttt.shim.html $(LEGACY_BUILD) $(CDOM)/shim/boot.js
	$(NODE) $(LEGACY_BUILD) page $< $@
out/uttt.wasm.ie9.js: out/uttt.wasm $(LEGACY_BUILD)
	$(NODE) $(LEGACY_BUILD) app-ie9 $< $@
out/cdom.frames.js: $(LEGACY_BUILD) $(CDOM)/tools/legacy/frames.js
	@mkdir -p out
	$(NODE) $(LEGACY_BUILD) shim-frames $@

clean:
	rm -rf out
