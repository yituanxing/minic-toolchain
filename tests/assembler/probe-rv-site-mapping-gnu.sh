#!/usr/bin/env bash
# Opt-in *diagnostic* GNU oracle for RISC-V lexical ISA mapping transitions.
# It is not a mandatory PASS gate until MiniAS implements all measured GNU
# $x<effective ISA> / $d transitions. A known mapping-only discrepancy is
# reported as EXPECTED_GAP, never disguised as GNU semantic parity.
set -Eeuo pipefail
: "${MINIAS:?point MINIAS at the exact candidate minic-as executable}"

for program in riscv64-linux-gnu-as riscv64-linux-gnu-objcopy readelf cmp python3; do
    if ! command -v "$program" >/dev/null 2>&1; then
        echo "MINIAS_RV_SITE_MAPPING=SKIP missing=$program"
        exit 0
    fi
done

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
fixture="$repo/tests/assembler/fixtures/rv-site-mapping-transition-gnu.S"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

for xlen in 32 64; do
    if test "$xlen" = 32; then mabi=ilp32; else mabi=lp64; fi
    # GNU GAS needs explicit I2p1: unversioned rv*i implies legacy I2p0
    # and would admit CSR outside the scoped +v region. MiniAS currently
    # accepts its unversioned rv*i spelling and selects I2p1 internally.
    gnu_march="rv${xlen}i2p1"
    mini_march="rv${xlen}i"
    source="$work/$xlen.S"
    printf '.attribute arch,"rv%si2p1"\n' "$xlen" >"$source"
    cat "$fixture" >>"$source"

    riscv64-linux-gnu-as "-march=$gnu_march" "-mabi=$mabi" -mno-relax \
        -o "$work/$xlen.gnu.o" "$source"
    "$MINIAS" "-march=$mini_march" "-mabi=$mabi" \
        -o "$work/$xlen.mini.o" "$source"

    for section in .text .text.mapping_extra .riscv.attributes; do
        suffix="${section#.}"
        for producer in gnu mini; do
            riscv64-linux-gnu-objcopy -O binary -j "$section" \
                "$work/$xlen.$producer.o" \
                "$work/$xlen.$producer.$suffix"
        done
        if ! cmp -s "$work/$xlen.gnu.$suffix" \
                    "$work/$xlen.mini.$suffix"; then
            echo "MINIAS_RV_SITE_MAPPING=FAIL xlen=$xlen section=$section bytes_or_file_attribute" >&2
            exit 1
        fi
    done

    # Preserve the original symtab sequence: sorting by offset would erase
    # observable same-offset transition order. Report both numeric Section
    # IDs and Section names; do not filter absent/extra mapping symbols.
    python3 - "$work/$xlen.gnu.o" "$work/$xlen.mini.o" "$xlen" <<'PY'
import re
import subprocess
import sys

gnu, mini, xlen = sys.argv[1:]

def section_names(path):
    output = subprocess.check_output(['readelf', '-SW', path], text=True)
    names = {}
    for row in output.splitlines():
        match = re.match(r'^\s*\[\s*(\d+)\]\s+(\S+)', row)
        if match:
            names[match.group(1)] = match.group(2)
    return names

def mapping(path):
    sections = section_names(path)
    output = subprocess.check_output(['readelf', '-sW', path], text=True)
    symbols = []
    for row in output.splitlines():
        columns = row.split()
        if len(columns) < 8 or not re.fullmatch(r'\d+:', columns[0]):
            continue
        name = columns[7]
        # GAS/object writers may add a private disambiguation suffix when
        # several mapping symbols share their base name in one object.
        if (name in ('$d', '$x') or name.startswith(('$d.', '$x.', '$xrv'))):
            section = columns[6]
            symbols.append((section, sections.get(section, '?'),
                            int(columns[1], 16), name))
    return symbols

gnu_map = mapping(gnu)
mini_map = mapping(mini)
if not gnu_map:
    raise SystemExit(f'MINIAS_RV_SITE_MAPPING=FAIL xlen={xlen} GNU_no_mapping_symbols')
if gnu_map == mini_map:
    print(f'MINIAS_RV_SITE_MAPPING=PASS xlen={xlen} '
          f'mapping_symbols={len(gnu_map)} ordered_section_offset_name=exact')
else:
    print(f'MINIAS_RV_SITE_MAPPING=EXPECTED_GAP xlen={xlen} '
          f'GNU={len(gnu_map)} MiniAS={len(mini_map)} '
          'ordered_section_offset_name=DIFF')
    for producer, rows in [('GNU', gnu_map), ('MiniAS', mini_map)]:
        for ordinal, (section_id, section_name, offset, name) in enumerate(rows):
            print(f'  {producer} ordinal={ordinal} '
                  f'section={section_id}:{section_name} '
                  f'offset={offset} name={name}')
PY
done

echo 'MINIAS_RV_SITE_MAPPING_ORACLE=DIAGNOSTIC_ONLY RV32_RV64=2 byte_sections=6 mapping_gap_not_suppressed=1'
