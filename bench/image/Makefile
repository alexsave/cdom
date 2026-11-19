# The image bench's pages (bench/image/run.mjs): make -C bench/image builds build-js/index.html and build-eh/index.html,
# the C arm with each CDOM_IMAGE_SJLJ, at cdom.mk's -Oz, scalar. make simd builds build-simd/index.html, the js way with
# CDOM_IMAGE_SIMD=1 (the hand-written wasm SIMD loops, docs/image-simd-hand.md). make inputs writes inputs/ (inputs.py, seeded).
CDOM := $(abspath ../..)
SJLJ ?= js
SIMD ?= 0
DIR := build-$(if $(filter 1,$(SIMD)),simd,$(SJLJ))
APP_WASM := $(DIR)/app.wasm
APP_PAGE := $(DIR)/index.html
APP_PAGE_SRC := page.html
APP_SRC := app.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror
CDOM_IMAGE_SJLJ := $(SJLJ)
CDOM_IMAGE_SIMD := $(SIMD)
include $(CDOM)/cdom.mk
all:
	$(MAKE) SJLJ=js page
	$(MAKE) SJLJ=eh page
page: $(APP_PAGE)
simd:
	$(MAKE) SIMD=1 page
inputs:
	python3 -I inputs.py inputs
.PHONY: all page simd inputs
