# cdom's own targets (an app's Makefile includes cdom.mk, not this file).
# new: a new app in DIR, which builds one page that shows "cdom" centered (tools/new.sh):
#   make -C path/to/cdom new DIR=path/to/app
new:
	@sh tools/new.sh '$(subst ','\'',$(DIR))' '$(subst ','\'',$(PWD))'
.PHONY: new
