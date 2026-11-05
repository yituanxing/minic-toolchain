#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head MiniAS executable}"
loong_as=${LOONGAS:-loongarch64-linux-gnu-as}
loong_ld=${LOONGLD:-loongarch64-linux-gnu-ld}

command -v python3 >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Every B16/B21 opcode and its GNU pseudoinstruction aliases must share the
# same short/long range planner, including the FCC inversion (bit 8 rather
# than the integer compare bit 26).  One object with independent .text.*
# sections exercises forward/backward bounds without materializing hundreds
# of separate linkers; GNU ld -r then concatenates independent copies.
declare -a cases=(
  'beq|$r4,$r5|16|forward'
  'bne|$r4,$r5|16|forward'
  'blt|$r4,$r5|16|forward'
  'bge|$r4,$r5|16|forward'
  'bltu|$r4,$r5|16|forward'
  'bgeu|$r4,$r5|16|forward'
  'bltz|$r4|16|forward'
  'bgtz|$r4|16|forward'
  'bgez|$r4|16|forward'
  'blez|$r4|16|forward'
  'bgt|$r4,$r5|16|forward'
  'ble|$r4,$r5|16|forward'
  'bgtu|$r4,$r5|16|forward'
  'bleu|$r4,$r5|16|forward'
  'beqz|$r4|21|forward'
  'bnez|$r4|21|forward'
  'bceqz|$fcc3|21|forward'
  'bcnez|$fcc3|21|forward'
  'beq|$r4,$r5|16|backward'
  'bceqz|$fcc3|21|backward'
  'beq|$r4,$r5|16|near'
  'bceqz|$fcc3|21|near'
)

case_count=0
for scenario in "${cases[@]}"; do
    IFS='|' read -r name operands width direction <<<"$scenario"
    label="case${case_count}"
    case_count=$((case_count+1))
    if [[ $direction == near ]]; then
        gap=0
    elif [[ $width == 16 ]]; then
        gap=131072
    else
        gap=4194304
    fi
    {
        printf '.section .text.%s,"ax",@progbits\n' "$label"
        if [[ $direction == backward ]]; then
            printf '%s:\n nop\n .space %u\n' "${label}_target" "$gap"
            printf '%s:\n %s %s,%s_target\n' "${label}_entry" "$name" "$operands" "$label"
        else
            printf '%s:\n %s %s,%s_target\n' "${label}_entry" "$name" "$operands" "$label"
            printf ' .space %u\n%s_target:\n nop\n' "$gap" "$label"
        fi
        if [[ $label == case0 ]]; then
            # This section also contains a shrinkable PCALA pair, forcing
            # GNU and MiniAS to retain the long branch's B26 relocation.
            printf ' la.local $r12,external_data\n'
        fi
    } >>"$work/branches.s"
done
printf '.data\nexternal_data:\n .dword 0\n' >>"$work/branches.s"

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/mini.o" "$work/branches.s"

# Run architecture-owned byte/width tests even on machines lacking the GNU
# cross toolchain; such a result is explicitly *not* a GNU parity PASS.
python3 - "$work/mini.o" <<'PY'
import struct
import sys
from elftools.elf.elffile import ELFFile

with open(sys.argv[1], 'rb') as source:
    elf = ELFFile(source)
    inversion = {
        'beq': 0x5c000000, 'bne': 0x58000000,
        'blt': 0x64000000, 'bge': 0x60000000,
        'bltu': 0x6c000000, 'bgeu': 0x68000000,
        'bltz': 0x64000000, 'bgtz': 0x64000000,
        'bgez': 0x60000000, 'blez': 0x60000000,
        'bgt': 0x64000000, 'ble': 0x60000000,
        'bgtu': 0x6c000000, 'bleu': 0x68000000,
        'beqz': 0x44000000, 'bnez': 0x40000000,
        'bceqz': 0x48000100, 'bcnez': 0x48000000,
    }
    cases = [
        ('beq', 16, 'forward'), ('bne', 16, 'forward'),
        ('blt', 16, 'forward'), ('bge', 16, 'forward'),
        ('bltu', 16, 'forward'), ('bgeu', 16, 'forward'),
        ('bltz', 16, 'forward'), ('bgtz', 16, 'forward'),
        ('bgez', 16, 'forward'), ('blez', 16, 'forward'),
        ('bgt', 16, 'forward'), ('ble', 16, 'forward'),
        ('bgtu', 16, 'forward'), ('bleu', 16, 'forward'),
        ('beqz', 21, 'forward'), ('bnez', 21, 'forward'),
        ('bceqz', 21, 'forward'), ('bcnez', 21, 'forward'),
        ('beq', 16, 'backward'), ('bceqz', 21, 'backward'),
        ('beq', 16, 'near'), ('bceqz', 21, 'near'),
    ]
    for i, (name, width, direction) in enumerate(cases):
        section = elf.get_section_by_name(f'.text.case{i}')
        assert section is not None, (i, 'missing section')
        body = section.data()
        relocation_section = elf.get_section_by_name(f'.rela.text.case{i}')
        b26_offsets = ([] if relocation_section is None else
                       [entry['r_offset'] for entry in relocation_section.iter_relocations()
                        if entry['r_info_type'] == 66])
        short_relocations = ([] if relocation_section is None else
                             [(entry['r_offset'], entry['r_info_type'])
                              for entry in relocation_section.iter_relocations()
                              if entry['r_info_type'] in (64, 65)])
        gap = 0 if direction == 'near' else 131072 if width == 16 else 4194304
        branch_width = 4 if direction == 'near' else 8
        extra = 8 if i == 0 else 0
        assert len(body) == gap + branch_width + 4 + extra, (
            i, name, direction, 'wrong growth', len(body))
        if direction == 'near':
            assert not b26_offsets, (i, name, 'spurious long B26', b26_offsets)
            assert (0, 64 if width == 16 else 65) in short_relocations, (
                i, name, 'missing retained short relocation', short_relocations)
            continue
        pos = gap + 4 if direction == 'backward' else 0
        assert pos + 4 in b26_offsets, (
            i, name, 'missing long B26 relocation', pos + 4, b26_offsets)
        first, second = struct.unpack_from('<II', body, pos)
        high_mask = 0xfc000100 if name.startswith('bc') else 0xfc000000
        assert (first & high_mask) == inversion[name], (i, name, hex(first))
        assert (first >> 10) & 0xffff == 2, (i, name, 'not skip +8', hex(first))
        delta = -(gap + 8) if direction == 'backward' else gap + 4
        units = (delta // 4) & 0x3ffffff
        expected = 0x50000000 | ((units & 0xffff) << 10) | ((units >> 16) & 0x3ff)
        assert second == expected, (i, name, 'B26 displacement', hex(second), hex(expected))
    rela = elf.get_section_by_name('.rela.text.case0')
    assert rela is not None, 'missing linker-relaxable branch relocation section'
    assert any(e['r_offset'] == 4 and e['r_info_type'] == 66
               for e in rela.iter_relocations()), 'missing retained R_LARCH_B26 at +4'
print('MINIAS_LOONG_LONG_BRANCH=PASS local_structural=22 width=4_or_8 '
      'integer_and_FCC_inversion=exact retained_B26=20 retained_short=2')
PY

if ! command -v "$loong_as" >/dev/null ||
   ! command -v "$loong_ld" >/dev/null; then
    if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
        echo 'MINIAS_LOONG_LONG_BRANCH=BLOCKED GNU LoongArch as/ld missing' >&2
        exit 1
    fi
    echo 'MINIAS_LOONG_LONG_BRANCH=SKIP GNU LoongArch as/ld unavailable'
    exit 0
fi

"$loong_as" -mabi=lp64d -o "$work/gnu.o" "$work/branches.s"

# This is not an opcode-mask comparator: compare *entire* .text.* section
# bytes and exact per-section (offset/type/addend) relocation sequences.
# A change in expansion width, condition inversion or B26 field is visible.
compare_objects() {
    python3 - "$1" "$2" "$3" <<'PY'
import sys
from elftools.elf.elffile import ELFFile

stage, first, second = sys.argv[1:]

def read(path):
    with open(path, 'rb') as file:
        elf = ELFFile(file)
        sections = {}
        relocations = {}
        for section in elf.iter_sections():
            if (section.name.startswith('.text.case') or
                (stage == 'final-link' and section.name == '.text')):
                sections[section.name] = section.data()
            if section['sh_type'] in ('SHT_RELA', 'SHT_REL') and \
               (section.name.startswith(('.rela.text.case', '.rel.text.case')) or
                (stage == 'final-link' and section.name in ('.rela.text', '.rel.text'))):
                name = section.name.removeprefix('.rela').removeprefix('.rel')
                relocations[name] = [
                    (entry['r_offset'], entry['r_info_type'],
                     entry['r_addend'] if entry.is_RELA() else 0)
                    for entry in section.iter_relocations()
                ]
        return sections, relocations

gnu_sections, gnu_relocs = read(first)
mini_sections, mini_relocs = read(second)
expected = 1 if stage == 'final-link' else 22
assert len(gnu_sections) == expected, (stage, 'GNU section coverage', len(gnu_sections))
assert gnu_sections.keys() == mini_sections.keys(), (stage, 'section set mismatch')
differences = []
for name, reference in gnu_sections.items():
    actual = mini_sections[name]
    if reference != actual:
        first_diff = next((idx for idx, (g, m) in enumerate(zip(reference, actual))
                           if g != m), min(len(reference), len(actual)))
        differences.append((name, 'bytes', len(reference), len(actual), first_diff,
                            reference[max(0, first_diff - 4):first_diff + 12].hex(),
                            actual[max(0, first_diff - 4):first_diff + 12].hex()))
    if gnu_relocs.get(name, []) != mini_relocs.get(name, []):
        differences.append((name, 'relocations', gnu_relocs.get(name, []),
                            mini_relocs.get(name, [])))
if differences:
    raise SystemExit(f'MINIAS_LOONG_LONG_BRANCH=FAIL stage={stage} '
                     f'differences={len(differences)}\n' +
                     '\n'.join(map(str, differences)))
print(f'MINIAS_LOONG_LONG_BRANCH=PASS stage={stage} '
      f'cases={len(gnu_sections)} bytes=exact relocations=exact')
PY
}

compare_objects input "$work/gnu.o" "$work/mini.o"

# Compare GNU+GNU to GNU+MiniAS so relocation offsets and internal symbol
# references must survive the genuine GNU linker both in ET_REL and ET_EXEC.
"$loong_ld" -r -o "$work/gg-r.o" "$work/gnu.o" "$work/gnu.o"
"$loong_ld" -r -o "$work/gm-r.o" "$work/gnu.o" "$work/mini.o"
compare_objects ld-r "$work/gg-r.o" "$work/gm-r.o"

"$loong_ld" -e 0 -o "$work/gg-final.elf" "$work/gnu.o" "$work/gnu.o"
"$loong_ld" -e 0 -o "$work/gm-final.elf" "$work/gnu.o" "$work/mini.o"
compare_objects final-link "$work/gg-final.elf" "$work/gm-final.elf"
echo "MINIAS_LOONG_LONG_BRANCH=PASS scenarios=$case_count GNU_ld_r=1 GNU_final_link=1"
