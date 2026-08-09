#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/lua-discovery"}
archive="$work/lua-5.5.0.tar.gz"
vendor="$work/lua-5.5.0"
minic=${MINIC:-"$root/build/ci-release/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
target_cc=${TARGET_CC:-riscv64-linux-gnu-gcc}
release=5.5.0
sha256=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d

rm -rf "$work"
mkdir -p "$work"

curl -fsSL "https://www.lua.org/ftp/lua-$release.tar.gz" -o "$archive"
printf '%s  %s\n' "$sha256" "$archive" | sha256sum -c -
tar -xzf "$archive" -C "$work"

if test -d "$vendor/src"; then
    src="$vendor/src"
else
    src="$vendor"
fi

# Establish the upstream reference first. This is the official released Lua source
# package, built unchanged through Lua's documented host build path.
make -C "$vendor" -j4 all >"$work/gcc-build.log" 2>&1
if test ! -x "$src/lua" && test ! -x "$vendor/lua"; then
    printf '%s\n' 'FAIL external/lua: GCC reference did not produce lua executable' >&2
    tail -n 100 "$work/gcc-build.log" >&2
    exit 1
fi

# The MiniC frontier is also derived from the unchanged official Lua source, but it is
# preprocessed against the real RV64 Linux glibc development headers. Do not replace
# libc/POSIX headers with synthetic declarations: system-header language constructs are
# part of the real compiler frontier for the target environment.
if ! command -v "$target_cc" >/dev/null 2>&1; then
    printf '%s\n' "FAIL external/lua: target preprocessor not found: $target_cc" >&2
    exit 1
fi

sources='lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c lcorolib.c ldblib.c liolib.c lmathlib.c loadlib.c oslib.c lstrlib.c ltablib.c lutf8lib.c linit.c lua.c'

# Keep the exact upstream file name here. The explicit assignment avoids hiding an
# accidental spelling change in the long source list above.
sources=$(printf '%s\n' "$sources" | sed 's/ oslib\.c / loslib.c /')

passed=0
for source in $sources; do
    base=${source%.c}
    "$target_cc" -E -P -std=c99 -DLUA_USE_LINUX \
        -I"$src" \
        "$src/$source" -o "$work/$base.i"

    set +e
    "$minic" -S "$work/$base.i" -o "$work/$base.s" \
        >"$work/$base.stdout" 2>"$work/$base.stderr"
    status=$?
    set -e

    if test "$status" -ne 0; then
        frontier_line=$(sed -n "s/.*$base\\.i:\\([0-9][0-9]*\\):.*/\\1/p" "$work/$base.stderr" | head -n 1)
        if test -z "$frontier_line"; then
            frontier_line=1
        fi
        start_line=$((frontier_line > 18 ? frontier_line - 18 : 1))
        end_line=$((frontier_line + 18))
        printf '%s\n' "LUA_BLOCKER source=$source passed=$passed minic_status=$status line=$frontier_line" >&2
        printf '%s\n' "$source preprocessed frontier lines=$start_line-$end_line:" >&2
        nl -ba "$work/$base.i" | sed -n "${start_line},${end_line}p" >&2
        printf '%s\n' 'MiniC diagnostic:' >&2
        sed -n '1,120p' "$work/$base.stderr" >&2
        exit "$status"
    fi
    passed=$((passed + 1))
    printf '%s\n' "PASS external/lua-tu source=$source completed=$passed"
done

printf '%s\n' "PASS external/lua release=$release translation_units=$passed gcc_reference=full-build target_headers=rv64-glibc"
