#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-top-level-comma-condition

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/top_level_comma_condition.c" \
    -o "$work/top_level_comma_condition.i"
"$minic" -S "$work/top_level_comma_condition.i" \
    -o "$work/top_level_comma_condition.s"

grep -F 'comma_condition_loop:' "$work/top_level_comma_condition.s" >/dev/null
grep -E '^[[:space:]]+b(eq|ne|lt|ge)' "$work/top_level_comma_condition.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/top_level_comma_condition while=1 void-left=1 result=right-scalar side-effect=sequenced'
