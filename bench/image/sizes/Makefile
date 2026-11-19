CDOM := $(abspath ../../..)
V ?= full
SJLJ ?= js
LEVEL ?= oz
APP_WASM := out-$(V)-$(SJLJ)-$(LEVEL)/app.wasm
APP_SRC := app.c
APP_MODULES := $(if $(filter none,$(V)),,image)
APP_CFLAGS := -Wall -Wextra -Werror $(if $(filter none,$(V)),-DNONE) $(if $(filter jpeg,$(V)),-DONLY_JPEG) $(if $(filter png,$(V)),-DONLY_PNG) -I$(CDOM)/src
APP_LDFLAGS := -Wl,--strip-all
CDOM_IMAGE_SJLJ := $(SJLJ)
CDOM_OPT := $(if $(filter o2,$(LEVEL)),-O2,$(if $(filter o3,$(LEVEL)),-O3,-Oz))
include $(CDOM)/cdom.mk
ifeq ($(LEVEL),mix)
CDOM_IMAGE_VENDOR_CFLAGS += -O2
endif
all: $(APP_WASM)
