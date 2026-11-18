# A fixture app of tests/link/run.sh (linked: see tests/link/run.sh, "the stylesheets"). It is built with
# make -C here CDOM=... OUT=..., so the build goes under tests/out and never into the tree; PAGE names
# its own page, or none for cdom's default page.
CDOM ?= $(abspath ../../../..)
OUT ?= build
APP_WASM := $(OUT)/app.wasm
APP_SRC := src/app.c
APP_PAGE := $(OUT)/index.html
APP_PAGE_SRC := $(PAGE)
APP_TITLE := linked
include $(CDOM)/cdom.mk

all: $(APP_PAGE)
.PHONY: all
