#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head MiniAS executable}"
loong_as=${LOONGAS:-loongarch64-linux-gnu-as}
loong_ld=${LOONGLD:-loongarch64-linux-gnu-ld}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# GNU GAS 2.42 has -mrelax by default and R_LARCH_ALIGN, but predates the
# in-source .option push/pop/relax/norelax spelling added in May 2024.
# Keep the GNU 2.42 differential and modern scoped-option tests separate.
cat >"$work/align.s" <<'ASM'
.section .text.always,"ax",@progbits
    nop
    .p2align 4
    nop
.section .text.maxskip,"ax",@progbits
    nop
    .p2align 5,,4
    nop
.section .text.balign,"ax",@progbits
    nop
    .balign 16,,3
    nop
.section .text.small,"ax",@progbits
    nop
    .p2align 2
    nop
.section .text.fill,"ax",@progbits
    nop
    .p2align 4,0x12
    nop
.section .text.linker,"ax",@progbits
.globl _start
_start:
    la.local $a0,link_target
    .p2align 5
link_target:
    ret
.section .text.alt,"ax",@progbits
    nop
    .balign 32,,4
    nop
.section .data.align,"aw",@progbits
    .byte 1
    .p2align 4
    .byte 2
ASM

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/mini.o" "$work/align.s"

# No GNU2.42 compiler option is needed to check that MiniAS preserves
# precise lexical relaxation scopes and the per-source MCInst snapshot.
cat >"$work/options.s" <<'ASM'
.section .text.options,"ax",@progbits
    la.local $a0,local
    .option push
    .option norelax
    la.local $a1,local
    .p2align 5
    .option pop
    la.local $a2,local
    .p2align 4
local:
    nop
ASM
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/options.o" "$work/options.s"

cat >"$work/options-underflow.s" <<'ASM'
.text
.option pop
ASM
if "$MINIAS" --target=loongarch64 -o "$work/invalid.o" \
    "$work/options-underflow.s" >"$work/underflow.out" 2>&1; then
    echo 'MINIAS_LOONG_ALIGN_RELAX=FAIL unmatched .option pop accepted' >&2
    exit 1
fi
grep -q 'loongarch-option-stack-underflow' "$work/underflow.out"

python3 - "$work/mini.o" "$work/options.o" <<'PY'
import struct
import sys
from elftools.elf.elffile import ELFFile

cases = {
    '.text.always': (20, [(4, 102, 0x4)]),
    '.text.maxskip': (36, [(4, 102, 0x405)]),
    '.text.balign': (20, [(4, 102, 0x304)]),
    '.text.small': (8, []),
    '.text.fill': (20, []),
    '.text.linker': (40, [(8, 102, 0x5)]),
    '.text.alt': (36, [(4, 102, 0x405)]),
    '.data.align': (17, []),
}

def relocs(elf, name):
    section = elf.get_section_by_name('.rela' + name)
    if section is None:
        return []
    return list(section.iter_relocations())

with open(sys.argv[1], 'rb') as handle:
    elf = ELFFile(handle)
    symtab = elf.get_section_by_name('.symtab')
    assert symtab is not None
    for name, (length, align) in cases.items():
        section = elf.get_section_by_name(name)
        assert section is not None, ('missing', name)
        assert len(section.data()) == length, (name, 'width', len(section.data()), length)
        marks = [(r['r_offset'], r['r_info_type'], r['r_addend'])
                 for r in relocs(elf, name) if r['r_info_type'] == 102]
        assert marks == align, (name, 'R_LARCH_ALIGN', marks, align)
        if align:
            assert section.data()[align[0][0]:align[0][0]+4] == \
                   bytes.fromhex('00004003'), (name, 'not NOP reservation')
        for reloc in relocs(elf, name):
            if reloc['r_info_type'] != 102:
                continue
            symbol = symtab.get_symbol(reloc['r_info_sym'])
            assert reloc['r_info_sym'] != 0 and symbol['st_shndx'] == \
                   elf.get_section_index(name), (name, 'foreign ALIGN anchor')
    linker = relocs(elf, '.text.linker')
    assert sum(r['r_info_type'] == 100 for r in linker) == 2, \
           'missing la.local R_LARCH_RELAX pair'
    body = elf.get_section_by_name('.text.fill').data()
    assert body[4:16] == b'\x12' * 12, 'explicit fill unexpectedly became NOPs'

with open(sys.argv[2], 'rb') as handle:
    elf = ELFFile(handle)
    body = elf.get_section_by_name('.text.options').data()
    assert len(body) == 56, ('option scope reservation width', len(body))
    reloc = relocs(elf, '.text.options')
    relax_offsets = [r['r_offset'] for r in reloc if r['r_info_type'] == 100]
    assert relax_offsets == [0, 4, 32, 36], ('R_LARCH_RELAX scopes', relax_offsets)
    align = [(r['r_offset'], r['r_addend']) for r in reloc
             if r['r_info_type'] == 102]
    assert align == [(40, 0x4)], ('scoped R_LARCH_ALIGN', align)

print('MINIAS_LOONG_ALIGN_RELAX=PASS MiniAS alignment_markers=5 '
      'option_scopes=3 mode_snapshots=exact per_section_anchor=1')
PY

if ! command -v "$loong_as" >/dev/null ||
   ! command -v "$loong_ld" >/dev/null; then
    if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
        echo 'MINIAS_LOONG_ALIGN_RELAX=BLOCKED GNU LoongArch as/ld missing' >&2
        exit 1
    fi
    echo 'MINIAS_LOONG_ALIGN_RELAX=SKIP GNU LoongArch as/ld unavailable'
    exit 0
fi

"$loong_as" -mabi=lp64d -mrelax -o "$work/gnu.o" "$work/align.s"

# The CI oracle is GNU GAS 2.42: its symbolic R_LARCH_ALIGN addend encodes
# log2(alignment) and optional max-skip, unlike the newer ABS0 short form.
# Compare its exact r_offset/type/addend and input bytes; symbol-private names
# need not match. The dynamic effect is checked by final GNU linking below.
python3 - "$work/gnu.o" "$work/mini.o" <<'PY'
import sys
from elftools.elf.elffile import ELFFile

def section_contents(path):
    with open(path, 'rb') as handle:
        elf = ELFFile(handle)
        sections = {}
        relocations = {}
        for section in elf.iter_sections():
            if section.name.startswith(('.text.', '.data.align')):
                sections[section.name] = section.data()
                rela = elf.get_section_by_name('.rela' + section.name)
                relocations[section.name] = ([] if rela is None else [
                    (r['r_offset'], r['r_info_type'], r['r_addend'])
                    for r in rela.iter_relocations()])
        return sections, relocations

gnu_sections, gnu_relocations = section_contents(sys.argv[1])
mini_sections, mini_relocations = section_contents(sys.argv[2])
assert len(gnu_sections) == 8, ('GNU fixture incomplete', gnu_sections.keys())
assert gnu_sections.keys() == mini_sections.keys(), ('section set', gnu_sections.keys(), mini_sections.keys())
for name in gnu_sections:
    assert gnu_sections[name] == mini_sections[name], (name, 'input bytes differ', len(gnu_sections[name]), len(mini_sections[name]))
    assert gnu_relocations[name] == mini_relocations[name], \
           (name, 'relocations differ', gnu_relocations[name], mini_relocations[name])
print('MINIAS_LOONG_ALIGN_RELAX=PASS GNU input sections=8 bytes=exact relocs=exact')
PY

"$loong_ld" -e _start -o "$work/gnu.elf" "$work/gnu.o"
"$loong_ld" -e _start -o "$work/mini.elf" "$work/mini.o"
python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY'
import sys
from elftools.elf.elffile import ELFFile

def read(path):
    with open(path, 'rb') as source:
        elf = ELFFile(source)
        # A final GNU linker script is allowed to coalesce .text.* into one
        # .text. Compare the *entire* allocated code/data payload, never an
        # empty per-input-section dictionary (which would be false green).
        sections = {section.name: section.data() for section in elf.iter_sections()
                    if (section.name in ('.text', '.data') or
                        section.name.startswith(('.text.', '.data.align'))) and
                    section['sh_type'] != 'SHT_NOBITS' and
                    len(section.data()) > 0}
        symtab = elf.get_section_by_name('.symtab')
        symbol = next(s for s in symtab.iter_symbols() if s.name == 'link_target')
        return sections, symbol['st_value']

gnu_sections, gnu_target = read(sys.argv[1])
mini_sections, mini_target = read(sys.argv[2])
assert gnu_sections and sum(len(b) for b in gnu_sections.values()) >= 40, \
       ('GNU final text coverage missing', gnu_sections.keys())
assert gnu_sections.keys() == mini_sections.keys(), 'final section set mismatch'
for name in gnu_sections:
    assert gnu_sections[name] == mini_sections[name], (name, 'GNU final text mismatch')
assert gnu_target % 32 == 0, ('GNU target unaligned', hex(gnu_target))
assert mini_target % 32 == 0, ('MiniAS target unaligned', hex(mini_target))
print('MINIAS_LOONG_ALIGN_RELAX=PASS GNU final_link=1 target_align=32 '
      f'gnu_target=0x{gnu_target:x} mini_target=0x{mini_target:x}')
PY
