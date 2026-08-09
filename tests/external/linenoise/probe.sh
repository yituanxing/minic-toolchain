#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/linenoise-discovery"}
vendor="$work/upstream"
minic=${MINIC:-"$root/build/ci-release/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
upstream=a473823d74b93eab2ba83480df16ed37617493f2

rm -rf "$work"
mkdir -p "$vendor"

curl -fsSL "https://raw.githubusercontent.com/antirez/linenoise/$upstream/linenoise.c" -o "$vendor/linenoise.c"
curl -fsSL "https://raw.githubusercontent.com/antirez/linenoise/$upstream/linenoise.h" -o "$vendor/linenoise.h"

test "$(git hash-object "$vendor/linenoise.c")" = 63f23ddaf0e06dea4d2ac04efa084c3ca275ad8c
test "$(git hash-object "$vendor/linenoise.h")" = 735629b78ed2302d407fb3b6c8e56c6ac24bd6b7

# Establish a real compiler reference first. Keep the upstream source unchanged.
"$host_cc" -std=gnu11 -O2 -I"$vendor" -c "$vendor/linenoise.c" -o "$work/linenoise-gcc.o"

# Discovery starts with the normal libc/POSIX preprocessing environment. If the first
# blocker is header-only noise rather than linenoise semantics, that evidence decides
# whether a controlled ABI declaration layer is warranted; do not pre-edit upstream.
"$host_cc" -E -P -std=gnu11 -I"$vendor" "$vendor/linenoise.c" -o "$work/linenoise.i"

set +e
"$minic" -S "$work/linenoise.i" -o "$work/linenoise.s" \
    >"$work/minic.stdout" 2>"$work/minic.stderr"
status=$?
set -e

if test "$status" -ne 0; then
    frontier_line=$(sed -n 's/.*linenoise\.i:\([0-9][0-9]*\):.*/\1/p' "$work/minic.stderr" | head -n 1)
    if test -z "$frontier_line"; then
        frontier_line=1
    fi
    start_line=$((frontier_line > 24 ? frontier_line - 24 : 1))
    end_line=$((frontier_line + 24))
    printf '%s\n' "LINENOISE_BLOCKER minic_status=$status line=$frontier_line" >&2
    printf '%s\n' "linenoise preprocessed frontier lines=$start_line-$end_line:" >&2
    nl -ba "$work/linenoise.i" | sed -n "${start_line},${end_line}p" >&2
    printf '%s\n' 'MiniC diagnostic:' >&2
    sed -n '1,160p' "$work/minic.stderr" >&2
    exit "$status"
fi

printf '%s\n' \
    "PASS external/linenoise frontier=full-translation-unit upstream=$upstream gcc_reference=object"
