# The image bench's pages (bench/image/run.mjs): make -C bench/image builds build-js/index.html and build-eh/index.html,
# the C arm with each CDOM_IMAGE_SJLJ, at cdom.mk's -Oz, scalar. make inputs writes inputs/ (inputs.py, seeded).
CDOM := $(abspath ../..)
SJLJ ?= js
# LEVEL (the optimization-level arm): oz (cdom.mk's -Oz, the default), o2, o3, or mix (third_party's parts at -O2,
# the rest at -Oz): pages build-js-o2 and so on.
LEVEL ?= oz
SUF := $(if $(filter oz,$(LEVEL)),,-$(LEVEL))
APP_WASM := build-$(SJLJ)$(SUF)/app.wasm
APP_PAGE := build-$(SJLJ)$(SUF)/index.html
APP_PAGE_SRC := page.html
APP_SRC := app.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror
CDOM_IMAGE_SJLJ := $(SJLJ)
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
inputs:
	python3 -I inputs.py inputs
.PHONY: all levels page inputs
