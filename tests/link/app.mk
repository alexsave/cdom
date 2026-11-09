# An app's Makefile as README.md shows one, for run.sh: the module and the page from cdom.mk's rules,
# CDOM found from the include's own path. OUT is where they go, PAGE the page to inline the shim into.
APP_WASM := $(OUT)/mk_app.wasm
APP_SRC := $(dir $(lastword $(MAKEFILE_LIST)))mk_app.c
APP_CFLAGS := -Wall -Wextra -Werror
APP_PAGE := $(OUT)/$(notdir $(PAGE))
APP_PAGE_SRC := $(PAGE)
include $(dir $(lastword $(MAKEFILE_LIST)))../../cdom.mk
# The app's own first rule stays its default goal, though cdom.mk's rules came before it.
app-goal:
	@echo app-goal
