#!/usr/bin/env bash
# GNU -EL is an explicit output-endianness request, not an invitation to
# reinterpret little-endian input as a new ABI. Every currently admitted
# MiniLD ELF target is little-endian; -EB must fail before an output appears.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
for tool in readelf cmp; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "MINILD_ENDIAN_CHECK=INCONCLUSIVE missing=$tool" >&2
        exit 8
    }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-endian.XXXXXX")
finish() {
    if [[ ${MINILD_ENDIAN_KEEP:-0} == 1 ]]; then
        echo "MINILD_ENDIAN_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

cat >"$work/input.s" <<'ASM'
.section .text,"ax",@progbits
.globl _start
_start:
    .byte 0
    .byte 0
    .byte 0
    .byte 0
ASM

for spec in \
    'rv32:riscv:rv32i:ilp32:elf32lriscv:ELF32:RISC-V' \
    'rv64:riscv:rv64i:lp64:elf64lriscv:ELF64:RISC-V' \
    'x86_64:x86_64:x86-64:sysv:elf_x86_64:ELF64:Advanced' \
    'aarch64:aarch64:armv8-a:lp64:aarch64linux:ELF64:AArch64' \
    'loongarch64:loongarch64:loongarch64:lp64d:elf64loongarch:ELF64:LoongArch'; do
    IFS=: read -r name target march mabi emulation elf_class machine <<<"$spec"
    "$MINIAS" "--target=$target" "-march=$march" "-mabi=$mabi" \
        -o "$work/$name.o" "$work/input.s"
    "$MINILD" -m "$emulation" -static -e _start \
        -o "$work/$name-default.elf" "$work/$name.o"
    "$MINILD" -m "$emulation" -static -EL -e _start \
        -o "$work/$name-little.elf" "$work/$name.o"
    cmp "$work/$name-default.elf" "$work/$name-little.elf" || {
        echo "MINILD_ENDIAN_CHECK=FAIL explicit-little-changed-output arch=$name" >&2
        exit 1
    }
    readelf -W -h "$work/$name-little.elf" \
        >"$work/$name-readelf.txt"
    grep -q "Class:.*$elf_class" "$work/$name-readelf.txt"
    grep -q 'Data:.*little endian' "$work/$name-readelf.txt"
    grep -q "Machine:.*$machine" "$work/$name-readelf.txt"

    if "$MINILD" -m "$emulation" -static -EB -e _start \
        -o "$work/$name-big.elf" "$work/$name.o" \
        >"$work/$name-big.stdout" 2>"$work/$name-big.stderr"; then
        echo "MINILD_ENDIAN_CHECK=FAIL accepted-unsupported-big arch=$name" >&2
        exit 1
    fi
    test ! -e "$work/$name-big.elf"
    grep -q "unsupported-target-endianness:$emulation:big" \
        "$work/$name-big.stderr"
    echo "MINILD_ENDIAN_CHECK=PASS arch=$name -EL=exact-default -EB=reject"
done

if "$MINILD" -m aarch64linux -static -EL -EB -e _start \
    -o "$work/conflicting.elf" "$work/aarch64.o" \
    >"$work/conflicting.stdout" 2>"$work/conflicting.stderr"; then
    echo 'MINILD_ENDIAN_CHECK=FAIL accepted-conflicting-options' >&2
    exit 1
fi
test ! -e "$work/conflicting.elf"
grep -q 'conflicting-endianness-options' "$work/conflicting.stderr"

# Verify the GNU BFD oracle accepts -EL on the same x86 object. ELF payload
# bytes can be laid out differently, but class, machine and byte order agree.
if command -v ld >/dev/null 2>&1; then
    ld -m elf_x86_64 -EL -static -e _start \
        -o "$work/x86_64-gnu-little.elf" "$work/x86_64.o"
    readelf -W -h "$work/x86_64-gnu-little.elf" >"$work/x86_64-gnu.txt"
    grep -q 'Data:.*little endian' "$work/x86_64-gnu.txt"
fi
echo 'MINILD_ENDIAN_CHECK=PASS five-target-explicit-little-and-refuse-unsupported-big'
