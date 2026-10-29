#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/release"}/tests/compiler-c0-symbolic-inline-two-callers
source="$root/tests/compiler/c0/symbolic_inline_two_callers.c"
mkdir -p "$work"

"$host_cc" -std=gnu11 -Wall -Wextra -Werror "$source" -o "$work/gcc"
"$work/gcc"
"$host_cc" -std=gnu11 -E -P "$source" -o "$work/source.i"

for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/source.i" \
        -o "$work/$target.s" >"$work/$target.out" 2>"$work/$target.err"
    test -s "$work/$target.s"
done

"$host_cc" -no-pie "$work/x86_64-linux.s" -o "$work/minic"
"$work/minic"
printf '%s\n' \
    'SYMBOLIC_INLINE_TWO_CALLERS=PASS gcc_runtime=1 minic_x86_runtime=1 targets=5 distinct_callers=2 dynamic_route=2'
