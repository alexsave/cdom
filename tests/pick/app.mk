# The SIMD pick's test app (tests/pick/run.sh, tools/pick.mjs): bench/image's app and page, built as an app builds them, with
# APP_SIMD := 1, so cdom.mk writes OUT/app.wasm (scalar) and OUT/app.simd.wasm, and the page that starts one of them:
#   make -f tests/pick/app.mk OUT=dir PAGE=name [APP_WASM_SEPARATE=1] [CDOM_SHIM=shim/cdom.js]
# writes OUT/PAGE.html, the modules bundled (or, separate, named by URL); with the readable shim on the command line, so
# tools/mutate.py's mutants of shim/cdom.js reach the page, else the minified one apps inline.
CDOM := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
APP_WASM := $(OUT)/app.wasm
APP_PAGE := $(OUT)/$(PAGE).html
APP_PAGE_SRC := $(CDOM)/bench/image/page.html
APP_SRC := $(CDOM)/bench/image/app.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror
APP_SIMD := 1
include $(CDOM)/cdom.mk
all: $(APP_PAGE)
