#!/bin/sh
# Native x86-64 exploration of the existing, pinned official Lua 5.5.0.
# GNU host libc/system headers stay real. No patched Lua or fake declarations.
set -eu
: "${MINIC_CC:?set MINIC_CC to the built minic-cc binary}"
: "${HOST_CC:=cc}"
case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo "SKIP Lua x86-64: native x86 host required"; exit 0 ;;
esac
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
release=5.5.0
sha256=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d
curl -fsSL "https://www.lua.org/ftp/lua-$release.tar.gz" -o "$work/lua.tar.gz"
printf '%s  %s\n' "$sha256" "$work/lua.tar.gz" | sha256sum -c -
tar -xzf "$work/lua.tar.gz" -C "$work"
vendor="$work/lua-$release"
if test -d "$vendor/src"; then src="$vendor/src"; else src="$vendor"; fi
make -C "$vendor" -j4 all >"$work/reference-gcc.log" 2>&1 || {
  echo "FAIL Lua x86-64: pristine GCC reference build failed" >&2
  tail -n 35 "$work/reference-gcc.log" >&2
  exit 1
}
if test -x "$src/lua"; then lua="$src/lua"; else lua="$vendor/lua"; fi
"$lua" -e 'assert(21 + 21 == 42)' || exit 1
echo "PASS Lua x86-64 GCC reference release=$release smoke=42"
sources='lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c loslib.c lstrlib.c ltablib.c lutf8lib.c linit.c lua.c'
# Optional source-wide survey: real, unmodified Lua TUs, with every
# preprocess/compiler/assembler failure reported independently. Preserve the
# original stop-on-first-error mode by default to keep diagnosis fast.
# Opt in with LUA_X86_64_SCAN_ALL=1; exit status still fails on any blocker.
scan_all=${LUA_X86_64_SCAN_ALL:-0}
case "$scan_all" in
  0|1) ;;
  *) echo "FAIL Lua x86-64: LUA_X86_64_SCAN_ALL must be 0 or 1" >&2; exit 2 ;;
esac
passed=0
assembled=0
blocked=0
total=0
for source in $sources; do
  total=$((total + 1))
  stem=${source%.c}
  if ! "$HOST_CC" -E -P -std=c99 -DLUA_USE_LINUX -I"$src" \
        "$src/$source" -o "$work/$stem.i" >"$work/$stem.cpp.stdout" 2>"$work/$stem.cpp.stderr"; then
    blocked=$((blocked + 1))
    echo "LUA_X86_64_BLOCKER source=$source stage=preprocess compiled=$passed" >&2
    sed -n '1,24p' "$work/$stem.cpp.stderr" >&2
    if test "$scan_all" -eq 0; then exit 1; fi
    continue
  fi
  if "$MINIC_CC" --target=x86_64-linux -S "$work/$stem.i" \
        -o "$work/$stem.s" >"$work/$stem.stdout" 2>"$work/$stem.stderr"; then
    passed=$((passed + 1))
    echo "PASS Lua x86-64 TU $source count=$passed"
    # In survey mode, verify that successful Core output also assembles as
    # a real x86-64 object; source-only success must not hide assembler faults.
    if test "$scan_all" -eq 1; then
      if "$HOST_CC" -c -x assembler "$work/$stem.s" -o "$work/$stem.o" \
            >"$work/$stem.as.stdout" 2>"$work/$stem.as.stderr"; then
        assembled=$((assembled + 1))
        echo "PASS Lua x86-64 OBJ $source count=$assembled"
      else
        blocked=$((blocked + 1))
        echo "LUA_X86_64_BLOCKER source=$source stage=assembler compiled=$passed assembled=$assembled" >&2
        sed -n '1,24p' "$work/$stem.as.stderr" >&2
      fi
    fi
    continue
  fi
  blocked=$((blocked + 1))
  line=$(sed -n "s/.*$stem\\.i:\\([0-9][0-9]*\\):.*/\\1/p" "$work/$stem.stderr" | head -n 1)
  if test -z "$line"; then line=1; fi
  start=$((line > 10 ? line - 10 : 1))
  end=$((line + 10))
  echo "LUA_X86_64_BLOCKER source=$source stage=compiler compiled=$passed line=$line" >&2
  nl -ba "$work/$stem.i" | sed -n "${start},${end}p" >&2
  sed -n '1,24p' "$work/$stem.stderr" >&2
  if test "$scan_all" -eq 0; then exit 1; fi
done
echo "LUA_X86_64_FRONTIER compiler=$passed assembled=$assembled total=$total blocked=$blocked scan_all=$scan_all"
if test "$blocked" -ne 0; then
  exit 1
fi
echo "PASS Lua x86-64 Core all-TU compilation count=$passed"
