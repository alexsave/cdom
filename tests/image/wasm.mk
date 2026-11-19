# The image module's wasm test app (tests/image/wasm.c), built as an app builds it, with cdom.mk:
#   make -f tests/image/wasm.mk OUT=tests/out/image SJLJ=js|eh [SIMD=none|simd128|simde] [CAP=pages] [MAXMEM=bytes]
# writes OUT/wasm_<SJLJ>[_cap][_max].html, the page with the module bundled. CAP caps cdom's heap
# (CDOM_HEAP_PAGES_MAX); MAXMEM caps the module's memory itself (wasm-ld --max-memory), so memory.grow refuses.
CDOM := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
SIMD ?= none
NAME := wasm_$(SJLJ)$(if $(CAP),_cap)$(if $(MAXMEM),_max)$(if $(filter-out none,$(SIMD)),_$(SIMD))
APP_WASM := $(OUT)/$(NAME).wasm
APP_PAGE := $(OUT)/$(NAME).html
APP_PAGE_SRC := $(CDOM)/tests/image/wasm.html
APP_SRC := $(CDOM)/tests/image/wasm.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror $(if $(CAP),-DCDOM_HEAP_PAGES_MAX=$(CAP)) $(if $(filter simde,$(SIMD)),-DCDOM_IMAGE_SIMDE)
CDOM_OPT := -O2
cdom_comma := ,
APP_LDFLAGS := $(if $(MAXMEM),-Wl$(cdom_comma)--max-memory=$(MAXMEM))
CDOM_IMAGE_SJLJ := $(SJLJ)
CDOM_IMAGE_SIMD := $(SIMD)
include $(CDOM)/cdom.mk
# the SIMD builds also take the test hooks (src/cdom_image_jsimd.c, src/cdom_image_jpeg.c): C on demand, ifast and merged
ifeq ($(SIMD),simde)
CDOM_IMAGE_OWN_CFLAGS += -DCDOM_IMAGE_SIMD_TEST
CDOM_IMAGE_VENDOR_CFLAGS += -DCDOM_IMAGE_SIMD_TEST
endif
all: $(APP_PAGE)
