#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-multidimensional-array-relocation
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_multidimensional_array_relocation.c" \
    -o "$work/valid.i"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_static_multidimensional_array_relocation.c" \
    -o "$work/invalid.i"

for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$minic_cc" "--target=$target-linux" -S "$work/valid.i" \
        -o "$work/valid.$target.s"
    grep -F 'pll_regs+40' "$work/valid.$target.s" >/dev/null
    grep -F 'nested_regs+80' "$work/valid.$target.s" >/dev/null
    if "$minic_cc" "--target=$target-linux" -S "$work/invalid.i" \
        -o "$work/invalid.$target.s" >"$work/invalid.$target.out" \
        2>"$work/invalid.$target.err"; then
        printf 'FAIL static multidimensional relocation: %s accepted wrong pointee\n' "$target" >&2
        exit 1
    fi
    grep -F 'static pointer initializer type mismatch' "$work/invalid.$target.err" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/static-multidimensional-array-relocation 5ISA row-decay=2D+3D byte-addend=40+80 mismatched-pointee=reject'
