# The image bench's pages (bench/image/run.mjs): make -C bench/image builds build-js/index.html and build-eh/index.html,
# the C arm with each CDOM_IMAGE_SJLJ, at cdom.mk's -Oz, scalar. make simd builds build-simd/index.html, the js way with
# CDOM_IMAGE_SIMD=1 (the hand-written wasm SIMD loops, docs/image-simd-hand.md). make inputs writes inputs/ (inputs.py, seeded).
CDOM := $(abspath ../..)
SJLJ ?= js
# LEVEL (the optimization-level arm): oz (cdom.mk's -Oz, the default), o2, o3, or mix (third_party's parts at -O2,
# the rest at -Oz): pages build-js-o2 and so on.
LEVEL ?= oz
SUF := $(if $(filter oz,$(LEVEL)),,-$(LEVEL))
SIMD ?= 0
DIR := $(if $(filter 1,$(SIMD)),build-simd,build-$(SJLJ)$(SUF))
APP_WASM := $(DIR)/app.wasm
APP_PAGE := $(DIR)/index.html
APP_PAGE_SRC := page.html
APP_SRC := app.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror
CDOM_IMAGE_SJLJ := $(SJLJ)
CDOM_OPT := $(if $(filter o2,$(LEVEL)),-O2,$(if $(filter o3,$(LEVEL)),-O3,-Oz))
CDOM_IMAGE_SIMD := $(SIMD)
include $(CDOM)/cdom.mk
ifeq ($(LEVEL),mix)
CDOM_IMAGE_VENDOR_CFLAGS += -O2
endif
all:
	$(MAKE) SJLJ=js page
	$(MAKE) SJLJ=eh page
levels:
	for l in o2 o3 mix; do $(MAKE) SJLJ=js LEVEL=$$l page || exit 1; done
page: $(APP_PAGE)
simd:
	$(MAKE) SIMD=1 page
inputs:
	python3 -I inputs.py inputs
.PHONY: all levels page simd inputs
