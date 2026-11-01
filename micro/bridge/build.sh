#!/bin/bash
# Builds every bridge arm (gen_arms.py) into micro/bridge/out/: the micro page, cdom's
# js-framework-benchmark entry (bench/cdom-keyed) and the foolish cdom port's table (read from
# PORT, never written), each with the arm's cdom.c and its shim minified and inlined as shipped,
# plus a counting build of each (<arm>_count: every import counted, untimed).
#   micro/bridge/build.sh [what]      what: all (default), micro, keyed, port
# The port's kernel objects are compiled once into out/port/obj and shared by every arm.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/../.."
out="$here/out"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
TERSER="${TERSER:-$top/node_modules/.bin/terser}"
[ -x "$TERSER" ] || TERSER=/Users/alex/Dev/cdom-fixes/node_modules/.bin/terser # the same terser 5.44.0, before npm install here
PORT="${PORT:-/Users/alex/Dev/foolish-cdom-port/foolish/c/cdom_poc}"
WHAT="${1:-all}"
ARMS=(a list b c d e f)
PORT_ARMS=(a list b c d e f e_t e_css) # e_t and e_css: arm a's cdom with the port's movers rewritten (port_patch.py)
KEYED_ARMS=(a list b c d e)
mkdir -p "$out"

gen() { # arm: generated sources in out/gen/<arm> and out/gen/<arm>_count
  local arm="$1"
  [ -f "$out/gen/$arm/cdom.c" ] && [ "$out/gen/$arm/cdom.c" -nt "$here/gen_arms.py" ] && return
  python3 "$here/gen_arms.py" "$arm" "$out/gen/$arm"
  python3 "$here/gen_arms.py" "$arm" "$out/gen/${arm}_count" --count
}
shim() { # gen dir, out file: the shim minified
  "$TERSER" "$1/cdom.js" -c passes=2 -m -o "$2"
}
inline() { # template, shim, out html
  sed -e "/\/\*CDOM_SHIM\*\//{r $2" -e "d;}" "$1" > "$3"
}
for arm in "${ARMS[@]}"; do gen "$arm"; done

if [ "$WHAT" = all ] || [ "$WHAT" = micro ]; then
  for arm in "${ARMS[@]}"; do
    for v in "$arm" "${arm}_count"; do
      d="$out/micro/$v"; mkdir -p "$d"
      "$CC" --target=wasm32 -Oz -nostdlib -mbulk-memory -mreference-types -Wall -Wextra -Werror -I"$out/gen/$arm" \
        -Wl,--no-entry -Wl,--strip-all -o "$d/bridge.wasm" "$here/bridge.c" "$out/gen/$arm/cdom.c" "$top/src/mini.c"
      shim "$out/gen/$v" "$d/cdom.min.js"
      inline "$here/bridge.html" "$d/cdom.min.js" "$d/index.html"
    done
  done
  echo "micro: $(ls "$out/micro" | wc -l | tr -d ' ') builds"
fi

if [ "$WHAT" = all ] || [ "$WHAT" = keyed ]; then
  mkdir -p "$out/css/bootstrap/dist/css"
  jcss="${JFB_CSS:-/Users/alex/Dev/cdom-fixes/jfb/css}"
  cp "$jcss/currentStyle.css" "$jcss/main.css" "$out/css/"
  cp "$jcss/bootstrap/dist/css/bootstrap.min.css" "$out/css/bootstrap/dist/css/"
  for arm in "${KEYED_ARMS[@]}"; do
    for v in "$arm" "${arm}_count"; do
      d="$out/keyed/$v"; mkdir -p "$d"
      "$CC" --target=wasm32 -Oz -nostdlib -mbulk-memory -mreference-types -Wall -Wextra -Werror \
        -Wl,--no-entry -Wl,--strip-all -o "$d/app.wasm" "$top/bench/cdom-keyed/src/app.c" "$out/gen/$arm/cdom.c" "$top/src/mini.c"
      shim "$out/gen/$v" "$d/cdom.min.js"
      inline "$top/bench/cdom-keyed/index.html" "$d/cdom.min.js" "$d/index.html"
    done
    printf 'keyed %s: app.wasm %s bytes\n' "$arm" "$(wc -c < "$out/keyed/$arm/app.wasm" | tr -d ' ')"
  done
fi

if [ "$WHAT" = all ] || [ "$WHAT" = port ]; then
  c="$PORT/.."
  shared="$c/../../shared/c"
  obj="$out/port/obj"; mkdir -p "$obj"
  # The port's build.sh flags, as they are there.
  KFLAGS=(--target=wasm32 -Oz -nostdlib -ffreestanding -mbulk-memory -mno-nontrapping-fptoint
    -ffp-contract=off
    -isystem "$c/wasm/include" -isystem "$shared/wasm/include" -I"$c/src"
    -D_Thread_local= -DMAX_LOG_PAIRS=64 -DMAX_LEGAL_MOVES=4096 -DMAX_MOVE_CARDS=28 -DMAX_BATTLES=64
    -DFOOLISH_SEEDED_BOTS_ONLY -Wall -Wextra -Wno-unused-parameter)
  KERNEL=(src/game.c src/legal.c src/view.c src/awire.c src/evwire.c src/anim_plan.c src/client_table.c
    src/card.c src/hand_layout.c src/roster.c src/bot_drive.c src/handwritten_prod_strategy.c)
  SHARED=(deal_rng.c wasm/libc.c wasm/libm.c)
  kobjs=()
  for f in "${KERNEL[@]}"; do
    o="$obj/$(basename "${f%.c}").o"
    [ "$o" -nt "$c/$f" ] || "$CC" "${KFLAGS[@]}" -c "$c/$f" -o "$o"
    kobjs+=("$o")
  done
  for f in "${SHARED[@]}"; do
    o="$obj/shared_$(basename "${f%.c}").o"
    [ "$o" -nt "$shared/$f" ] || "$CC" "${KFLAGS[@]}" -c "$shared/$f" -o "$o"
    kobjs+=("$o")
  done
  for arm in "${PORT_ARMS[@]}"; do
    cdom="$arm"; case "$arm" in e_t|e_css) cdom=a;; esac
    app="$PORT/src/table_app.c"
    if [ "$arm" = e ] || [ "$arm" = e_t ] || [ "$arm" = e_css ]; then
      app="$out/port/table_app_$arm.c"
      python3 "$here/port_patch.py" "$arm" "$PORT/src/table_app.c" "$app"
    fi
    AFLAGS=("${KFLAGS[@]}" -mreference-types -I"$out/gen/$cdom" -I"$PORT/src" -I"$PORT/vendor" -I"$c" -Werror)
    aobj="$out/port/obj_$arm"; mkdir -p "$aobj"
    objs=("${kobjs[@]}")
    for f in "$out/gen/$cdom/cdom.c" "$top/src/cdom_store.c" "$PORT/src/kernel_glue.c" "$PORT/src/game_keep.c" \
      "$PORT/src/anim_run.c" "$PORT/src/textures.c" "$PORT/src/crtrig.c" "$PORT/src/trig.c"; do
      o="$aobj/$(basename "${f%.c}").o"
      "$CC" "${AFLAGS[@]}" -c "$f" -o "$o"
      objs+=("$o")
    done
    "$CC" "${AFLAGS[@]}" -DTABLE_DEBUG -c "$app" -o "$aobj/table_app_debug.o"
    objs+=("$aobj/table_app_debug.o")
    for v in "$arm" "${arm}_count"; do
      d="$out/port/$v"; mkdir -p "$d"
      if [ "$v" = "$arm" ]; then
        "$CC" --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--export-memory -Wl,--stack-first -Wl,-z,stack-size=65536 \
          -Wl,--strip-all -o "$d/table_debug.wasm" "${objs[@]}"
        wasm-opt -Oz --enable-bulk-memory --enable-mutable-globals --enable-sign-ext --enable-reference-types \
          "$d/table_debug.wasm" -o "$d/table_debug.wasm"
      else
        cp "$out/port/$arm/table_debug.wasm" "$d/table_debug.wasm"
      fi
      gv="$cdom"; [ "$v" != "$arm" ] && gv="${cdom}_count"
      shim "$out/gen/$gv" "$d/cdom.min.js"
      inline "$PORT/src/index.html" "$d/cdom.min.js" "$d/index.html"
    done
    printf 'port %s: table_debug.wasm %s bytes\n' "$arm" "$(wc -c < "$out/port/$arm/table_debug.wasm" | tr -d ' ')"
  done
fi
