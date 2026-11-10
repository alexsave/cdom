CDOM ?= ../cdom
LITGEN ?= ../litgen
APP_SOURCES := src/app.c
APP_HEADERS := $(CDOM)/src/cdom.h
GEN := build/gen
APP_WASM := build/app.wasm
APP_SRC := $(addprefix $(GEN)/,$(notdir $(APP_SOURCES))) $(GEN)/litgen_table.c
APP_CFLAGS := $(addprefix -iquote ,$(sort $(dir $(APP_SOURCES))))
APP_PAGE := build/index.html
include $(CDOM)/cdom.mk

all: $(APP_PAGE)
.PHONY: all
# one run sees every source: the ids are global to the app
$(GEN)/.stamp: $(APP_SOURCES) $(APP_HEADERS) $(LITGEN)
	@mkdir -p $(GEN)
	$(LITGEN) -o $(GEN) $(addprefix -h ,$(APP_HEADERS)) $(APP_SOURCES)
	@touch $@
$(APP_SRC): $(GEN)/.stamp ;
