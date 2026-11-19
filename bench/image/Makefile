# The image bench's pages (bench/image/run.mjs): make -C bench/image builds build-<arm>/index.html for each arm of the
# C module: the SJLJ way (js, eh) and CDOM_IMAGE_SIMD (none, simd128, simde) at cdom.mk's -Oz, or at -O2 (OPT=O2).
# The scalar js page stays build-js, as it was. make inputs writes inputs/ (inputs.py, seeded).
CDOM := $(abspath ../..)
SJLJ ?= js
SIMD ?= none
OPT ?= Oz
ARM := $(SJLJ)$(if $(filter-out none,$(SIMD)),-$(SIMD))$(if $(filter-out Oz,$(OPT)),-$(OPT))
APP_WASM := build-$(ARM)/app.wasm
APP_PAGE := build-$(ARM)/index.html
APP_PAGE_SRC := page.html
APP_SRC := app.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror $(if $(filter simde,$(SIMD)),-DCDOM_IMAGE_SIMDE)
CDOM_OPT := -$(OPT)
CDOM_IMAGE_SJLJ := $(SJLJ)
CDOM_IMAGE_SIMD := $(SIMD)
include $(CDOM)/cdom.mk
all:
	$(MAKE) SJLJ=js page
	$(MAKE) SJLJ=eh page
page: $(APP_PAGE)
simd:
	for s in none simd128 simde; do for o in Oz O2; do $(MAKE) SIMD=$$s OPT=$$o page || exit 1; done; done
inputs:
	python3 -I inputs.py inputs
.PHONY: all page simd inputs
