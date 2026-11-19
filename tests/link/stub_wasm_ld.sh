#!/bin/sh
# stub_clang.sh's linker, for its version only.
[ "$1" = --version ] && echo "LLD $STUB_LLD" && exit 0
exit 1
