#!/bin/sh
# make -C path/to/cdom new DIR=path/to/app (cdom's Makefile): a new app in DIR, whose make builds
# build/index.html, one file that shows "cdom" centered and opens from disk. It writes three files and
# nothing cdom.mk could own: a Makefile (CDOM, the four APP_* lines, the include, a default goal),
# src/app.c and a .gitignore. CDOM defaults (?=) to this checkout's absolute path, and make CDOM=... or
# the environment's CDOM overrides it (another clone, CI, a moved folder). No git init and no submodule,
# so it works offline and at a commit not pushed anywhere; it prints how to vendor cdom as a submodule.
# Usage: sh tools/new.sh DIR [the directory DIR is relative to]
set -eu
dir="${1:-}"
from="${2:-}"
if [ -z "$dir" ]; then
  echo "usage: make -C path/to/cdom new DIR=path/to/app" >&2
  exit 2
fi
case "$dir" in
  "~") dir="$HOME" ;;
  "~/"*) dir="$HOME/${dir#"~/"}" ;;
esac
case "$dir" in
  /*) ;;
  *)
    if [ -z "$from" ]; then
      echo "cdom new: DIR=$dir is relative, and make did not say where it was run from; give an absolute DIR" >&2
      exit 2
    fi
    dir="$from/$dir" ;;
esac
if [ -e "$dir" ] && [ -n "$(ls -A "$dir")" ]; then
  echo "cdom new: $dir is not empty; give a new or empty directory" >&2
  exit 1
fi
cdom="$(cd "$(dirname "$0")/.." && pwd -P)"
mkdir -p "$dir/src"
dir="$(cd "$dir" && pwd -P)"

cat > "$dir/Makefile" <<EOF
CDOM ?= $cdom
APP_WASM := build/app.wasm
APP_SRC := src/app.c
APP_PAGE := build/index.html
include \$(CDOM)/cdom.mk

all: \$(APP_PAGE)
.PHONY: all
EOF

cat > "$dir/src/app.c" <<'EOF'
#include "cdom.h"

/* The app: "cdom", centered in the page. Edit this file and run make again. */
void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  cd_open(CD_LIT("style"));
  cd_text(CD_LIT(":root { color-scheme: light dark; font-family: system-ui, sans-serif; }"
                 ".page { position: fixed; inset: 0; display: flex; align-items: center; justify-content: center; }"
                 ".page h1 { margin: 0; font-size: 64px; }"
                 ".hint { position: fixed; left: 0; right: 0; bottom: 24px; margin: 0; text-align: center; opacity: 0.6; }"));
  cd_close();
  cd_open(CD_LIT("div")); cd_class(CD_LIT("page"));
  cd_open(CD_LIT("h1")); cd_text(CD_LIT("cdom")); cd_close();
  cd_close();
  cd_open(CD_LIT("p")); cd_class(CD_LIT("hint")); cd_text(CD_LIT("edit src/app.c")); cd_close();
}
EOF

printf 'build/\n' > "$dir/.gitignore"

echo "cdom new: wrote $dir/Makefile, $dir/src/app.c and $dir/.gitignore"
echo "cdom new: now run make -C $dir, and open $dir/build/index.html"
echo "cdom new: to vendor cdom with the app: cd $dir && git init && git submodule add https://github.com/alexsave/cdom cdom, then CDOM ?= cdom in the Makefile"
