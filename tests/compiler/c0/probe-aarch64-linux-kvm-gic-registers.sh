#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_kvm_gic_registers.c" \
    -o "$work/input.i"
"$minic" --target=aarch64-linux -S "$work/input.i" -o "$work/gic.s"
test -s "$work/gic.s"
test "$(grep -c '^  \.inst 0xd5' "$work/gic.s")" -eq 24
for reg in ICH_AP0R0_EL2 ICH_AP1R3_EL2 ICH_LR0_EL2 ICH_LR7_EL2 \
    ICH_LR8_EL2 ICH_LR15_EL2; do
    grep -Fq "write_$reg:" "$work/gic.s"
done
for form in '8 0' '8 3' '9 0' '9 3' '12 0' '12 7' '13 0' '13 7'; do
    set -- $form
    crm=$1; op2=$2
    encoding=$(printf '0x%08x' "$((0xd5000000 | (3 << 19) | (4 << 16) | (12 << 12) | (crm << 8) | (op2 << 5) | 9))")
    grep -Fq ".inst $encoding" "$work/gic.s"
done
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/input.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        printf 'FAIL ARM64 GIC reg accepted by %s\n' "$target" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS compiler/c0/aarch64_linux_kvm_gic_registers GNU_macro_ops=24 encoding_spotchecks=8 target-gate=4'
