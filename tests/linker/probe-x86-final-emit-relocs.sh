#!/usr/bin/env bash
# GNU --emit-relocs parity on a real ET_EXEC with scripted output-section
# merging. All entries must refer to the FINAL .symtab and final section
# addresses, not the original ET_REL section numbering/offsets.
set -Eeuo pipefail

: "${MINILD:?MINILD must name the exact-head linker}"
for tool in as ld readelf python3; do
    command -v "$tool" >/dev/null || exit 8
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-x86-final-relocs.XXXXXXXX")
finish() {
    if [[ ${MINILD_EMIT_RELOCS_KEEP:-0} == 1 ]]; then
        echo "MINILD_EMIT_RELOCS_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

printf '%s\n' \
    '.section .text.entry,"ax",@progbits' \
    '.globl _start' \
    '.type _start,@function' \
    '_start:' \
    '    call helper' \
    '    mov $60,%eax' \
    '    xor %edi,%edi' \
    '    syscall' \
    '.size _start,.-_start' \
    '.section .text.dead,"ax",@progbits' \
    'dead:' \
    '    call undefined_dead' >"$work/entry.s"
printf '%s\n' \
    '.section .text.helper,"ax",@progbits' \
    '.globl helper' \
    '.type helper,@function' \
    'helper:' \
    '    ret' \
    '.size helper,.-helper' \
    '.section .data.ptr,"aw",@progbits' \
    '.globl helper_ptr' \
    'helper_ptr:' \
    '    .quad helper' \
    '.section .debug_probe,"",@progbits' \
    '    .quad helper' >"$work/helper.s"
printf '%s\n' \
    'OUTPUT_ARCH(i386:x86-64)' \
    'ENTRY(_start)' \
    'SECTIONS {' \
    '  . = 0x400000;' \
    '  .text : { *(.text.entry) *(.text.helper) }' \
    '  . = ALIGN(0x1000);' \
    '  .data : { *(.data.ptr) }' \
    '  .debug_probe 0 : { *(.debug_probe) }' \
    '  /DISCARD/ : { *(.text.dead) }' \
    '}' >"$work/layout.lds"

as -o "$work/entry.o" "$work/entry.s"
as -o "$work/helper.o" "$work/helper.s"

for linker in ld "$MINILD"; do
    [[ $linker != ld ]] || owner=gnu
    [[ $linker == ld ]] || owner=mini
    "$linker" -m elf_x86_64 --emit-relocs -static -T "$work/layout.lds" \
        -o "$work/$owner.elf" "$work/entry.o" "$work/helper.o" \
        >"$work/$owner.log" 2>&1
    readelf -W -r "$work/$owner.elf" >"$work/$owner.rela"
    readelf -W -S "$work/$owner.elf" >"$work/$owner.sections"
    grep -Fq '.rela.text' "$work/$owner.rela"
    grep -Fq '.rela.data' "$work/$owner.rela"
    grep -Fq '.rela.debug_probe' "$work/$owner.rela"
    "$work/$owner.elf"
done

python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY'
import re
import subprocess
import sys

def snapshot(path):
    sections = {}
    indices = {}
    for line in subprocess.check_output(['readelf', '-W', '-S', path], text=True).splitlines():
        match = re.search(r'\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([A-Z]*)\s+(\d+)\s+(\d+)\s+(\d+)', line)
        if match:
            shndx, name, kind, addr, offset, size, flags, link, info, align = match.groups()
            sections[name] = (int(shndx), kind, int(addr, 16), int(size, 16), int(link), int(info))
            indices[int(shndx)] = name
    names = {}
    for line in subprocess.check_output(['readelf', '-W', '-s', path], text=True).splitlines():
        match = re.search(r'^\s*(\d+):\s+[0-9a-fA-F]+\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+(\S+)$', line)
        if match:
            names[int(match.group(1))] = match.group(2)
    relocs = {}
    current = None
    for line in subprocess.check_output(['readelf', '-W', '-r', path], text=True).splitlines():
        heading = re.match(r"Relocation section '(\.rela\S+)'", line)
        if heading:
            current = heading.group(1)
            relocs[current] = []
        if current is not None:
            match = re.match(r'^\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(R_X86_64_\w+)', line)
            if match:
                relocs[current].append((int(match.group(1), 16), match.group(3)))
    for name in ('.rela.text', '.rela.data', '.rela.debug_probe'):
        if name not in sections or name not in relocs or not relocs[name]:
            raise SystemExit('MINILD_EMIT_RELOCS=FAIL missing_relocations:' + path + ':' + name)
        idx, kind, _, _, link, info = sections[name]
        expected_target = name[len('.rela'):]
        if kind != 'RELA' or indices.get(link) != '.symtab' or indices.get(info) != expected_target:
            raise SystemExit('MINILD_EMIT_RELOCS=FAIL wrong_sh_link_or_info:' + path + ':' + name)
        start = sections[expected_target][2]
        size = sections[expected_target][3]
        if not all((start <= offset < start + size) if start else
                   (offset < size) for offset, _ in relocs[name]):
            raise SystemExit('MINILD_EMIT_RELOCS=FAIL wrong_final_reloc_offset:' + path + ':' + name)
    return relocs

gnu, mini = (snapshot(path) for path in sys.argv[1:])
for name in ('.rela.text', '.rela.data', '.rela.debug_probe'):
    if gnu[name] != mini[name]:
        raise SystemExit(f'MINILD_EMIT_RELOCS=FAIL GNU_mismatch:{name}:gnu={gnu[name]}:mini={mini[name]}')
print('MINILD_EMIT_RELOCS=PASS GNU_final_section_rela_parity=3/3 static_runtime=2/2')
PY

# Do not emit stale GOTPCRELX types when the static linker rewrites opcodes.
printf '%s\n' \
    '.section .text,"ax",@progbits' \
    '.globl _start' \
    '_start:' \
    '    mov helper@GOTPCREL(%rip),%rax' \
    '    ret' \
    '.globl helper' \
    'helper:' \
    '    ret' >"$work/relax.s"
as -o "$work/relax.o" "$work/relax.s"
if "$MINILD" -m elf_x86_64 --emit-relocs -static -e _start \
        -o "$work/invalid-relax.elf" "$work/relax.o" \
        >"$work/relax.log" 2>&1; then
    echo 'MINILD_EMIT_RELOCS=FAIL incorrect_relaxation_metadata_accepted' >&2
    exit 1
fi
grep -Fq 'unsupported-emit-relocs-transform' "$work/relax.log"
test ! -e "$work/invalid-relax.elf"

# -X removes .L* from final .symtab, while GAS may canonicalize a relocation
# against a local label into its containing section symbol.  Verify that
# that surviving section symbol gets a valid final r_info index.
printf '%s\n' \
    '.section .text,"ax",@progbits' \
    '.globl _start' \
    '_start:' \
    '    mov $.Lreferenced,%rax' \
    '    mov $60,%eax' \
    '    xor %edi,%edi' \
    '    syscall' \
    '.section .rodata,"a",@progbits' \
    '.Lreferenced:' \
    '    .quad 0' >"$work/discarded-local.s"
as -L -o "$work/discarded-local.o" "$work/discarded-local.s"
for linker in ld "$MINILD"; do
    [[ $linker != ld ]] || owner=gnu
    [[ $linker == ld ]] || owner=mini
    "$linker" -m elf_x86_64 -X --emit-relocs -static -e _start \
        -o "$work/$owner-discard-local.elf" "$work/discarded-local.o"
    readelf -W -s "$work/$owner-discard-local.elf" >"$work/$owner-local.symtab"
    readelf -W -r "$work/$owner-discard-local.elf" >"$work/$owner-local.relocs"
    ! grep -Fq '.Lreferenced' "$work/$owner-local.symtab"
    grep -Fq 'R_X86_64_32S' "$work/$owner-local.relocs"
    grep -Fq '.rodata' "$work/$owner-local.relocs"
    "$work/$owner-discard-local.elf"
done

# Scripted non-ALLOC sections whose VMA is nonzero need a separate final
# section-address policy; fail closed instead of outputting GNU-incompatible
# absolute relocation offsets against a zero-address section.
sed 's/\.debug_probe 0 :/\.debug_probe :/' \
    "$work/layout.lds" >"$work/layout-nonalloc-vma.lds"
if "$MINILD" -m elf_x86_64 --emit-relocs -static \
        -T "$work/layout-nonalloc-vma.lds" \
        -o "$work/invalid-nonalloc-vma.elf" "$work/entry.o" "$work/helper.o" \
        >"$work/nonalloc-vma.log" 2>&1; then
    echo 'MINILD_EMIT_RELOCS=FAIL unrepresentable_nonalloc_vma_accepted' >&2
    exit 1
fi
grep -Fq 'unsupported-emit-relocs-nonalloc-vma' "$work/nonalloc-vma.log"
test ! -e "$work/invalid-nonalloc-vma.elf"
echo 'MINILD_EMIT_RELOCS=PASS unsafe_target_transforms_and_symbol_vma=REJECT'
