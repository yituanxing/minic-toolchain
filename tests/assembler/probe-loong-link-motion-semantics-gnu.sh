#!/usr/bin/env bash
# Final *linked* LoongArch64 correctness across all consumers of a movable
# code interval: a fixed PCALA pair, two directional .word differences,
# DWARF line PCs, BOTH CFI sections' row PCs/FDE bounds, and debug ULEB128.
#
# Ubuntu GNU GAS 2.42 has -mrelax but predates lexical .option relax. Its
# source is therefore an independent globally relaxed baseline; MiniAS uses
# an explicit relaxable LA *between* a nonrelax LA and terminal norelax.
# Do not use object-byte equality as a surrogate for link-time semantics.
set -Eeuo pipefail

: "${MINIAS:?Point MINIAS to the exact-head minic-as binary}"
gas=${LOONGAS:-loongarch64-linux-gnu-as}
linker=${LOONGLD:-loongarch64-linux-gnu-ld}
copy=${LOONGCOPY:-loongarch64-linux-gnu-objcopy}
for program in "$gas" "$linker" "$copy" readelf python3; do
    if ! command -v "$program" >/dev/null 2>&1; then
        if [[ ${REQUIRE_ALL_GNU:-0} == 1 ]]; then
            echo "MINIAS_LOONG_LINK_MOTION=BLOCKED missing=$program" >&2
            exit 2
        fi
        echo "MINIAS_LOONG_LINK_MOTION=SKIP missing=$program"
        exit 0
    fi
done

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

# The first LA is never link-relaxable in the MiniAS input. It must still
# track a later LA instruction whose two words may shorten to one at link.
# Both .4byte values and the ULEB range cross that same shrinkable site.
cat >"$work/mini.s" <<'ASM'
.file 1 "loong-link-motion.S"
.cfi_sections .eh_frame, .debug_frame
.text
.option norelax
.globl motion_start
motion_start:
.cfi_startproc
.cfi_def_cfa $sp,0
.loc 1 1 0
    la.local $a2,motion_target
.globl motion_before_relax
motion_before_relax:
    .cfi_def_cfa_offset 16
.option relax
    la.local $a0,motion_target
.globl motion_dot_minus
motion_dot_minus:
    .cfi_def_cfa_offset 24
    .4byte .-motion_start
.globl motion_symbol_minus
motion_symbol_minus:
    .4byte motion_start-.
.globl motion_target
motion_target:
.cfi_def_cfa_offset 32
.loc 1 2 0
    addi.d $a1,$a1,0
.cfi_endproc
.globl motion_end
motion_end:
.section .debug_rnglists,"",@progbits
    .uleb128 motion_target-motion_start
.option norelax
ASM

# GNU GAS 2.42 supports the same instructions but not in-source .option.
# -mrelax allows it to choose its own linkage policy independently.
cat >"$work/gnu.s" <<'ASM'
.file 1 "loong-link-motion.S"
.cfi_sections .eh_frame, .debug_frame
.text
.globl motion_start
motion_start:
.cfi_startproc
.cfi_def_cfa $sp,0
.loc 1 1 0
    la.local $a2,motion_target
.globl motion_before_relax
motion_before_relax:
    .cfi_def_cfa_offset 16
    la.local $a0,motion_target
.globl motion_dot_minus
motion_dot_minus:
    .cfi_def_cfa_offset 24
    .4byte .-motion_start
.globl motion_symbol_minus
motion_symbol_minus:
    .4byte motion_start-.
.globl motion_target
motion_target:
.cfi_def_cfa_offset 32
.loc 1 2 0
    addi.d $a1,$a1,0
.cfi_endproc
.globl motion_end
motion_end:
.section .debug_rnglists,"",@progbits
    .uleb128 motion_target-motion_start
ASM

"$gas" -mabi=lp64d -mrelax -o "$work/gnu.o" "$work/gnu.s"
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/mini.o" "$work/mini.s"

for producer in gnu mini; do
    for mode in relax no-relax; do
        stem="$producer-$mode"
        "$linker" "--$mode" -e motion_start \
            -o "$work/$stem.elf" "$work/$producer.o"
        "$copy" --dump-section ".text=$work/$stem.text" \
            "$work/$stem.elf"
        "$copy" --dump-section ".debug_rnglists=$work/$stem.uleb" \
            "$work/$stem.elf"
        readelf --debug-dump=decodedline "$work/$stem.elf" \
            >"$work/$stem.lines"
        readelf --debug-dump=frames "$work/$stem.elf" \
            >"$work/$stem.frames"
    done
done

python3 - "$work" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

root = Path(sys.argv[1])
bad = []
gnu_bad = []

def symbol(elf, name):
    raw = subprocess.check_output(['readelf', '-Ws', str(elf)], text=True)
    for line in raw.splitlines():
        cols = line.split()
        if len(cols) >= 8 and cols[0].endswith(':') and cols[-1] == name:
            return int(cols[1], 16)
    raise AssertionError(f'missing linked symbol {name}: {elf}')

def section_vma(elf, name):
    raw = subprocess.check_output(['readelf', '-SW', str(elf)], text=True)
    pattern = re.compile(r'^\s*\[\s*\d+\]\s+' + re.escape(name) +
                         r'\s+\S+\s+([0-9a-fA-F]+)\b')
    for line in raw.splitlines():
        match = pattern.match(line)
        if match:
            return int(match.group(1), 16)
    raise AssertionError(f'missing linked {name}: {elf}')

def sle(value, width):
    return value - (1 << width) if value & (1 << (width - 1)) else value

def decode_pcala_pair(text, at, vma):
    offset = at - vma
    assert offset >= 0 and offset + 8 <= len(text), (offset, len(text))
    high = int.from_bytes(text[offset:offset+4], 'little')
    low = int.from_bytes(text[offset+4:offset+8], 'little')
    assert (high & 0xfe000000) == 0x1a000000, ('not pcalau12i', hex(high))
    assert (low & 0xffc00000) == 0x02c00000, ('not addi.d', hex(low))
    hi20 = sle((high >> 5) & 0xfffff, 20)
    lo12 = sle((low >> 10) & 0xfff, 12)
    return (at & ~0xfff) + (hi20 << 12) + lo12

def decode_uleb(blob):
    value = shift = 0
    for byte in blob:
        value |= (byte & 0x7f) << shift
        if not byte & 0x80:
            return value
        shift += 7
        assert shift < 70
    raise AssertionError('truncated ULEB')

def line_pcs(raw):
    result = []
    for line in raw.splitlines():
        match = re.match(r'^\s*\S*loong-link-motion\.S\s+(\d+)\s+(0x[0-9a-fA-F]+)', line)
        if match:
            result.append((int(match.group(1)), int(match.group(2), 16)))
    return result

def cfi_sections(raw):
    # GNU prints each CFI section separately; only compare linked semantics,
    # because the producers may choose different CIEs/opcode widths.
    sections = {}
    current = None
    for line in raw.splitlines():
        section = re.search(r'Contents of the (\.(?:eh|debug)_frame) section', line)
        if section:
            current = section.group(1)
            sections[current] = {'fde': [], 'rows': []}
            continue
        if current is None:
            continue
        fde = re.search(r'\bFDE\b.*\bpc=([0-9a-fA-F]+)\.\.([0-9a-fA-F]+)', line)
        if fde:
            sections[current]['fde'].append((int(fde.group(1), 16),
                                             int(fde.group(2), 16)))
        if 'DW_CFA_advance_loc:' in line:
            pc = re.search(r'\bto\s+(?:0x)?([0-9a-fA-F]+)\b', line)
            if pc:
                sections[current]['rows'].append(int(pc.group(1), 16))
    return sections

for producer in ('gnu', 'mini'):
    distances = {}
    for mode in ('no-relax', 'relax'):
        stem = f'{producer}-{mode}'
        elf = root / f'{stem}.elf'
        text = (root / f'{stem}.text').read_bytes()
        vma = section_vma(elf, '.text')
        begin, before_relax, dot_minus, symbol_minus, target, end = (
            symbol(elf, label) for label in
            ('motion_start', 'motion_before_relax', 'motion_dot_minus',
             'motion_symbol_minus', 'motion_target', 'motion_end'))
        distances[mode] = target - begin
        field_checks = (
            ('dot-minus', dot_minus, (dot_minus - begin) & 0xffffffff),
            ('symbol-minus', symbol_minus, (begin - symbol_minus) & 0xffffffff),
        )
        values = []
        for label, at, expected in field_checks:
            pos = at - vma
            assert pos >= 0 and pos + 4 <= len(text), (stem, at)
            actual = int.from_bytes(text[pos:pos+4], 'little')
            values.append((label, expected, actual))
        expected_uleb = target - begin
        actual_uleb = decode_uleb((root / f'{stem}.uleb').read_bytes())
        rows = line_pcs((root / f'{stem}.lines').read_text())
        lines_correct = (1, begin) in rows and (2, target) in rows
        cfis = cfi_sections((root / f'{stem}.frames').read_text())
        expected_cfi_rows = [before_relax, dot_minus, target]
        for cfi_section in ('.eh_frame', '.debug_frame'):
            cfi = cfis.get(cfi_section, {'fde': [], 'rows': []})
            values.append((f'CFI-FDE-{cfi_section}', [(begin, end)],
                           cfi['fde']))
            values.append((f'CFI-row-PCs-{cfi_section}', expected_cfi_rows,
                           cfi['rows'][:3]))
        if producer == 'mini':
            # This first LA was deliberately emitted under norelax. Its
            # decoded runtime result must follow the later shrinking LA.
            actual_pcala = decode_pcala_pair(text, begin, vma)
            values.append(('fixed-pcala', target, actual_pcala))
        values.append(('ULEB', expected_uleb, actual_uleb))
        values.append(('DWARF-line', True, lines_correct))
        for label, expected, actual in values:
            good = expected == actual
            print('MINIAS_LOONG_LINK_MOTION=CASE '
                  f'producer={producer} mode={mode} field={label} '
                  f'expected={expected} actual={actual} '
                  f'semantic={"OK" if good else "BAD"}')
            if not good:
                (gnu_bad if producer == 'gnu' else bad).append(f'{stem}:{label}')
    shrink = distances['no-relax'] - distances['relax']
    if shrink < 4 or shrink % 4:
        bad.append(f'{producer}:no-real-link-shrink:{shrink}')
        print(f'MINIAS_LOONG_LINK_MOTION=INVALID producer={producer} '
              f'actual_shrink={shrink}')

if gnu_bad:
    print('MINIAS_LOONG_LINK_MOTION=GNU_BASELINE_UNSAFE ' + ','.join(gnu_bad))
if bad:
    print('MINIAS_LOONG_LINK_MOTION=FAIL ' + ','.join(bad), file=sys.stderr)
    sys.exit(1)
print('MINIAS_LOONG_LINK_MOTION=PASS producer=gnu,mini modes=relax,no-relax '
      'semantic_fields=pcala,data2,uleb,dwarf,eh_frame,debug_frame')
PY

# SLEB has no LoongArch/ELF link-time SET/SUB equivalent. A symbolic code
# difference crossing a removable instruction must fail closed: accepting
# an assembly-time constant silently publishes an incorrect linked value.
cat >"$work/negative-sleb.s" <<'ASM'
.text
.option relax
span_begin:
    la.local $a0,span_end
span_end:
    ret
.section .debug_rnglists,"",@progbits
    .sleb128 span_end-span_begin
.option norelax
ASM
if "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/negative-sleb.o" "$work/negative-sleb.s" \
    >"$work/negative-sleb.out" 2>&1; then
    echo 'MINIAS_LOONG_LINK_MOTION=FAIL accepted unrelocatable SLEB difference' >&2
    exit 1
fi
grep -q 'unrepresentable-link-relax-sleb' "$work/negative-sleb.out"
echo 'MINIAS_LOONG_LINK_MOTION=PASS negative_sleb=fail_closed'
