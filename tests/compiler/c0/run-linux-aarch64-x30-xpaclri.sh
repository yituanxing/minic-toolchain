#!/usr/bin/env bash
set -Eeuo pipefail

minic=${1:?expected MiniC-cc executable}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

source_file="$root/tests/compiler/c0/linux-aarch64-x30-xpaclri.c"
cc -E -P -x c "$source_file" -o "$work/original-macro.i"
"$minic" --target=aarch64-linux -S "$work/original-macro.i" \
    -o "$work/minic.s"

# Three XPACLRI expansion sites. Every tied operand must explicitly use the
# physical link register, and every function must save and restore entry LR.
test "$(grep -Ec '^[[:space:]]*hint #7$' "$work/minic.s")" -eq 4
test "$(grep -Ec '^[[:space:]]*ldr x30, \[x9\]$' "$work/minic.s")" -eq 4
test "$(grep -Ec '^[[:space:]]*str x30, \[x9\]$' "$work/minic.s")" -eq 4
test "$(grep -Fc 'stp x29, x30, [sp, #-16]!' "$work/minic.s")" -eq 3
test "$(grep -Fc 'ldp x29, x30, [sp], #16' "$work/minic.s")" -eq 3

for invalid in wrong-template wrong-register; do
    cc -E -P -x c \
       "$root/tests/compiler/c0/linux-aarch64-x30-xpaclri-$invalid.c" \
       -o "$work/$invalid.i"
    if "$minic" --target=aarch64-linux -S "$work/$invalid.i" \
        -o "$work/$invalid.s" >"$work/$invalid.stdout" \
        2>"$work/$invalid.stderr"; then
        echo "invalid x30 Linux ASM contract accepted: $invalid" >&2
        exit 1
    fi
    test ! -s "$work/$invalid.s"
done

# Local x30 register admission belongs exclusively to the AArch64 target.
for target in riscv32-linux riscv64-linux x86_64-linux loongarch64-linux; do
    if "$minic" --target="$target" -S "$work/original-macro.i" \
        -o "$work/$target.s" >"$work/$target.stdout" \
        2>"$work/$target.stderr"; then
        echo "non-AArch64 target accepted a hard x30 binding: $target" >&2
        exit 1
    fi
    grep -F 'local fixed register binding is not supported by this target' \
        "$work/$target.stderr" >/dev/null
    test ! -s "$work/$target.s"
done

# Optional real-GNU AArch64 assembly check. GNU GCC may use a different
# prologue, but both compilers must keep XPACLRI attached to physical x30.
gnu_cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
gnu_objdump=${AARCH64_OBJDUMP:-aarch64-linux-gnu-objdump}
if command -v "$gnu_cc" >/dev/null && \
   command -v "$gnu_objdump" >/dev/null; then
    "$gnu_cc" -O2 -S -x c "$source_file" -o "$work/gnu.s"
    grep -E 'hint[[:space:]]*#7' "$work/gnu.s" >/dev/null
    "$gnu_cc" -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
    "$gnu_cc" -c -x assembler "$work/minic.s" -o "$work/minic.o"
    "$gnu_objdump" -dr "$work/gnu.o" >"$work/gnu.dump"
    "$gnu_objdump" -dr "$work/minic.o" >"$work/minic.dump"
    test "$(grep -Ec 'd50320ff[[:space:]]+hint' "$work/gnu.dump")" -eq 4
    test "$(grep -Ec 'd50320ff[[:space:]]+hint' "$work/minic.dump")" -eq 4
    echo 'AARCH64_X30_XPACLRI=PASS minic_S=4 gnu_S=4 machine_encoding=d50320ff'
else
    echo 'AARCH64_X30_XPACLRI=PASS minic_S=4 gnu_cross_ISA=SKIP(toolchain-unavailable)'
fi
