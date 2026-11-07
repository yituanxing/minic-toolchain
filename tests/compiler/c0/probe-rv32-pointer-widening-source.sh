#!/usr/bin/env bash
# MC-048 lightweight acceptance: one GNU-preprocessed C source, five targets.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { echo "MC048_POINTER_WIDEN_SOURCE=FAIL $*" >&2; exit 1; }
test -x "$minic" || fail 'MiniC binary missing'
src="$root/tests/compiler/audit/mc048_rv32_pointer_widening.c"

"$cc" -std=gnu11 -Werror -fsyntax-only "$src" || fail 'GNU C syntax'
"$cc" -std=gnu11 -E -P "$src" -o "$work/pointer.i" ||
    fail 'GNU C preprocessing'
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    args=()
    if test "$target" = riscv32-linux; then args=(-march=rv32i -mabi=ilp32); fi
    "$minic" --target="$target" "${args[@]}" -S "$work/pointer.i" \
        -o "$work/$target.s" || fail "MiniC -S target=$target"
done
echo 'MC048_POINTER_WIDEN_SOURCE=PASS GNU_C_syntax_and_five_target_S'
