#!/usr/bin/env bash
# Linux creates the .orc_lookup SHT_NOBITS with only `. += length` in ldscript;
# no input section owns any bytes. Compare its ELF metadata to GNU ld.
set -Eeuo pipefail
: "${MINILD:?MINILD must name the linker under test}"
for tool in as ld readelf python3; do
    command -v "$tool" >/dev/null || exit 8
done
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-x86-orc-nobits.XXXXXXXX")
finish() {
    if [[ ${MINILD_SYNTH_NOBITS_KEEP:-0} == 1 ]]; then
        echo "MINILD_SYNTH_NOBITS_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT
as -o "$work/input.o" "$root/tests/linker/script-x86-synthetic-nobits.S"
for ld_tool in ld "$MINILD"; do
    owner=mini
    [[ $ld_tool != ld ]] || owner=gnu
    "$ld_tool" -m elf_x86_64 \
        -T "$root/tests/linker/script-x86-synthetic-nobits.lds" \
        -o "$work/$owner.elf" "$work/input.o"
    "$work/$owner.elf"
    readelf -W -S "$work/$owner.elf" >"$work/$owner.sections"
    readelf -W -l "$work/$owner.elf" >"$work/$owner.phdr"
done
python3 - "$work/gnu.sections" "$work/mini.sections" \
          "$work/gnu.phdr" "$work/mini.phdr" <<'PY'
import re
import sys

def section(path):
    for line in open(path, encoding='utf-8'):
        m = re.search(r'\] \.orc_lookup\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+(\S+)\s+\d+\s+\d+\s+(\d+)', line)
        if m:
            kind, addr, offset, size, flags, alignment = m.groups()
            return (kind, int(addr, 16), int(offset, 16),
                    int(size, 16), flags, int(alignment))
    raise SystemExit('X86_SYNTH_NOBITS=FAIL missing-.orc_lookup')

gnu, mini = (section(path) for path in sys.argv[1:3])
if gnu != mini or gnu[0] != 'NOBITS' or gnu[3] != 0x2000:
    raise SystemExit(f'X86_SYNTH_NOBITS=FAIL section_GNU_mismatch GNU={gnu} Mini={mini}')

def loads(path):
    for line in open(path, encoding='utf-8'):
        if re.match(r'^  LOAD\s', line):
            fields = line.split()
            if int(fields[2], 16) == 0x401000:
                return int(fields[4], 16), int(fields[5], 16)
    raise SystemExit('X86_SYNTH_NOBITS=FAIL missing-data-LOAD')

gnu_load, mini_load = (loads(path) for path in sys.argv[3:])
if gnu_load != mini_load or gnu_load != (0, 0x2000):
    raise SystemExit(f'X86_SYNTH_NOBITS=FAIL LOAD_GNU_mismatch GNU={gnu_load} Mini={mini_load}')
print('X86_SYNTH_NOBITS=PASS GNU_NOBITS_SH_OFF_VMA_SIZE=true fileSz=0 memSz=0x2000')
PY
