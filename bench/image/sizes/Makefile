CDOM := $(abspath ../../..)
V ?= full
SJLJ ?= js
SIMD ?= 0
LEVEL ?= oz
APP_WASM := out-$(V)-$(SJLJ)-$(LEVEL)$(if $(filter 1,$(SIMD)),-simd)/app.wasm
APP_SRC := app.c
APP_MODULES := $(if $(filter none,$(V)),,image) $(if $(filter fileread fileglue,$(V)),file)
APP_CFLAGS := -Wall -Wextra -Werror $(if $(filter none,$(V)),-DNONE) $(if $(filter jpeg,$(V)),-DONLY_JPEG) $(if $(filter png,$(V)),-DONLY_PNG) \
  $(if $(filter fileread,$(V)),-DFILE_READ) $(if $(filter fileglue,$(V)),-DFILE_GLUE) -I$(CDOM)/src
APP_LDFLAGS := -Wl,--strip-all
CDOM_IMAGE_SJLJ := $(SJLJ)
CDOM_OPT := $(if $(filter o2,$(LEVEL)),-O2,$(if $(filter o3,$(LEVEL)),-O3,-Oz))
CDOM_IMAGE_SIMD := $(SIMD)
include $(CDOM)/cdom.mk
ifeq ($(LEVEL),mix)
CDOM_IMAGE_VENDOR_CFLAGS += -O2
endif
all: $(APP_WASM)
