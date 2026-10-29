#!/bin/sh
# Source effect summary: white-box C0 AST contract, GNU native differential,
# and the same accepted C source across all five registered target backends.
set -eu
: "${MINIC:?Set MINIC to the absolute path of minic-cc}"
: "${CC:=cc}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/core-local-effect-summary"}
mkdir -p "$work"

"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$root/include" -I"$root/src" \
    "$root/tests/core/core_local_effect_summary_test.c" \
    "$root/src/core/core_lower_local_effects.c" \
    -o "$work/effects-unit"
"$work/effects-unit"

source_file="$root/tests/compiler/c0/core_local_effect_summary.c"
"$CC" -std=gnu11 -O0 "$source_file" -o "$work/gnu"
"$CC" -std=gnu11 -E -P "$source_file" -o "$work/test.i"
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$MINIC" --target="$target" -S "$work/test.i" -o "$work/$target.s"
done
"$CC" -no-pie "$work/x86_64-linux.s" -o "$work/minic"
"$work/gnu"
"$work/minic"
printf 'PASS Core source effects: AST unit, GNU native, five targets -S\n'
