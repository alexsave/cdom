CDOM := $(abspath ../../..)
V ?= full
SJLJ ?= js
SIMD ?= 0
APP_WASM := out-$(V)-$(SJLJ)$(if $(filter 1,$(SIMD)),-simd)/app.wasm
APP_SRC := app.c
APP_MODULES := $(if $(filter none,$(V)),,image)
APP_CFLAGS := -Wall -Wextra -Werror $(if $(filter none,$(V)),-DNONE) $(if $(filter jpeg,$(V)),-DONLY_JPEG) $(if $(filter png,$(V)),-DONLY_PNG) -I$(CDOM)/src
APP_LDFLAGS := -Wl,--strip-all
CDOM_IMAGE_SJLJ := $(SJLJ)
CDOM_IMAGE_SIMD := $(SIMD)
include $(CDOM)/cdom.mk
all: $(APP_WASM)
