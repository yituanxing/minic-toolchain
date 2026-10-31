#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_hyp_debug_qcom_sysregs.c" \
    -o "$work/debug.i"
"$minic" --target=aarch64-linux -S "$work/debug.i" -o "$work/debug.s"
test "$(grep -c '^  \.inst 0xd5' "$work/debug.s")" -eq 13
for opcode in '0xd51bf0c9' '0xd51bf0e9'; do
    grep -Fq ".inst $opcode" "$work/debug.s"
done
for reg in id_aa64dfr0_el1 mdccint_el1 tpidrro_el0 sp_el1 vmpidr_el2 \
    CPACR_EL1 SCTLR_EL1; do
    grep -Fq "mrs x9, $reg" "$work/debug.s"
done
for number in 17 18; do
    grep -Fq "hint #$number" "$work/debug.s"
done
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/debug.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch hyp debug/QCOM sysreg on $target" >&2
        exit 1
    fi
done
sed 's/"6"/"5"/' "$work/debug.i" > "$work/wrong-vendor.i"
sed 's/"rZ"(value)/"m"(value)/' "$work/debug.i" > "$work/wrong-constraint.i"
sed 's/"=r"(value)/"=m"(value)/' "$work/debug.i" > "$work/wrong-mrs.i"
sed 's/hint #18/hint #19/' "$work/debug.i" > "$work/wrong-hint.i"
for category in vendor constraint mrs hint; do
    if "$minic" --target=aarch64-linux -S "$work/wrong-$category.i" \
        -o "$work/wrong-$category.s" >/dev/null 2>&1; then
        echo "FAIL accepted unsupported AArch hardware $category" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS GNU AArch KVM/SME 7 MRS, QCOM/VHE/SME 13 encoded MSR, PSB/TSB 2, 5ISA +4 negative'
