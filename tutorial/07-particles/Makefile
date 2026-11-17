CDOM ?= $(abspath ../..)
APP_WASM := build/app.wasm
APP_SRC := src/app.c
APP_PAGE := build/index.html
APP_TITLE := Particles
APP_CFLAGS := -Wall -Wextra -Werror
include $(CDOM)/cdom.mk

all: $(APP_PAGE)
.PHONY: all
