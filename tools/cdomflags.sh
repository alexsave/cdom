# Sourced (bash) by cdom's own build scripts: CDOM_CFLAGS and CDOM_LDFLAGS as bash arrays, read from
# cdom.mk, the one place the set an app builds with is written, so cdom's tests, demo, micro and benchmark
# builds use exactly what apps use; and cdom_flags_gate, which fails when one of those scripts spells a
# flag of that set itself (a copy that would drift from cdom.mk when the set changes).
cdom_top="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cdom_mk_var() { # the value of one of cdom.mk's variables, for this checkout
  printf 'cdom-print-var:\n\t@echo $(%s)\n' "$1" |
    make -s --no-print-directory -f "$cdom_top/cdom.mk" -f - cdom-print-var CDOM="$cdom_top"
}
read -r -a CDOM_CFLAGS <<< "$(cdom_mk_var CDOM_CFLAGS)"
read -r -a CDOM_LDFLAGS <<< "$(cdom_mk_var CDOM_LDFLAGS)"
if [ "${#CDOM_CFLAGS[@]}" -lt 3 ] || [ "${#CDOM_LDFLAGS[@]}" -lt 1 ]; then
  echo "FAIL cdom.mk gave no CDOM_CFLAGS or CDOM_LDFLAGS" >&2; exit 1
fi
# The scripts that build cdom modules. Each must take the set from here, never write a flag of it.
CDOM_FLAG_USERS=(tests/build.sh tests/link/run.sh bench/build.sh micro/svg/run.sh)
cdom_flags_gate() {
  local bad=0 f w n
  for f in "${CDOM_FLAG_USERS[@]}"; do
    for w in "${CDOM_CFLAGS[@]}" "${CDOM_LDFLAGS[@]}"; do
      case "$w" in -I*|/*) continue;; esac # cdom's paths, which a script names for its own reasons
      n="$(grep -n -F -e "$w" "$cdom_top/$f" | head -1 | cut -d: -f1 || true)"
      if [ -n "$n" ]; then
        echo "FAIL $f:$n hardcodes $w, a flag of cdom.mk's set: take CDOM_CFLAGS and CDOM_LDFLAGS from tools/cdomflags.sh" >&2
        bad=1
      fi
    done
  done
  return $bad
}
