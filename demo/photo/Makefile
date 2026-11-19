# The image module's example (README.md, "image"): pick a photo, and C resizes it to fit 900 x 900 (cd_image_resize),
# encodes it as a JPEG (cd_encode_image) and shows it. make -C demo/photo builds build/index.html.
CDOM ?= $(abspath ../..)
APP_WASM := build/app.wasm
APP_SRC := src/app.c
APP_PAGE := build/index.html
APP_TITLE := Photo
APP_MODULES := file image
APP_CFLAGS := -Wall -Wextra -Werror
include $(CDOM)/cdom.mk

all: $(APP_PAGE)
.PHONY: all
