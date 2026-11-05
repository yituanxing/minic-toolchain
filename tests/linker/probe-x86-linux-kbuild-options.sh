#!/usr/bin/env bash
# Real GNU Kbuild option semantics on one small, reproducible x86 ET_EXEC.
# This does not claim Linux vmlinux pass; the frozen archive is replayed apart.
set -Eeuo pipefail

: "${MINILD:?MINILD must name the exact linker under test}"
for cmd in as ld readelf objcopy python3; do
    command -v "$cmd" >/dev/null || exit 8
done

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-x86-kbuild-options.XXXXXXXX")
finish() {
    if [[ ${MINILD_X86_KBUILD_KEEP:-0} == 1 ]]; then
        echo "MINILD_X86_KBUILD_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

printf '%s\n' \
    '.section .text,"ax",@progbits' \
    '.globl _start' \
    '_start:' \
    '  mov $60,%eax' \
    '  xor %edi,%edi' \
    '  syscall' \
    '.section .rodata,"a",@progbits' \
    '.Ltest:' \
    '  .quad 0x12345678' \
    '.section .data,"aw",@progbits' \
    '  .quad 1234' >"$work/source.s"
as -L -o "$work/source.o" "$work/source.s"

printf '%s\n' \
    'OUTPUT_FORMAT("elf64-x86-64")' \
    'OUTPUT_ARCH(i386:x86-64)' \
    'ENTRY(_start)' \
    'PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); note PT_NOTE FLAGS(0); }' \
    'SECTIONS {' \
    '  . = 0x400000;' \
    '  .text : AT(ADDR(.text) - 0x400000) { *(.text) . = ALIGN(32); } :text = 0x11223344' \
    '  . = ALIGN(0x400100, 0x1000);' \
    '  .rodata : AT(ADDR(.rodata) - 0x400000) { *(.rodata) } :text' \
    '  .notes : AT(ADDR(.notes) - 0x400000) { *(.note.*) } :text :note' \
    '  . = ALIGN(0x500000, 0x1000);' \
    '  .data : AT(ADDR(.data) - 0x400000) { *(.data) . += 16; } :data' \
    '  .bss : { *(.bss) } :data' \
    '  .got (INFO) : { *(.got) }' \
    '  ASSERT(SIZEOF(.got) == 0, "GOT must be empty")' \
    '  .got.plt (INFO) : { *(.got.plt) }' \
    '  .rela.got : { *(.rela.got) }' \
    '  .iplt : { *(.iplt) }' \
    '  .rela.iplt : { *(.rela.iplt) }' \
    '  .igot.plt : { *(.igot.plt) }' \
    '}' \
    'PROVIDE(alias = _start);' \
    '. = ASSERT((_start & 3) == 0, "entry must be aligned");' \
    >"$work/kernel.lds"

for linker in ld "$MINILD"; do
    owner=mini
    [[ $linker != ld ]] || owner=gnu
    "$linker" -m elf_x86_64 -static --emit-relocs -X --discard-none \
        -z max-page-size=0x200000 --orphan-handling=error \
        --no-warn-rwx-segments --build-id=sha1 \
        -T "$work/kernel.lds" -o "$work/$owner.elf" "$work/source.o" \
        >"$work/$owner.log" 2>&1
    readelf -W -l "$work/$owner.elf" >"$work/$owner.phdr"
    readelf -W -n "$work/$owner.elf" >"$work/$owner.note"
    readelf -W -s "$work/$owner.elf" >"$work/$owner.symtab"
    objcopy --dump-section .text="$work/$owner.text" "$work/$owner.elf"
    grep -Eq '^  NOTE[[:space:]]' "$work/$owner.phdr"
    grep -Eq '^[[:space:]]+[0-9]+[[:space:]]+\.notes' "$work/$owner.phdr"
    grep -Fq '.Ltest' "$work/$owner.symtab"
    grep -Fq 'NT_GNU_BUILD_ID' "$work/$owner.note"
    "$work/$owner.elf"
done

python3 - "$work/gnu.phdr" "$work/mini.phdr" \
          "$work/gnu.text" "$work/mini.text" <<'PY'
import re
import sys
def loads(path):
    rows = []
    for line in open(path, encoding='utf-8'):
        if re.match(r'^  LOAD\s', line):
            fields = line.split()
            rows.append((int(fields[1], 16), int(fields[2], 16),
                         int(fields[3], 16), int(fields[-1], 16)))
    return rows
gnu, mini = (loads(path) for path in sys.argv[1:3])
if not gnu or not mini or len(gnu) != len(mini):
    raise SystemExit(f'X86_KBUILD_OPTIONS=FAIL loads GNU={gnu} Mini={mini}')
if gnu != mini:
    raise SystemExit(f'X86_KBUILD_OPTIONS=FAIL PHDR_affine_GNU_mismatch GNU={gnu} Mini={mini}')
if any(row[-1] != 0x200000 for row in mini):
    raise SystemExit('X86_KBUILD_OPTIONS=FAIL p_align_not_2MiB')
with open(sys.argv[3], 'rb') as fh:
    gnu_text = fh.read()
with open(sys.argv[4], 'rb') as fh:
    mini_text = fh.read()
if gnu_text != mini_text or gnu_text[9:17] != b'\x11\x22\x33\x44' * 2:
    raise SystemExit('X86_KBUILD_OPTIONS=FAIL multi_byte_script_fill_GNU_mismatch')
print(f'X86_KBUILD_OPTIONS=PASS PHDR_LOADED={len(mini)} p_align_2MiB=true GNU_offset_vaddr_paddr_parity=true 4byte_fill_GNU_match=true')
PY

# GNU's --orphan-handling=error is not an ignored spelling. An alloc input
# section unmatched by the script must fail before writing an output image.
printf '%s\n' '.section .unplaced,"a",@progbits' '.byte 1' \
    >"$work/orphan.s"
as -o "$work/orphan.o" "$work/orphan.s"
for linker in ld "$MINILD"; do
    owner=mini
    [[ $linker != ld ]] || owner=gnu
    if "$linker" -m elf_x86_64 -static --orphan-handling=error \
        -T "$work/kernel.lds" -o "$work/$owner-orphan.elf" \
        "$work/source.o" "$work/orphan.o" \
        >"$work/$owner-orphan.log" 2>&1; then
        echo "X86_KBUILD_OPTIONS=FAIL orphan_accepted:$owner" >&2
        exit 1
    fi
    test ! -e "$work/$owner-orphan.elf"
done

if "$MINILD" -m elf_x86_64 -z max-page-size=0x300000 -o "$work/bad.elf" \
        "$work/source.o" >"$work/page.log" 2>&1; then
    echo 'X86_KBUILD_OPTIONS=FAIL invalid_page_size_accepted' >&2
    exit 1
fi
grep -Fq 'invalid-max-page-size' "$work/page.log"
if "$MINILD" -m elf_x86_64 --fix-cortex-a53-843419 -o "$work/bad.elf" \
        "$work/source.o" >"$work/cortex.log" 2>&1; then
    echo 'X86_KBUILD_OPTIONS=FAIL cross_arch_erratum_accepted' >&2
    exit 1
fi
grep -Fq 'requires-aarch64-static-or-kernel-image' "$work/cortex.log"
echo 'X86_KBUILD_OPTIONS=PASS orphan_handling=error invalid_page_size=reject cortex_cross_arch=reject'
