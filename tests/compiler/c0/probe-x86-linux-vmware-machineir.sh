#!/usr/bin/env bash
# Linux 6.6.143 arch/x86/kernel/cpu/vmware.c, compile-only privileged ISA.
# Verify that target-qualified Source forms become sealed MachineIR, while
# GAS receives real vmcall/vmmcall/I/O and both ALTERNATIVE_2 ELF fixups.
set -Eeuo pipefail
minic=$1
source_file=${BASH_SOURCE[0]%/*}/x86_linux_vmware_machineir.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -std=gnu11 -E -P "$source_file" -o "$work/fixture.i"
"$minic" --target=x86_64-linux -S "$work/fixture.i" -o "$work/fixture.s"
as --64 -o "$work/fixture.o" "$work/fixture.s"

readelf -SW "$work/fixture.o" | grep -E '\.altinstructions +PROGBITS.*00001c ' >/dev/null
readelf -SW "$work/fixture.o" | grep -E '\.altinstr_replacement +PROGBITS.*000006 ' >/dev/null
readelf -rW "$work/fixture.o" | grep -F "'.rela.altinstructions'" | grep -F 'contains 4 entries' >/dev/null
objdump -dr "$work/fixture.o" | grep -E '\bvmcall$' >/dev/null
objdump -dr "$work/fixture.o" | grep -E '\bvmmcall$' >/dev/null
objdump -dr "$work/fixture.o" | grep -E '\bin[[:space:]]+\(%dx\),%eax$' >/dev/null

# Fail closed for an unapproved privileged instruction, replacement feature
# bit, and mutated GNU fixed output register binding.
sed 's/__asm__("vmcall"/__asm__("syscall"/' "$work/fixture.i" >"$work/bad-opcode.i"
sed 's/8\*32+18/8*32+17/' "$work/fixture.i" >"$work/bad-feature.i"
sed 's/"=c"(info)/"=d"(info)/' "$work/fixture.i" >"$work/bad-binding.i"
for bad in bad-opcode bad-feature bad-binding; do
    if "$minic" --target=x86_64-linux -S "$work/$bad.i" \
        -o "$work/$bad.s" >/dev/null 2>"$work/$bad.err"; then
        echo "FAIL: VMware unsafe $bad was accepted" >&2
        exit 1
    fi
done

echo 'MINIC_X86_VMWARE_MACHINEIR_GNU_AS=PASS variants=3 alt=2 feature=18,19 reloc=4 reject=3 runtime=NOT_RUN'
