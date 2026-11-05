#!/usr/bin/env bash
# Custom linker scripts suppress GNU's implicit default-script init-array
# PROVIDE markers. Extra invented markers corrupt Linux kallsyms symbol sets.
set -Eeuo pipefail
: "${MINILD:?MINILD required}"
for tool in as ld nm readelf; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT

printf '%s\n' '.text' '.global _start' '_start:' \
  'mov $60, %eax' 'xor %edi, %edi' 'syscall' >"$work/start.s"
as -o "$work/start.o" "$work/start.s"
printf '%s\n' 'SECTIONS {' '  . = 0x400000;' \
  '  .text : { *(.text .text.*) }' \
  '  .data : { *(.data .data.*) }' '}' >"$work/custom.lds"
printf '%s\n' 'SECTIONS {' '  . = 0x400000;' \
  '  .text : { *(.text .text.*) }' \
  '  .data : { __init_array_start = .; __init_array_end = .; *(.data) }' \
  '}' >"$work/explicit.lds"

for linker in ld "$MINILD"; do
  if [[ $linker == ld ]]; then owner=gnu; else owner=mini; fi
  "$linker" -m elf_x86_64 -T "$work/custom.lds" \
    -o "$work/$owner-custom.elf" "$work/start.o"
  if nm "$work/$owner-custom.elf" | grep -Eq \
      '__((pre)?init|fini)_array_(start|end)'; then
    echo "MINILD_SCRIPT_ARRAY_BOUNDS=FAIL invented_boundaries:$owner" >&2
    exit 1
  fi
  "$linker" -m elf_x86_64 -T "$work/explicit.lds" \
    -o "$work/$owner-explicit.elf" "$work/start.o"
  nm "$work/$owner-explicit.elf" | grep -Eq ' __init_array_start$'
  nm "$work/$owner-explicit.elf" | grep -Eq ' __init_array_end$'
  if nm "$work/$owner-explicit.elf" | grep -Eq \
      '__preinit_array_(start|end)|__fini_array_(start|end)'; then
    echo "MINILD_SCRIPT_ARRAY_BOUNDS=FAIL invented_unrelated_boundaries:$owner" >&2
    exit 1
  fi
done

# The GNU default link script may PROVIDE start/end when the input references
# them; retain this (unscripted) path and compare both linkers' outcomes.
printf '%s\n' '.text' '.global _start' '_start:' \
  'mov $60, %eax' 'xor %edi, %edi' 'syscall' \
  '.data' '.quad __init_array_start' '.quad __init_array_end' \
  >"$work/referenced.s"
as -o "$work/referenced.o" "$work/referenced.s"
for linker in ld "$MINILD"; do
  if [[ $linker == ld ]]; then owner=gnu; else owner=mini; fi
  "$linker" -m elf_x86_64 -o "$work/$owner-default.elf" \
    "$work/referenced.o"
  nm "$work/$owner-default.elf" | grep -Eq ' __init_array_start$'
  nm "$work/$owner-default.elf" | grep -Eq ' __init_array_end$'
done

for linker in ld "$MINILD"; do
  if [[ $linker == ld ]]; then owner=gnu; else owner=mini; fi
  if "$linker" -m elf_x86_64 -T "$work/custom.lds" \
      -o "$work/$owner-should-fail.elf" "$work/referenced.o" \
      >"$work/$owner-should-fail.stdout" \
      2>"$work/$owner-should-fail.stderr"; then
    echo "MINILD_SCRIPT_ARRAY_BOUNDS=FAIL silently_synthesized_when_unresolved:$owner" >&2
    exit 1
  fi
done

echo 'MINILD_SCRIPT_ARRAY_BOUNDS=PASS GNU_BFD_custom_absence=true explicit_script_only=true unscripted_default_present=true true_unresolved_rejected=true'
