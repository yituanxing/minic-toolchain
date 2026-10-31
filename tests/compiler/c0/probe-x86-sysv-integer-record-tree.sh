#!/bin/sh
# One signed-off x86 target ABI classifier for one- and two-GPR nested records.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/x86_sysv_integer_record_tree.c"
driver="$root/tests/compiler/c0/x86_sysv_integer_record_tree_driver.c"
${CC:-cc} -E -P -x c "$source" -o "$work/source.i"
${CC:-cc} -O0 -c "$source" -o "$work/gnu.o"
${CC:-cc} -O0 -c "$driver" -o "$work/driver.o"
"$MINIC_CC" --target=x86_64-linux -S "$work/source.i" -o "$work/minic.s"
${CC:-cc} -c -x assembler "$work/minic.s" -o "$work/minic.o"
${CC:-cc} "$work/driver.o" "$work/gnu.o" -o "$work/gnu"
${CC:-cc} "$work/driver.o" "$work/minic.o" -o "$work/minic"
"$work/gnu"
"$work/minic"
for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$MINIC_CC" --target="$target-linux" -S "$work/source.i" \
        -o "$work/$target.s"
    for name in x86_one_roundtrip x86_two_roundtrip x86_array_roundtrip x86_bit_roundtrip; do
        grep -Eq "^${name}:$" "$work/$target.s" || {
            echo "X86_SYSV_INTEGER_TREE=FAIL target=$target missing=$name" >&2
            exit 1
        }
    done
done
echo 'X86_SYSV_INTEGER_TREE=PASS GNU-caller=4 nested-register-records five-target-S=5'
