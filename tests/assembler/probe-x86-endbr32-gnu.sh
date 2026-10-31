#!/usr/bin/env bash
# The Linux i386 vDSO and 32-bit boot paths emit ENDBR32 when IBT is enabled.
# Compare actual GNU GAS bytes for both i386 ELF32 and mixed-mode ELF64.
set -Eeuo pipefail

: "${MINIAS:?provide MiniAS binary}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/endbr.s" <<'ASM'
.text
.code32
.globl __kernel_endbr32
__kernel_endbr32:
    endbr32
    nop
.code64
    endbr64
    endbr32
ASM

for abi in elf32 elf64; do
    if [[ "$abi" == elf32 ]]; then
        flags=(--target=x86_64 -march=i386 -mabi=i386)
        gas=(--32)
    else
        flags=(--target=x86_64)
        gas=(--64)
    fi
    "$MINIAS" "${flags[@]}" -o "$work/$abi-mini.o" "$work/endbr.s"
    as "${gas[@]}" -o "$work/$abi-gnu.o" "$work/endbr.s"
    objcopy -j .text -O binary "$work/$abi-mini.o" "$work/$abi-mini.bin"
    objcopy -j .text -O binary "$work/$abi-gnu.o" "$work/$abi-gnu.bin"
    cmp "$work/$abi-mini.bin" "$work/$abi-gnu.bin"
    if [[ "$abi" == elf32 ]]; then
        readelf -h "$work/$abi-mini.o" | grep -q 'Class:.*ELF32'
    else
        readelf -h "$work/$abi-mini.o" | grep -q 'Class:.*ELF64'
    fi
done

printf '.text\n.code32\nendbr32 %%eax\n' >"$work/invalid.s"
if "$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
    -o "$work/invalid.o" "$work/invalid.s" >/dev/null 2>&1; then
    echo 'MINIAS_X86_ENDBR32=FAIL spurious_operand_accepted' >&2
    exit 1
fi
echo 'MINIAS_X86_ENDBR32=PASS GNU_bytes=exact elf32=1 mixed_elf64=1 invalid=1'
