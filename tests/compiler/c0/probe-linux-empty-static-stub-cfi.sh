#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/linux-empty-static-stub-cfi
mkdir -p "$work"
source="$root/tests/compiler/c0/linux-empty-static-stub-cfi.c"
cc -E -P -std=gnu11 -x c "$source" -o "$work/source.i"
cc -O2 -S -x c "$work/source.i" -o "$work/gnu.s"
"$MINIC_CC" --target=x86_64-linux -S "$work/source.i" -o "$work/minic.s"
for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$MINIC_CC" --target="$target" -S "$work/source.i" -o "$work/$target.s"
    if grep -Eq 'minic_missing_cfi_(start|end)' "$work/$target.s"; then
        echo "LINUX_EMPTY_STATIC_STUB_CFI=FAIL $target retained unused CFI symbol" >&2
        exit 1
    fi
done
cc -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
cc -c -x assembler "$work/minic.s" -o "$work/minic.o"
cc -no-pie -Wl,-z,noexecstack "$work/gnu.o" -o "$work/gnu"
cc -no-pie -Wl,-z,noexecstack "$work/minic.o" -o "$work/minic"
"$work/gnu"
"$work/minic"
printf 'LINUX_EMPTY_STATIC_STUB_CFI=PASS five_targets=5 GNU_AS=2 GNU_LD=2 runtime=2 volatile_side_effect=1\n'
