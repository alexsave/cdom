CDOM ?= ../../../..
APP_WASM := build/app.wasm
APP_SRC := $(wildcard src/*.c) ../../lib/cd_state.c
APP_PAGE := build/index.html
APP_TITLE := Shared state
APP_MODULES := ui
APP_CFLAGS := -Wall -Wextra -Werror -Isrc -I../../lib
include $(CDOM)/cdom.mk
all: $(APP_PAGE)
.PHONY: all
