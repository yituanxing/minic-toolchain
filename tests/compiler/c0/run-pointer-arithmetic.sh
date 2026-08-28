#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-pointer-arithmetic"

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/pointer_aggregate_arithmetic.c" \
    -o "$work/pointer_aggregate_arithmetic.i"
"$minic" -S \
    "$work/pointer_aggregate_arithmetic.i" \
    -o "$work/pointer_aggregate_arithmetic.s"

grep -E '^[[:space:]]+li[[:space:]]+[^,]+,[[:space:]]*12$' \
    "$work/pointer_aggregate_arithmetic.s" >/dev/null
test "$(grep -E -c '^[[:space:]]+mul[[:space:]]+[^,]+,[[:space:]]*[^,]+,[[:space:]]*[^,]+$' \
    "$work/pointer_aggregate_arithmetic.s")" -ge 2
printf '%s\n' "PASS compiler/c0/pointer_aggregate_arithmetic stride=12 multiply>=2"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="$build_dir" \
sh "$root/tests/compiler/c0/run-gnu-void-pointer-arithmetic.sh"
