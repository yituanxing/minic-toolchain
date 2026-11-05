#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in as ld readelf objcopy cmp awk diff; do
  command -v "$tool" >/dev/null
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# The unmodified Linux 6.6.143 arch/x86/boot/header.S emits this exact
# unresolved setup_sects byte in .header; GAS keeps R_386_8 with a -1
# embedded REL addend.  Reject silently resolving the byte to zero or
# merely accepting the source without surviving GNU ld.
cat >"$work/header.s" <<'ASM'
.code16
.section ".header", "a"
hdr:
.byte setup_sects - 1
.byte 0xa5
.byte 0xeb
.byte start_of_setup-1f
1:
.section ".entrytext", "ax"
start_of_setup:
nop
ASM

as --32 -o "$work/gnu.o" "$work/header.s"
"$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
  -o "$work/mini.o" "$work/header.s"

readelf -rW "$work/gnu.o" >"$work/gnu.reloc"
readelf -rW "$work/mini.o" >"$work/mini.reloc"
awk '/R_386_/ {print $1,$3,$5}' "$work/gnu.reloc" >"$work/gnu.tuples"
awk '/R_386_/ {print $1,$3,$5}' "$work/mini.reloc" >"$work/mini.tuples"
diff -u "$work/gnu.tuples" "$work/mini.tuples"
grep -q 'R_386_8.*setup_sects' "$work/mini.reloc"
grep -q 'R_386_PC8' "$work/mini.reloc"

objcopy --dump-section .header="$work/gnu.header" "$work/gnu.o"
objcopy --dump-section .header="$work/mini.header" "$work/mini.o"
cmp "$work/gnu.header" "$work/mini.header"

# GNU LD consumes the relocation after a real absolute link-time symbol
# definition. Place the real-mode header and its near entry consecutively,
# just as the Linux setup linker must do for an 8-bit relative branch.
cat >"$work/link.ld" <<'LD'
SECTIONS {
  . = 0x1000;
  .header : { *(.header) }
  .entrytext : { *(.entrytext) }
}
LD
ld -m elf_i386 -T "$work/link.ld" -e 0 --defsym setup_sects=6 \
  -o "$work/gnu.elf" "$work/gnu.o"
ld -m elf_i386 -T "$work/link.ld" -e 0 --defsym setup_sects=6 \
  -o "$work/mini.elf" "$work/mini.o"
objcopy --dump-section .header="$work/gnu.final" "$work/gnu.elf"
objcopy --dump-section .header="$work/mini.final" "$work/mini.elf"
cmp "$work/gnu.final" "$work/mini.final"
test "$(od -An -tu1 -N1 "$work/mini.final" | tr -d ' ')" = 5
echo 'MINIAS_X86_BOOT_HEADER=PASS ELF32_R_386_8=GNU_exact ELF32_R_386_PC8=GNU_exact REL_addend=exact GNU_ld=PASS'
