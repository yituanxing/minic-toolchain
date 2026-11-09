#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-integer-pointer-cast-comparison

mkdir -p "$work"
"$host_cc" -std=gnu11 -Wall -Wextra -fsyntax-only \
    "$root/tests/compiler/c0/static_integer_pointer_cast_comparison.c"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/static_integer_pointer_cast_comparison.c" -o "$work/good.i"

MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -S \
    "$work/good.i" -o "$work/x86.s"
"$host_cc" -no-pie "$work/x86.s" -o "$work/x86"
"$work/x86"
"$host_cc" -std=gnu11 "$root/tests/compiler/c0/static_integer_pointer_cast_comparison.c" \
    -o "$work/gnu"
"$work/gnu"

for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/good.i" -o "$work/$target.s"
done

if "$host_cc" -std=gnu11 -fsyntax-only \
    "$root/tests/compiler/c0/invalid_static_pointer_compare_nonconstant.c" \
    >"$work/negative.gnu.stdout" 2>"$work/negative.gnu.stderr"; then
    echo 'FAIL GCC accepted dynamic pointer comparison static initializer' >&2
    exit 1
fi
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_static_pointer_compare_nonconstant.c" \
    -o "$work/negative.i"
if "$minic_cc" --target=x86_64-linux -S \
    "$work/negative.i" -o "$work/negative.s" \
    >"$work/negative.stdout" 2>"$work/negative.stderr"; then
    echo 'FAIL MiniC accepted dynamic pointer comparison static initializer' >&2
    exit 1
fi
grep -F 'integer initializer requires an arithmetic constant' \
    "$work/negative.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-integer-pointer-cast-comparison GNU-differential=1 x86-native=1 five-targets=1 dynamic-rejected=1 strict-core=1'
