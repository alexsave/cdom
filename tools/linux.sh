#!/bin/bash
# cdom on Linux, in containers (Docker). cdom needs wasm-ld from LLVM 23 or later, the first that defines
# __rodata_start and __rodata_end (src/cdom.c, cd_tmpz). Ubuntu 24.04 ships clang and lld 18, and
# apt.llvm.org's lld-22 (22.1.8) does not define them either: with each, cdom.mk's toolchain check stops
# tutorial lesson 1 with the linker's version and the fix. With clang-23 and lld-23 from apt.llvm.org, the
# fix it names, and cdom.mk's defaults otherwise (HOST_CC cc = gcc 13), lesson 1 builds its page. Both host
# tools also build with gcc and clang under Alpine 3.20 (gcc 13, clang 17), the oldest compilers that take
# -std=c2x and refuse -std=c23.
# Usage: tools/linux.sh   (needs docker and the network; reads the tree, writes nothing in it)
# Where apt.llvm.org cannot be reached, CDOM_LINUX_LLVM_22 and CDOM_LINUX_LLVM_23 name an LLVM release tree
# for Linux x86-64 (LLVM-<version>-Linux-X64 from github.com/llvm/llvm-project/releases, unpacked), which
# the container sees as /usr/lib/llvm-<major> in place of the apt.llvm.org packages.
set -u
top="$(cd "$(dirname "$0")/.." && pwd)"
bad=0
log="$(mktemp)"
# ubuntu <llvm major, or distro> <expect: stops or builds>: lesson 1 on Ubuntu 24.04 with that clang and lld.
ubuntu() {
    local v="$1" want="$2" cc=clang ld=wasm-ld setup mount=() root
    setup='apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq make gcc clang lld curl ca-certificates >/dev/null 2>&1
cp -r /src /w && find /w/tools/out /w/tutorial/01-hello/build -delete 2>/dev/null'
    if [ "$v" != distro ]; then
        cc="/usr/lib/llvm-$v/bin/clang" ld="/usr/lib/llvm-$v/bin/wasm-ld"
        root="CDOM_LINUX_LLVM_$v"; root="${!root:-}"
        if [ -n "$root" ]; then mount=(-v "$(cd "$root" && pwd -P):/usr/lib/llvm-$v:ro")
        else setup="$setup
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key > /etc/apt/trusted.gpg.d/apt.llvm.org.asc
echo 'deb http://apt.llvm.org/noble/ llvm-toolchain-noble-$v main' > /etc/apt/sources.list.d/llvm.list
apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq clang-$v lld-$v >/dev/null 2>&1"
        fi
    fi
    local found="$setup
lld=\"\$($ld --version)\"
make -s -C /w/tutorial/01-hello WASM_CC=$cc > /tmp/log 2>&1; rc=\$?"
    if [ "$want" = builds ]; then found="$found
[ \$rc = 0 ] && [ -s /w/tutorial/01-hello/build/index.html ] || { cat /tmp/log; exit 1; }
echo \"\$lld\""
    else found="$found
[ \$rc != 0 ] && grep -qF \"cdom.mk: $cc links with \$lld, which does not define __rodata_start\" /tmp/log \\
  && grep -qF 'cdom.mk: install LLVM 23 or later:' /tmp/log && ! grep -q 'error:' /tmp/log \\
  && [ ! -e /w/tutorial/01-hello/build/app.wasm ] || { echo \"\$lld\"; cat /tmp/log; exit 1; }
echo \"\$lld\""
    fi
    local out
    if out=$(docker run --rm -v "$top:/src:ro" ${mount[@]+"${mount[@]}"} ubuntu:24.04 bash -c "$found" 2> "$log"); then
        echo "ok   ubuntu 24.04, clang $v${root:+ ($root)} ($out): lesson 1 $want"
    else
        echo "FAIL ubuntu 24.04, clang $v${root:+ ($root)}: wanted lesson 1 $want:"; echo "$out" | head -5; head -5 "$log"; bad=1
    fi
}
ubuntu distro "stops at the toolchain check"
ubuntu 22 "stops at the toolchain check"
ubuntu 23 builds
rm -f "$log"
alpine='apk add -q gcc clang musl-dev >/dev/null 2>&1; r=0
for cc in gcc clang; do for t in cdlits cdpage; do
  $cc -std=c2x -O2 -Wall -Wextra -Werror -o /tmp/$t /src/tools/$t.c 2>/tmp/err || { echo "$cc $t: $(head -1 /tmp/err)"; r=1; }
done; done; exit $r'
if out=$(docker run --rm -v "$top:/src:ro" alpine:3.20 sh -c "$alpine" 2>&1); then
    echo "ok   alpine 3.20: cdlits and cdpage build with gcc 13 and clang 17"
else
    echo "FAIL alpine 3.20: $out"; bad=1
fi
exit $bad
