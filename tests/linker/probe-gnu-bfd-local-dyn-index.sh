#!/usr/bin/env bash
# Native-GNU ELF control: late linker-script PROVIDE of input HIDDEN UND
# occupies a local dynamic-symbol index even with .dynsym discarded.
set -Eeuo pipefail

work=$(mktemp -d "${TMPDIR:-/tmp}/gnu-bfd-local-dyn.XXXXXX")
trap 'rm -r -- "$work"' EXIT

cat >"$work/start.s" <<'ASM'
.text
.globl _start
_start:
    mov hidden_a@GOTPCREL(%rip), %rax
    mov hidden_b@GOTPCREL(%rip), %rax
    mov weak_external@GOTPCREL(%rip), %rax
    ret
.hidden hidden_a
.hidden hidden_b
.weak weak_external
ASM
as --64 -o "$work/start.o" "$work/start.s"

cat >"$work/one.lds" <<'LDS'
PROVIDE(hidden_a = ABSOLUTE(0x11223344));
PROVIDE(hidden_b = ABSOLUTE(0x55667788));
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  .got : { *(.got*) }
  .rela.dyn : { *(.rela.dyn) }
  /DISCARD/ : { *(.interp .dynamic .dynsym .dynstr .hash .gnu.hash) }
}
LDS

ld -m elf_x86_64 -shared -Bsymbolic -z notext \
   -T "$work/one.lds" -o "$work/one.so" "$work/start.o" \
   2>"$work/one.log"

sed '/\/DISCARD\/ :/d' "$work/one.lds" >"$work/keep.lds"
ld -m elf_x86_64 -shared -Bsymbolic -z notext \
   -T "$work/keep.lds" -o "$work/keep.so" "$work/start.o" \
   2>"$work/keep.log"

readelf -W --dyn-syms "$work/keep.so" >"$work/dynsym.txt"
grep -Eq '^ *1: .* LOCAL +DEFAULT +ABS hidden_a$' "$work/dynsym.txt"
grep -Eq '^ *2: .* LOCAL +DEFAULT +ABS hidden_b$' "$work/dynsym.txt"
grep -Eq '^ *3: .* WEAK +DEFAULT +UND weak_external$' "$work/dynsym.txt"

readelf -W -S "$work/one.so" >"$work/sections.txt"
if grep -Eq '\.(dynsym|dynamic|dynstr)' "$work/sections.txt"; then
    echo 'GNU_BFD_LOCAL_DYN=FAIL discarded_dynamic_metadata_reappeared' >&2
    exit 1
fi
for variant in one keep; do
    # readelf without .dynsym can misrender r_sym=3 as the unrelated
    # .symtab[3] hidden_b. Compare the raw r_info upper 32 bits instead.
    readelf -W -r "$work/$variant.so" | \
        awk '$3 == "R_X86_64_GLOB_DAT" {print substr($2, 1, 8)}' \
        >"$work/$variant.dynindices"
    grep -qx '00000003' "$work/$variant.dynindices"
done
echo 'GNU_BFD_LOCAL_DYN=PASS late_hidden_provide_local_prefix=2 weak_global_dynindex=3 discarded_dynsym_preserves_reloc_index=1'
