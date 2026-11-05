#!/usr/bin/env bash
# Exact GNU 2.42 oracle for RISC-V lexical ISA mapping transitions. Compare
# original symbol-table order, names, Section IDs and final offsets, not just
# bytes or unordered mapping-symbol counts. Missing GNU tools are visible SKIP.
set -Eeuo pipefail
: "${MINIAS:?point MINIAS at the exact candidate minic-as executable}"

for program in riscv64-linux-gnu-as riscv64-linux-gnu-objcopy readelf cmp python3; do
    if ! command -v "$program" >/dev/null 2>&1; then
        echo "MINIAS_RV_SITE_MAPPING=SKIP missing=$program"
        exit 0
    fi
done

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

for scenario in transition rvc alignment profile profile_g profile_v; do
  fixture="$repo/tests/assembler/fixtures/rv-site-mapping-${scenario}-gnu.S"
  if test "$scenario" = profile || test "$scenario" = profile_g || test "$scenario" = profile_v; then
      fixture="$repo/tests/assembler/fixtures/rv-site-mapping-alignment-gnu.S"
  fi
  sections='.text .riscv.attributes'
  test "$scenario" = transition && sections="$sections .text.mapping_extra"
  for xlen in 32 64; do
    if test "$xlen" = 32; then mabi=ilp32; else mabi=lp64; fi
    # GNU GAS needs explicit I2p1: unversioned rv*i implies legacy I2p0
    # and would admit CSR outside the scoped +v region. MiniAS currently
    # accepts its unversioned rv*i spelling and selects I2p1 internally.
    gnu_march="rv${xlen}i2p1"
    mini_march="rv${xlen}i"
    if test "$scenario" = rvc; then
        gnu_march="${gnu_march}_c2p0"
        mini_march="${mini_march}c"
    elif test "$scenario" = profile; then
        gnu_march="${gnu_march}_m2p0_zbb1p0"
        mini_march="${mini_march}m_zbb"
    elif test "$scenario" = profile_g; then
        gnu_march="${gnu_march}_m2p0_a2p1_f2p2_d2p2_zicsr2p0_zifencei2p0"
        mini_march="rv${xlen}g"
    elif test "$scenario" = profile_v; then
        gnu_march="${gnu_march}_v1p0"
        mini_march="${mini_march}v"
    fi
    source="$work/$scenario-$xlen.S"
    printf '.attribute arch,"%s"\n' "$gnu_march" >"$source"
    cat "$fixture" >>"$source"

    riscv64-linux-gnu-as "-march=$gnu_march" "-mabi=$mabi" -mno-relax \
        -o "$work/$scenario-$xlen.gnu.o" "$source"
    "$MINIAS" "-march=$mini_march" "-mabi=$mabi" \
        -o "$work/$scenario-$xlen.mini.o" "$source"

    for section in $sections; do
        suffix="${section#.}"
        for producer in gnu mini; do
            riscv64-linux-gnu-objcopy -O binary -j "$section" \
                "$work/$scenario-$xlen.$producer.o" \
                "$work/$scenario-$xlen.$producer.$suffix"
        done
        if ! cmp -s "$work/$scenario-$xlen.gnu.$suffix" \
                    "$work/$scenario-$xlen.mini.$suffix"; then
            echo "MINIAS_RV_SITE_MAPPING=FAIL scenario=$scenario xlen=$xlen section=$section bytes_or_file_attribute" >&2
            exit 1
        fi
    done

    # Preserve the original symtab sequence: sorting by offset would erase
    # observable same-offset transition order. Report both numeric Section
    # IDs and Section names; do not filter absent/extra mapping symbols.
    python3 - "$work/$scenario-$xlen.gnu.o" \
        "$work/$scenario-$xlen.mini.o" "$xlen" "$scenario" <<'PY'
import re
import subprocess
import sys

gnu, mini, xlen, scenario = sys.argv[1:]

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
    print(f'MINIAS_RV_SITE_MAPPING=PASS scenario={scenario} xlen={xlen} '
          f'mapping_symbols={len(gnu_map)} ordered_section_offset_name=exact')
else:
    print(f'MINIAS_RV_SITE_MAPPING=FAIL scenario={scenario} xlen={xlen} '
          f'GNU={len(gnu_map)} MiniAS={len(mini_map)} '
          'ordered_section_offset_name=DIFF')
    for producer, rows in [('GNU', gnu_map), ('MiniAS', mini_map)]:
        for ordinal, (section_id, section_name, offset, name) in enumerate(rows):
            print(f'  {producer} ordinal={ordinal} '
                  f'section={section_id}:{section_name} '
                  f'offset={offset} name={name}')
    raise SystemExit(1)
PY
  done
done

echo 'MINIAS_RV_SITE_MAPPING_ORACLE=PASS cases=12 RV32_RV64=2 exact_order=1 zero_width=covered'
