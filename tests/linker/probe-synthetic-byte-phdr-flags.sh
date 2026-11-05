#!/usr/bin/env bash
# GNU output-section SHF_WRITE semantics are independent of PT_LOAD p_flags.
# The PHDR may be RWX or RW while a synthetic BYTE-only output remains
# alloc-only, even following an ordinary writable input section.
set -Eeuo pipefail
: "${MINILD:?MINILD must name the linker under test}"
for tool in as ld readelf python3; do
    command -v "$tool" >/dev/null || exit 8
done
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-synthetic-byte-flags.XXXXXXXX")
finish() {
    if [[ ${MINILD_SYNTH_BYTE_KEEP:-0} == 1 ]]; then
        echo "MINILD_SYNTH_BYTE_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT
as -o "$work/input.o" "$root/tests/linker/script-synthetic-byte-phdr-flags.S"
for scenario in rwx data; do
    script="$root/tests/linker/script-synthetic-byte-phdr-flags.lds"
    if [[ $scenario == data ]]; then
        script="$root/tests/linker/script-synthetic-byte-data-phdr-flags.lds"
    fi
    for ld_tool in ld "$MINILD"; do
        owner=mini
        [[ $ld_tool != ld ]] || owner=gnu
        "$ld_tool" -m elf_x86_64 \
            -T "$script" \
            -o "$work/$scenario.$owner.elf" "$work/input.o"
        readelf -W -S "$work/$scenario.$owner.elf" \
            >"$work/$scenario.$owner.sections"
        readelf -W -l "$work/$scenario.$owner.elf" \
            >"$work/$scenario.$owner.phdr"
    done
    python3 - "$work/$scenario.gnu.sections" "$work/$scenario.mini.sections" \
              "$work/$scenario.gnu.phdr" "$work/$scenario.mini.phdr" \
              "$scenario" <<'PY'
import re
import sys

def sections(path):
    found = {}
    for line in open(path, encoding='utf-8'):
        m = re.search(
            r'\] (\.text|\.data|\.edata_padding)\s+(\S+)\s+'
            r'([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+'
            r'[0-9a-f]+\s+(\S+)\s+\d+\s+\d+\s+(\d+)',
            line)
        if m:
            name, typ, vma, offset, size, flags, align = m.groups()
            found[name] = (typ, int(vma, 16), int(offset, 16),
                           int(size, 16), flags, int(align))
    return found

gnu, mini = (sections(path) for path in sys.argv[1:3])
for name in ('.text', '.data', '.edata_padding'):
    if name not in gnu or name not in mini:
        raise SystemExit(f'SYNTH_BYTE_PHDR=FAIL missing-section:{name}')
    if gnu[name] != mini[name]:
        raise SystemExit(f'SYNTH_BYTE_PHDR=FAIL GNU-section-differential:'
                         f'{name}:GNU={gnu[name]}:Mini={mini[name]}')
if gnu['.data'][4] != 'WA' or gnu['.edata_padding'][4] != 'A' or \
        gnu['.edata_padding'][0] != 'PROGBITS':
    raise SystemExit('SYNTH_BYTE_PHDR=FAIL unexpected-GNU-flags:'
                     f'data={gnu[".data"]}:pad={gnu[".edata_padding"]}')

def loads(path):
    return [line.split()[1:] for line in open(path, encoding='utf-8')
            if re.match(r'^  LOAD\s+', line)]

gnu_load, mini_load = (loads(path) for path in sys.argv[3:5])
if gnu_load != mini_load:
    raise SystemExit(f'SYNTH_BYTE_PHDR=FAIL PHDR-differential:'
                     f'GNU={gnu_load}:Mini={mini_load}')
scenario = sys.argv[5]
expected = 'RWE' if scenario == 'rwx' else 'RW'
if not gnu_load or not any(expected in ' '.join(row) for row in gnu_load):
    raise SystemExit(f'SYNTH_BYTE_PHDR=FAIL missing-{expected}-PHDR:{gnu_load}')
print(f'SYNTH_BYTE_PHDR=PASS scenario={scenario} '
      'synthetic_PROGBITS_flags=A data_flags=WA '
      'GNU_section_and_segment_metadata_equal=true')
PY
done
