#!/bin/sh
# Exact Linux C sources -> MiniC -S -> real GNU AS + ELF relocations.
# No privileged instruction from these fixtures is ever executed.
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
temp=$(mktemp -d)
trap 'rm -rf -- "$temp"' EXIT HUP INT TERM

for name in x86_64_linux_next_owner x86_64_linux_kernel_nofault_goto \
    x86_64_linux_next_owner_smap x86_64_linux_usercopy_lfence; do
    "$host_cc" -E -P "$root/tests/target/$name.c" -o "$temp/$name.i"
    "$minic_cc" --target=x86_64-linux -fshort-wchar -funsigned-char \
        -fno-PIE -mno-sse -S "$temp/$name.i" -o "$temp/$name.s"
    as --64 -o "$temp/$name.o" "$temp/$name.s"
done

readelf -SW "$temp/x86_64_linux_next_owner.o" > "$temp/sections"
readelf -rW "$temp/x86_64_linux_next_owner.o" > "$temp/relocs"
for section in '.discard.unreachable' '.altinstructions' \
    '.altinstr_replacement'; do
    if ! grep -Fq "$section" "$temp/sections"; then
        printf 'FAIL: original Linux %s section missing\n' "$section" >&2
        exit 1
    fi
done
if ! grep -Fq '.rela.altinstructions' "$temp/sections" ||
    ! grep -Fq '.discard.unreachable' "$temp/relocs"; then
    printf 'FAIL: Linux feature/unreachable relocations missing\n' >&2
    exit 1
fi
readelf -SW "$temp/x86_64_linux_kernel_nofault_goto.o" > "$temp/nofault.sections"
if ! grep -Fq '.rela__ex_table' "$temp/nofault.sections"; then
    printf 'FAIL: GNU nofault asm-goto relocations missing\n' >&2
    exit 1
fi

# Every mutation changes one source-authenticated semantic fact; accepting
# any of them would silently weaken the existing Target/MachineIR contract.
must_reject() {
    name=$1
    from=$2
    to=$3
    source=$4
    sed "s/$from/$to/g" "$temp/$source.i" > "$temp/invalid.i"
    if "$minic_cc" --target=x86_64-linux -fshort-wchar -funsigned-char \
        -fno-PIE -mno-sse -S "$temp/invalid.i" -o "$temp/invalid.s" \
        >/dev/null 2>&1; then
        printf 'FAIL: forged %s source admitted\n' "$name" >&2
        exit 1
    fi
}
must_reject 'far-jump width' 'ljmpl' 'ljmpq' x86_64_linux_next_owner
must_reject 'unreachable i witness' '"i" (375)' '"i" (376)' \
    x86_64_linux_next_owner
must_reject 'WRMSR fence opcode' 'mfence; lfence' 'mfence; sfence' \
    x86_64_linux_next_owner
must_reject 'nofault recovery type' '.long 3' '.long 4' \
    x86_64_linux_kernel_nofault_goto
must_reject 'SMAP opcode' '0x01,0xca' '0x01,0xcc' \
    x86_64_linux_next_owner_smap
must_reject 'usercopy alternative opcode' 'lfence' 'sfence' \
    x86_64_linux_usercopy_lfence

printf 'PASS x86 original owner: far-jump, unreachable, WRMSR, nofault goto, SMAP, LFENCE; 6 forged-source negatives\n'
