# The image module's wasm test app (tests/image/wasm.c), built as an app builds it, with cdom.mk:
#   make -f tests/image/wasm.mk OUT=tests/out/image SJLJ=js|eh [CAP=pages] [MAXMEM=bytes]
# writes OUT/wasm_<SJLJ>[_cap][_max].html, the page with the module bundled. CAP caps cdom's heap
# (CDOM_HEAP_PAGES_MAX); MAXMEM caps the module's memory itself (wasm-ld --max-memory), so memory.grow refuses.
CDOM := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
NAME := wasm_$(SJLJ)$(if $(CAP),_cap)$(if $(MAXMEM),_max)
APP_WASM := $(OUT)/$(NAME).wasm
APP_PAGE := $(OUT)/$(NAME).html
APP_PAGE_SRC := $(CDOM)/tests/image/wasm.html
APP_SRC := $(CDOM)/tests/image/wasm.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror $(if $(CAP),-DCDOM_HEAP_PAGES_MAX=$(CAP))
CDOM_OPT := -O2
cdom_comma := ,
APP_LDFLAGS := $(if $(MAXMEM),-Wl$(cdom_comma)--max-memory=$(MAXMEM))
CDOM_IMAGE_SJLJ := $(SJLJ)
include $(CDOM)/cdom.mk
all: $(APP_PAGE)
