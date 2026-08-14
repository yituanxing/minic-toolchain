#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-gnu-top-level-empty-declaration"

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/gnu_top_level_empty_declaration.c" \
    -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

grep -F "first:" "$work/output.s" >/dev/null
grep -F "second:" "$work/output.s" >/dev/null
grep -F "main:" "$work/output.s" >/dev/null

printf '%s\n' \
    "PASS compiler/c0/gnu_top_level_empty_declaration leading=1 consecutive=2 after-function=1 between-declarations=1 trailing=1"
