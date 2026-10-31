#!/usr/bin/env bash
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT

cc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
   -Wstrict-prototypes -Wmissing-prototypes -O2 -Werror \
   -I"$repo/elf/include" \
   "$repo/tests/elf/relocation_format_test.c" \
   "$repo/elf/src/relocatable_writer.c" \
   "$repo/elf/src/reader.c" \
   -o "$work/rel-format-test"

"$work/rel-format-test" "$work/i386.o" "$work/rv32.o"
readelf -h "$work/i386.o" | grep -q 'Class:.*ELF32'
readelf -h "$work/i386.o" | grep -q 'Machine:.*Intel 80386'
readelf -SW "$work/i386.o" | grep -Eq '[[:space:]]\.rel\.text[[:space:]]+REL[[:space:]]'
readelf -r "$work/i386.o" | grep -q 'R_386_PC32'
objdump -s -j .text "$work/i386.o" | grep -qi 'e8fcffff'

readelf -h "$work/rv32.o" | grep -q 'Class:.*ELF32'
readelf -SW "$work/rv32.o" | grep -Eq '[[:space:]]\.rela\.data[[:space:]]+RELA[[:space:]]'
readelf -r "$work/rv32.o" | grep -q 'R_RISCV_32'

printf '.text\n.globl callee\ncallee:\n  ret\n' | \
  as --32 -o "$work/callee.o" -
ld -m elf_i386 -nostdlib -e _start -o "$work/i386-linked" \
  "$work/i386.o" "$work/callee.o"
readelf -h "$work/i386-linked" | grep -q 'Machine:.*Intel 80386'
if command -v qemu-i386 >/dev/null 2>&1; then
  qemu-i386 "$work/i386-linked"
  echo 'MINIELF_RELOCATION_FORMAT_RUN=PASS qemu_i386_exit=0'
else
  # Some x86-64 CI containers disable IA32_EMULATION; GNU ld's successful
  # ELF32 link remains the mandatory contract, guest execution optional.
  echo 'MINIELF_RELOCATION_FORMAT_RUN=SKIP qemu_i386_unavailable'
fi

echo 'MINIELF_RELOCATION_FORMAT_LINK=PASS EM_386_SHT_REL_implicit_addend=1 GNU_ld_elf_i386=1 RV32_RELA_regression=1'
