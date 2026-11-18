CDOM ?= ../../../..
APP_WASM := build/app.wasm
APP_SRC := $(wildcard src/*.c)
APP_PAGE := build/index.html
APP_TITLE := Shared state
APP_MODULES := ui
APP_CFLAGS := -Wall -Wextra -Werror -Isrc
include $(CDOM)/cdom.mk
all: $(APP_PAGE)
.PHONY: all
