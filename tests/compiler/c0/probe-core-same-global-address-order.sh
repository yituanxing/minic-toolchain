#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/core-same-global-address-order
mkdir -p "$work"
source="$root/tests/compiler/c0/core_same_global_address_order.c"
cc -E -P -std=gnu11 -x c "$source" -o "$work/source.i"
cc -O2 -S -x c "$work/source.i" -o "$work/gnu.s"
for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$MINIC_CC" --target="$target" -S "$work/source.i" -o "$work/$target.s"
    if grep -Eq 'impossible_(early|late)_sentinel' "$work/$target.s"; then
        echo "CORE_GLOBAL_ADDRESS_ORDER=FAIL $target retained same-object impossible branch" >&2
        exit 1
    fi
done
if ! sed -n '/^unrelated_global_order:$/,/^\.size unrelated_global_order,/p' \
        "$work/x86_64-linux.s" | grep -Fq 'cmpq '; then
    echo 'CORE_GLOBAL_ADDRESS_ORDER=FAIL compared unrelated globals were constant-folded' >&2
    exit 1
fi
cc -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
cc -c -x assembler "$work/x86_64-linux.s" -o "$work/minic.o"
cc -no-pie -Wl,-z,noexecstack "$work/gnu.o" -o "$work/gnu"
cc -no-pie -Wl,-z,noexecstack "$work/minic.o" -o "$work/minic"
"$work/gnu"
"$work/minic"
printf 'CORE_GLOBAL_ADDRESS_ORDER=PASS five_targets=5 GNU_AS=2 GNU_LD=2 native_runtime=2 unrelated_objects_not_folded=1\n'
