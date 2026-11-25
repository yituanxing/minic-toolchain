#!/usr/bin/env bash
# Original Linux 6.6.143 GNU frozen .i owners, genuine GNU GAS/ELF and
# fail-closed CPU-alternative feature/instruction mutation tests.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
frozen=${2:?x86 Linux frozen input root}
test -x "$mini"
test -d "$frozen/i"
command -v as >/dev/null
command -v readelf >/dev/null
command -v objdump >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for owner in arch/x86/kernel/apic/apic_flat_64 \
             arch/x86/kernel/apic/ipi \
             arch/x86/kernel/cpu/amd; do
    input="$frozen/i/$owner.i"
    test -s "$input"
    tag=${owner//\//-}
    "$mini" --target=x86_64-linux -fshort-wchar -funsigned-char -fno-PIE \
        -S "$input" -o "$work/$tag.s"
    as --64 -o "$work/$tag.o" "$work/$tag.s" 2>"$work/$tag.gas.log"
    readelf -SW "$work/$tag.o" | grep -Fq '.altinstructions'
    readelf -SW "$work/$tag.o" | grep -Fq '.altinstr_replacement'
done
for owner in apic_flat_64 ipi; do
    tag="arch-x86-kernel-apic-$owner"
    objdump -d -j .altinstr_replacement "$work/$tag.o" |
        grep -Eq '\bxchg[[:space:]]+%r11d,\(%r10\)'
    grep -Fq 'movl %r11d, (%r10)' "$work/$tag.s"
done
objdump -d -j .altinstr_replacement \
    "$work/arch-x86-kernel-cpu-amd.o" | grep -Eq '\bdiv[[:space:]]+%r10'
grep -Fq '.4byte (22*32+(1*32+1))' "$work/arch-x86-kernel-cpu-amd.s"

sed 's/movl %0, %P1/movl %0, %P2/' \
    "$frozen/i/arch/x86/kernel/apic/apic_flat_64.i" >"$work/bad-apic.i"
if "$mini" --target=x86_64-linux -fshort-wchar -funsigned-char -fno-PIE \
    -S "$work/bad-apic.i" -o "$work/bad-apic.s" >"$work/bad-apic.log" 2>&1; then
    echo 'FAIL: altered APIC MMIO ALT register was accepted' >&2
    exit 1
fi
test ! -e "$work/bad-apic.s"

sed 's/div %2/idiv %2/' \
    "$frozen/i/arch/x86/kernel/cpu/amd.i" >"$work/bad-amd.i"
if "$mini" --target=x86_64-linux -fshort-wchar -funsigned-char -fno-PIE \
    -S "$work/bad-amd.i" -o "$work/bad-amd.s" >"$work/bad-amd.log" 2>&1; then
    echo 'FAIL: altered AMD alternative DIV instruction was accepted' >&2
    exit 1
fi
test ! -e "$work/bad-amd.s"
echo 'MINIC_X86_LINUX_ALT_APIC_AMD=PASS owners=3 GNU_GAS=3 mutated=2'
