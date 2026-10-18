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

# The native survey is now able to assemble all 33 pristine Lua TUs.
# The next real-project gate is one actual Lua interpreter produced from
# exactly those MiniC-generated objects, linked against unmodified GNU
# CRT/libc/math/dlopen. The pristine Lua build does not request readline. Do not imply a native MiniLD x86 port.
if test "$scan_all" -eq 1; then
  set --
  for source in $sources; do
    stem=${source%.c}
    set -- "$@" "$work/$stem.o"
  done
  if ! "$HOST_CC" -no-pie "$@" -lm -ldl -o "$work/lua-minic" \
        >"$work/lua-link.stdout" 2>"$work/lua-link.stderr"; then
    echo "LUA_X86_64_LINK=FAIL stage=gnu-link objects=$assembled" >&2
    sed -n '1,75p' "$work/lua-link.stderr" >&2
    exit 1
  fi
  echo "LUA_X86_64_LINK=PASS objects=$assembled driver=GNU"
  # Deterministic original-Lua runtime differential: actual interpreter
  # execution, closures, tables, strings, coroutine, numeric library.
  script='local t={3,1,2}; table.sort(t); assert(table.concat(t,",")=="1,2,3"); local function f(n) return n*7 end; assert(f(6)==42); local co=coroutine.create(function() coroutine.yield(21); return 42 end); local ok,x=coroutine.resume(co); assert(ok and x==21); ok,x=coroutine.resume(co); assert(ok and x==42); assert(math.floor(42.9)==42); print("LUA_NATIVE",string.reverse("CiniM"),string.format("%d",f(6)))'
  if ! "$lua" -e "$script" >"$work/gcc-run.stdout" 2>"$work/gcc-run.stderr"; then
    echo "LUA_X86_64_RUNTIME=FAIL stage=GCC-oracle" >&2
    sed -n '1,60p' "$work/gcc-run.stderr" >&2
    exit 1
  fi
  if ! "$work/lua-minic" -e "$script" >"$work/minic-run.stdout" 2>"$work/minic-run.stderr"; then
    echo "LUA_X86_64_RUNTIME=FAIL stage=MiniC" >&2
    sed -n '1,75p' "$work/minic-run.stderr" >&2
    exit 1
  fi
  if ! cmp -s "$work/gcc-run.stdout" "$work/minic-run.stdout" ||
     ! cmp -s "$work/gcc-run.stderr" "$work/minic-run.stderr"; then
    echo "LUA_X86_64_RUNTIME=FAIL stage=GCC-differential" >&2
    diff -u "$work/gcc-run.stdout" "$work/minic-run.stdout" >&2 || true
    diff -u "$work/gcc-run.stderr" "$work/minic-run.stderr" >&2 || true
    exit 1
  fi
  echo "LUA_X86_64_RUNTIME=PASS oracle=GCC byte-exact sample=table,closure,coroutine,math,string"
fi
