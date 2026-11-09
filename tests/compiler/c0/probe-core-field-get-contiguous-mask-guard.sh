#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/core-field-get-contiguous-mask-guard
mkdir -p "$work"
source="$root/tests/compiler/c0/core_field_get_contiguous_mask_guard.c"
cc -E -P -std=gnu11 -x c "$source" -o "$work/positive.i"
cc -DMINIC_BAD_MASK_NEGATIVE -E -P -std=gnu11 -x c "$source" -o "$work/negative.i"
cc -O2 -Werror -S -x c "$work/positive.i" -o "$work/gnu.s"
for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$MINIC_CC" --target="$target" -S "$work/positive.i" -o "$work/$target.s"
    if grep -q 'impossible_noncontiguous_bitfield_mask' "$work/$target.s"; then
        echo "CORE_FIELD_GET_MASK=FAIL $target emitted impossible valid guard" >&2
        exit 1
    fi
    "$MINIC_CC" --target="$target" -S "$work/negative.i" -o "$work/$target-negative.s"
    if ! grep -q 'impossible_noncontiguous_bitfield_mask' "$work/$target-negative.s"; then
        echo "CORE_FIELD_GET_MASK=FAIL $target erased genuine invalid guard" >&2
        exit 1
    fi
done
cc -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
cc -c -x assembler "$work/x86_64-linux.s" -o "$work/minic.o"
cc -no-pie -Wl,-z,noexecstack "$work/gnu.o" -o "$work/gnu"
cc -no-pie -Wl,-z,noexecstack "$work/minic.o" -o "$work/minic"
"$work/gnu"
"$work/minic"
printf 'CORE_FIELD_GET_MASK=PASS targets=5 GNU_O2=1 native_runtime=2 negative=noncontiguous\n'
