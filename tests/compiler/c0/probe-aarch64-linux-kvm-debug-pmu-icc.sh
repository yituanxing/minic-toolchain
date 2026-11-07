#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_kvm_debug_pmu_writes.c" \
    -o "$work/debug.i"
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/aarch64_linux_kvm_icc_encoded_writes.c" \
    -o "$work/icc.i"
"$minic" --target=aarch64-linux -S "$work/debug.i" -o "$work/debug.s"
"$minic" --target=aarch64-linux -S "$work/icc.i" -o "$work/icc.s"
test "$(grep -c '^  msr ' "$work/debug.s")" -eq 134
test "$(grep -c '^  \.inst 0xd5' "$work/icc.s")" -eq 8

for reg in dbgbcr0_el1 dbgbcr15_el1 dbgwvr0_el1 dbgwvr15_el1 \
    pmevcntr0_el0 pmevcntr30_el0 pmevtyper0_el0 pmevtyper30_el0 \
    spsr_irq spsr_fiq sp_el1 dacr32_el2 ifsr32_el2 dbgvcr32_el2; do
    grep -Fq "msr $reg," "$work/debug.s"
done
for reg in '8 4' '8 7' '9 0' '9 3'; do
    set -- $reg
    crm=$1; op2=$2
    encoding=$(printf '0x%08x' "$((0xd5000000 | (3 << 19) | (12 << 12) | (crm << 8) | (op2 << 5) | 9))")
    grep -Fq ".inst $encoding" "$work/icc.s"
done
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/debug.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        echo "FAIL accepted AArch64 sysreg by $target" >&2
        exit 1
    fi
done
printf '%s\n' \
    'void unknown(unsigned long long v) { asm volatile("msr dbgbvr16_el1, %x0" :: "rZ"(v)); }' \
    > "$work/unknown.i"
if "$minic" --target=aarch64-linux -S "$work/unknown.i" \
    -o "$work/unknown.s" >/dev/null 2>&1; then
    echo 'FAIL accepted out-of-range debug sysreg' >&2
    exit 1
fi
printf '%s\n' \
    'void wrong(unsigned long long v) { asm volatile("msr dbgbcr15_el1, %x0" :: "m"(v)); }' \
    > "$work/wrong.i"
if "$minic" --target=aarch64-linux -S "$work/wrong.i" \
    -o "$work/wrong.s" >/dev/null 2>&1; then
    echo 'FAIL accepted invalid GNU constraint' >&2
    exit 1
fi
printf '%s\n' 'PASS AArch64 KVM debug 64 PMUv3 62 extra 8 ICC 8 forbidden 2 target gates 4'
