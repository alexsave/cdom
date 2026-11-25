#!/bin/bash
# build.sh: cross-compiles agent.c to a 32-bit Windows XP exe (a.exe next to this script) with Homebrew LLVM
# (clang + llvm-dlltool) and lld-link.  No Windows SDK or mingw: agent.c declares what it uses, and the import
# libraries are made here from the .def lists below.  Needs: brew install llvm lld
set -e
cd "$(dirname "$0")"
L=/opt/homebrew/opt/llvm/bin
export PATH=/opt/homebrew/bin:$PATH
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
mkdef() {  # mkdef <dll> <name@bytes>...
    dll=$1; shift
    { echo "LIBRARY $dll"; echo EXPORTS; for f in "$@"; do echo "  $f"; done; } > "$T/$dll.def"
    "$L/llvm-dlltool" -k -m i386 -d "$T/$dll.def" -l "$T/$dll.lib"
}
mkdef kernel32.dll CreateProcessA@40 CloseHandle@4 Sleep@4 ExitProcess@4 GetLastError@0 GetCurrentProcessId@0 \
  CreateToolhelp32Snapshot@8 Process32First@8 Process32Next@8 OpenProcess@12 TerminateProcess@8
mkdef ws2_32.dll WSAStartup@8 socket@12 connect@12 send@16 recv@16 setsockopt@20 closesocket@4
"$L/clang" --target=i686-pc-windows-msvc -march=pentium -mno-sse -Os -ffreestanding -fno-builtin \
  -fno-stack-protector -Wall -Wextra -c agent.c -o "$T/agent.obj"
lld-link /nodefaultlib /subsystem:windows,5.01 /entry:start /filealign:512 /out:a.exe "$T/agent.obj" \
  "$T/kernel32.dll.lib" "$T/ws2_32.dll.lib"
ls -l a.exe
