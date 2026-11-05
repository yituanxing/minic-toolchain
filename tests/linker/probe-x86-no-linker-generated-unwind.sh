#!/usr/bin/env bash
# Linux 6.6.143 arch/x86/boot/compressed/Makefile passes this exact GNU flag.
# MiniLD never synthesizes its own unwind records; accepting the explicit
# GNU prohibition must leave ELF output unchanged, and typos must fail.
set -Eeuo pipefail
: "${MINILD:?MINILD required}"
for tool in as ld readelf cmp; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT

printf '%s\n' '.section .text,"ax",@progbits' \
  '.globl _start' '_start:' 'mov $60,%eax' 'xor %edi,%edi' 'syscall' \
  >"$work/entry.s"
as -o "$work/entry.o" "$work/entry.s"
printf '%s\n' 'SECTIONS {' '  . = 0x1000;' \
  '  .text : { *(.text) }' '  .data : { *(.data) }' '}' \
  >"$work/compressed.lds"

for linker in ld "$MINILD"; do
  if [[ $linker == ld ]]; then owner=gnu; else owner=mini; fi
  "$linker" -m elf_x86_64 -pie --no-dynamic-linker -T "$work/compressed.lds" \
    --no-ld-generated-unwind-info -o "$work/$owner.elf" "$work/entry.o"
  readelf -W -h "$work/$owner.elf" | grep -Eq 'Machine:.*X86-64'
  ! readelf -W -l "$work/$owner.elf" | grep -Fq 'INTERP'
  if readelf -W -S "$work/$owner.elf" | grep -Eq '\.eh_frame(_hdr)?([[:space:]]|$)'; then
    echo "MINILD_X86_NO_GENERATED_UNWIND=FAIL synthesized_unwind=$owner" >&2
    exit 1
  fi
done

"$MINILD" -m elf_x86_64 -pie --no-dynamic-linker -T "$work/compressed.lds" \
  -o "$work/mini-default.elf" "$work/entry.o"
readelf -W -S "$work/mini-default.elf" >"$work/mini-default.sections"
readelf -W -S "$work/mini.elf" >"$work/mini-with-flag.sections"
cmp -s "$work/mini-default.sections" "$work/mini-with-flag.sections" || {
  echo 'MINILD_X86_NO_GENERATED_UNWIND=FAIL unexpected_linker_section_change' >&2
  exit 1
}

if "$MINILD" -m elf_x86_64 -pie --no-dynamic-linker -T "$work/compressed.lds" \
    --no-ld-generated-unwind-info-typo \
    -o "$work/typo.elf" "$work/entry.o" \
    >"$work/typo.stdout" 2>"$work/typo.stderr"; then
  echo 'MINILD_X86_NO_GENERATED_UNWIND=FAIL accepted_typo' >&2
  exit 1
fi
grep -Fq 'unsupported-option:--no-ld-generated-unwind-info-typo' "$work/typo.stderr"
[[ ! -e $work/typo.elf ]]
if "$MINILD" -m elf_x86_64 -pie --no-dynamic-linker \
    -o "$work/no-script.elf" "$work/entry.o" \
    >"$work/no-script.stdout" 2>"$work/no-script.stderr"; then
  echo 'MINILD_X86_NO_GENERATED_UNWIND=FAIL admitted_scriptless_kernel_pie' >&2
  exit 1
fi
grep -Fq -- '--no-dynamic-linker-requires-static-link' "$work/no-script.stderr"
[[ ! -e $work/no-script.elf ]]
echo 'MINILD_X86_NO_GENERATED_UNWIND=PASS GNU_BFD_accepts=true MiniLD_no_synthetic_unwind=true exact_x86_compressed_static_PIE=true typo_and_scriptless_rejected=true'
