# An app's Makefile as README.md shows one, for run.sh: the module and the page from cdom.mk's rules,
# CDOM found from the include's own path. OUT is where they go, PAGE the page to inline the shim into.
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
APP_PAGE := $(OUT)/$(notdir $(PAGE))
APP_PAGE_SRC := $(PAGE)
ifneq ($(filter last twice,$(INCLUDE)),)
include $(cdom_mk_here)../../cdom.mk
endif
# The app's own first rule stays its default goal, though cdom.mk's rules came before it.
app-goal:
	@echo app-goal
