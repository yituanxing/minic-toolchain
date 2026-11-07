#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"${HOST_CC:-cc}" -std=gnu11 -E -P -x c \
    "$root/tests/compiler/c0/aarch64_linux_kvm_el2_pmu_sysreg.c" \
    -o "$work/kvm-sysreg.i"
"$minic" --target=aarch64-linux -S "$work/kvm-sysreg.i" -o "$work/accepted.s"
test -s "$work/accepted.s"
for reg in par_el1 elr_el2 spsr_el2 spsr_abt spsr_und vmpidr_el2 \
    cnthctl_el2 cntvoff_el2 pmuserenr_el0 pmcntenset_el0 \
    pmcntenclr_el0 pmovsclr_el0 pmevtyper0_el0 pmevcntr0_el0 \
    pmselr_el0 pmintenset_el1 pmintenclr_el1 pmccntr_el0 \
    pmccfiltr_el0 pmcr_el0 dbgbcr15_el1 dbgbvr0_el1 CPACR_EL1; do
    grep -Fq "msr $reg," "$work/accepted.s"
done
for target in x86_64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/kvm-sysreg.i" \
        -o "$work/$target.s" >/dev/null 2>&1; then
        printf '%s\n' "FAIL compiler/c0/kvm-sysreg unexpected target acceptance: $target" >&2
        exit 1
    fi
done
printf '%s\n' \
    'void rejected_reg(unsigned long long v){asm volatile("msr invented_el2, %x0"::"rZ"(v));}' \
    > "$work/unknown.i"
if "$minic" --target=aarch64-linux -S "$work/unknown.i" \
    -o "$work/unknown.s" >/dev/null 2>&1; then
    printf '%s\n' 'FAIL compiler/c0/kvm-sysreg unknown sysreg accepted' >&2
    exit 1
fi
printf '%s\n' \
    'void wrong_constraint(unsigned long long v){asm volatile("msr cntvoff_el2, %x0"::"m"(v));}' \
    > "$work/wrong-constraint.i"
if "$minic" --target=aarch64-linux -S "$work/wrong-constraint.i" \
    -o "$work/wrong-constraint.s" >/dev/null 2>&1; then
    printf '%s\n' 'FAIL compiler/c0/kvm-sysreg invalid constraint accepted' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/aarch64_kvm_el2_pmu_sysreg hardware_writes=23 target-gate=4 negative=2'
