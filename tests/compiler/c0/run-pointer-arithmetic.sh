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

# RV64I does not admit MUL.  Record stride 12 is selected as 4*x+8*x
# (SLLI 2 / SLLI 3 / ADD) at both source pointer computations.
test "$(grep -Ec '^[[:space:]]+slli[[:space:]]+[^,]+,[[:space:]]*[^,]+,[[:space:]]*2$' \
    "$work/pointer_aggregate_arithmetic.s")" -ge 2
test "$(grep -Ec '^[[:space:]]+slli[[:space:]]+[^,]+,[[:space:]]*[^,]+,[[:space:]]*3$' \
    "$work/pointer_aggregate_arithmetic.s")" -ge 2
test "$(grep -Ec '^[[:space:]]+add[[:space:]]+' "$work/pointer_aggregate_arithmetic.s")" -ge 2
printf '%s\n' "PASS compiler/c0/pointer_aggregate_arithmetic stride=12 shift-add>=2"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="$build_dir" \
sh "$root/tests/compiler/c0/run-gnu-void-pointer-arithmetic.sh"
