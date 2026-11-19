# An app's Makefile as README.md shows one, for run.sh: the module and the page from cdom.mk's rules,
# CDOM found from the include's own path. OUT is where they go, PAGE the page to inline the shim into
# (none: cdom.mk's default page, written to OUT/index.html). APP_WASM_SEPARATE, APP_TITLE and
# APP_WASM_URL come from the command line, when a case sets them.
# INCLUDE says where the one include is: first (before APP_* are set) or last (after them); twice is the
# old two-include form (once for the variables, again after APP_*), which still builds.
cdom_mk_here := $(dir $(lastword $(MAKEFILE_LIST)))
ifneq ($(filter first twice,$(INCLUDE)),)
include $(cdom_mk_here)../../cdom.mk
endif
APP_WASM := $(OUT)/mk_app.wasm
APP_SRC := $(cdom_mk_here)mk_app.c $(EXTRA_SRC)
APP_MODULES ?= random
APP_CFLAGS := -Wall -Wextra -Werror
APP_PAGE := $(OUT)/$(if $(PAGE),$(notdir $(PAGE)),index.html)
APP_PAGE_SRC := $(PAGE)
ifneq ($(filter last twice,$(INCLUDE)),)
include $(cdom_mk_here)../../cdom.mk
endif
# The app's own first rule stays its default goal, though cdom.mk's rules came before it.
app-goal:
	@echo app-goal
# A test module with several files and a part of its own (cdom.mk, CDOM_<NAME>_PARTS), named when PARTS is set:
# after every include, since cdom.mk sets CDOM_MODULES. EXTRA_CFLAGS goes after the app's flags.
ifdef PARTS
CDOM_MODULES += parts
CDOM_PARTS_SRC := $(cdom_mk_here)parts/own_a.c $(cdom_mk_here)parts/own_b.c
CDOM_PARTS_PARTS := lib
CDOM_PARTS_LIB_SRC := $(cdom_mk_here)parts/lib/lib_a.c $(cdom_mk_here)parts/lib/lib_b.c
CDOM_PARTS_LIB_CFLAGS := -w -I$(cdom_mk_here)parts/lib/private -DPARTS_LIB=3
CDOM_PARTS_DEPS := $(cdom_mk_here)parts/lib/private/parts_private.h
CDOM_PARTS_LDFLAGS := -Wl,--export=parts_lib_b
APP_CFLAGS += $(EXTRA_CFLAGS)
endif
