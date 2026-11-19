# The image bench's pages (bench/image/run.mjs): make -C bench/image builds build-js/index.html and build-eh/index.html,
# the C arm with each CDOM_IMAGE_SJLJ, at cdom.mk's -Oz, scalar. make inputs writes inputs/ (inputs.py, seeded).
CDOM := $(abspath ../..)
SJLJ ?= js
APP_WASM := build-$(SJLJ)/app.wasm
APP_PAGE := build-$(SJLJ)/index.html
APP_PAGE_SRC := page.html
APP_SRC := app.c
APP_MODULES := image
APP_CFLAGS := -Wall -Wextra -Werror
CDOM_IMAGE_SJLJ := $(SJLJ)
include $(CDOM)/cdom.mk
all:
	$(MAKE) SJLJ=js page
	$(MAKE) SJLJ=eh page
page: $(APP_PAGE)
inputs:
	python3 -I inputs.py inputs
.PHONY: all page inputs
