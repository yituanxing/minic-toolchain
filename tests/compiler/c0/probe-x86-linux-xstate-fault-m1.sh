#!/usr/bin/env bash
# Only assemble privileged XSAVE-family forms; never execute them in userspace.
# Optional second argument: SHA-pinned original Linux fpu/xstate GNU .i.
set -Eeuo pipefail

mini=${1:?usage: probe-x86-linux-xstate-fault-m1.sh <minic-cc> [original-xstate.i]}
original=${2:-}
test -x "$mini"
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT

gcc -E -P -x c "$root/tests/target/x86_64_linux_xstate_fault.c" \
    -o "$scratch/xstate.i"
"$mini" --target=x86_64-linux -S "$scratch/xstate.i" \
    -o "$scratch/minic.s"
as --64 "$scratch/minic.s" -o "$scratch/minic.o"
gcc -c -x c "$root/tests/target/x86_64_linux_xstate_fault.c" \
    -o "$scratch/gnu.o"

for compiler in minic gnu; do
    readelf -SW "$scratch/$compiler.o" > "$scratch/$compiler.sections"
    readelf -r "$scratch/$compiler.o" > "$scratch/$compiler.relocs"
    objdump -d "$scratch/$compiler.o" > "$scratch/$compiler.dump"
    # Four authenticated type-15 sites, each 12-byte aligned record and two
    # R_X86_64_PC32 relocations to the fault and fixup instruction labels.
    grep -Eq '^  \[[[:space:]]*[[:digit:]]+\] __ex_table[[:space:]]+PROGBITS.*000030' \
        "$scratch/$compiler.sections"
    [ "$(sed -n "/Relocation section '\.rela__ex_table'/,/^$/p" \
        "$scratch/$compiler.relocs" | grep -c 'R_X86_64_PC32')" = 8 ]
    for op in 'xsave64' 'xrstor64' 'xrstors64' 'xsaves64'; do
        grep -Fq "$op" "$scratch/$compiler.dump"
    done
done
readelf -x __ex_table "$scratch/minic.o" > "$scratch/ex-table.hex"
[ "$(grep -o '0f000000' "$scratch/ex-table.hex" | wc -l)" = 4 ]

# Fail closed on an invalid ModRM, wrong extable type, swapped fixed RDI,
# or removal of the explicit compiler memory barrier.
sed 's/0x0f,0xae,0x27/0x0f,0xae,0x26/' \
    "$scratch/xstate.i" > "$scratch/bad-opcode.i"
sed 's/"15"/"16"/g' \
    "$scratch/xstate.i" > "$scratch/bad-extable.i"
sed 's/"D"(p)/"S"(p)/g' \
    "$scratch/xstate.i" > "$scratch/bad-rdi.i"
sed 's/: "memory");/: "cc");/g' \
    "$scratch/xstate.i" > "$scratch/bad-barrier.i"
for bad in opcode extable rdi barrier; do
    if "$mini" --target=x86_64-linux -S "$scratch/bad-$bad.i" \
        -o "$scratch/bad-$bad.s" >"$scratch/bad-$bad.log" 2>&1; then
        printf 'X86_XSTATE_FAULT_M1=FAIL admitted malformed %s\n' "$bad" >&2
        exit 1
    fi
done

if [ -n "$original" ]; then
    test -f "$original"
    "$mini" --target=x86_64-linux -S "$original" \
        -o "$scratch/original.s" >"$scratch/original.log" 2>&1
    as --64 "$scratch/original.s" -o "$scratch/original.o"
    readelf -SW "$scratch/original.o" | grep -Fq '__ex_table'
    printf 'X86_XSTATE_ORIGINAL_GNU_AS=PASS source=%s\n' "$original"
fi
printf 'X86_XSTATE_FAULT_M1=PASS variants=4 extable_bytes=48 pc32_relocs=8 negative=4 GCC_compare=PASS\n'
