# The image bench's pages (bench/image/run.mjs): make -C bench/image builds build-<arm>/index.html for each arm of the
# C module: the SJLJ way (js, eh), CDOM_IMAGE_SIMD (none, simd128, simde: docs/image-simd.md) and LEVEL.
# LEVEL (the optimization-level arm): oz (cdom.mk's -Oz, the default), o2, o3, or mix (third_party's parts at -O2,
# the rest at -Oz). Pages: build-js (scalar -Oz, as it was), build-js-o2, build-js-simde, build-js-simde-o2, and so on.
# make inputs writes inputs/ (inputs.py, seeded).
CDOM := $(abspath ../..)
SJLJ ?= js
SIMD ?= none
LEVEL ?= oz
SUF := $(if $(filter-out none,$(SIMD)),-$(SIMD))$(if $(filter oz,$(LEVEL)),,-$(LEVEL))
APP_WASM := build-$(SJLJ)$(SUF)/app.wasm
APP_PAGE := build-$(SJLJ)$(SUF)/index.html
APP_PAGE_SRC := page.html
APP_SRC := app.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror $(if $(filter simde,$(SIMD)),-DCDOM_IMAGE_SIMDE)
CDOM_IMAGE_SJLJ := $(SJLJ)
CDOM_IMAGE_SIMD := $(SIMD)
CDOM_OPT := $(if $(filter o2,$(LEVEL)),-O2,$(if $(filter o3,$(LEVEL)),-O3,-Oz))
include $(CDOM)/cdom.mk
ifeq ($(LEVEL),mix)
CDOM_IMAGE_VENDOR_CFLAGS += -O2
CDOM_IMAGE_MINIZ_CFLAGS += -O2
endif
all:
	$(MAKE) SJLJ=js page
	$(MAKE) SJLJ=eh page
levels:
	for l in o2 o3 mix; do $(MAKE) SJLJ=js LEVEL=$$l page || exit 1; done
page: $(APP_PAGE)
simd:
	for s in none simd128 simde; do for l in oz o2; do $(MAKE) SIMD=$$s LEVEL=$$l page || exit 1; done; done
inputs:
	python3 -I inputs.py inputs
.PHONY: all levels page simd inputs
