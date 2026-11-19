# The image module's wasm test app (tests/image/wasm.c), built as an app builds it, with cdom.mk:
#   make -f tests/image/wasm.mk OUT=tests/out/image SJLJ=js|eh [CAP=pages]
# writes OUT/wasm_<SJLJ>[_cap].html, the page with the module bundled.
CDOM := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
NAME := wasm_$(SJLJ)$(if $(CAP),_cap)
APP_WASM := $(OUT)/$(NAME).wasm
APP_PAGE := $(OUT)/$(NAME).html
APP_PAGE_SRC := $(CDOM)/tests/image/wasm.html
APP_SRC := $(CDOM)/tests/image/wasm.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror $(if $(CAP),-DCDOM_HEAP_PAGES_MAX=$(CAP))
CDOM_OPT := -O2
CDOM_IMAGE_SJLJ := $(SJLJ)
include $(CDOM)/cdom.mk
all: $(APP_PAGE)
