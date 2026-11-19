CDOM := $(abspath ../../..)
V ?= full
SJLJ ?= js
SIMD ?= none
OPT ?= Oz
ARM := $(V)-$(SJLJ)$(if $(filter-out none,$(SIMD)),-$(SIMD))$(if $(filter-out Oz,$(OPT)),-$(OPT))
APP_WASM := out-$(ARM)/app.wasm
APP_SRC := app.c
APP_MODULES := $(if $(filter none,$(V)),,image)
APP_CFLAGS := -Wall -Wextra -Werror $(if $(filter none,$(V)),-DNONE) $(if $(filter jpeg,$(V)),-DONLY_JPEG) $(if $(filter png,$(V)),-DONLY_PNG) -I$(CDOM)/src
APP_LDFLAGS := -Wl,--strip-all
CDOM_IMAGE_SJLJ := $(SJLJ)
CDOM_IMAGE_SIMD := $(SIMD)
CDOM_OPT := -$(OPT)
include $(CDOM)/cdom.mk
all: $(APP_WASM)
