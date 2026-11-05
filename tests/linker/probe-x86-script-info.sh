#!/usr/bin/env bash
# GNU output section (INFO) must strip SHF_ALLOC without losing file bytes,
# and exclude the bytes from PT_LOAD even though sh_addr is nonzero.
set -Eeuo pipefail
: "${MINILD:?MINILD must name the exact linker under test}"
for cmd in as ld readelf python3; do
    command -v "$cmd" >/dev/null || exit 8
done
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-x86-info-output.XXXXXXXX")
finish() {
    if [[ ${MINILD_INFO_KEEP:-0} == 1 ]]; then
        echo "MINILD_INFO_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT
as -o "$work/input.o" "$root/tests/linker/script-x86-info-nonempty.S"

for linker in ld "$MINILD"; do
    owner=mini
    [[ $linker != ld ]] || owner=gnu
    "$linker" -m elf_x86_64 \
        -T "$root/tests/linker/script-x86-info-nonempty.lds" \
        -o "$work/$owner.elf" "$work/input.o"
    readelf -W -S "$work/$owner.elf" >"$work/$owner.sections"
    readelf -W -l "$work/$owner.elf" >"$work/$owner.phdrs"
    "$work/$owner.elf"
done

python3 - "$work/gnu.sections" "$work/mini.sections" \
          "$work/gnu.phdrs" "$work/mini.phdrs" <<'PY'
import re
import sys

def section(path):
    for line in open(path, encoding='utf-8'):
        match = re.search(r'\] \.got\.plt\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+(\S+)\s+\d+\s+\d+\s+(\d+)', line)
        if match:
            kind, addr, offset, size, flags, align = match.groups()
            return kind, int(addr, 16), int(offset, 16), int(size, 16), flags, int(align)
    raise SystemExit('X86_SCRIPT_INFO=FAIL no_.got.plt:' + path)

gnu, mini = (section(path) for path in sys.argv[1:3])
if gnu != mini or gnu[4] != 'W' or gnu[5] != 8:
    raise SystemExit(f'X86_SCRIPT_INFO=FAIL section_GNU_mismatch GNU={gnu} Mini={mini}')

def load(path):
    lines = open(path, encoding='utf-8').read().splitlines()
    rows = []
    for line in lines:
        if re.match(r'^  LOAD\s', line):
            fields = line.split()
            rows.append((int(fields[1], 16), int(fields[2], 16),
                         int(fields[4], 16), int(fields[5], 16)))
    return rows, '\n'.join(lines)

gnu_loads, gnu_phdr = load(sys.argv[3])
mini_loads, mini_phdr = load(sys.argv[4])
if gnu_loads != mini_loads or len(mini_loads) != 2 or '.data .got.plt' in mini_phdr:
    raise SystemExit(f'X86_SCRIPT_INFO=FAIL PT_LOAD_GNU_mismatch GNU={gnu_loads} Mini={mini_loads}')
print('X86_SCRIPT_INFO=PASS SHF_ALLOC_removed=true SHF_WRITE_preserved=true INFO_nonload=true GNU_section_and_PHDR_parity=true')
PY
