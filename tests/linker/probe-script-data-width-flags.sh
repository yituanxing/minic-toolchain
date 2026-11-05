#!/usr/bin/env bash
# GNU BFD is the oracle for script-only ELF32 PROGBITS sections:
# BYTE/SHORT/LONG/QUAD widths, little-endian truncation, and the order of
# preceding symbol assignments versus the first content-producing directive.
set -Eeuo pipefail
: "${MINILD:?MINILD must name the linker under test}"
for tool in as ld python3; do
    command -v "$tool" >/dev/null || exit 8
done
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-data-width.XXXXXXXX")
finish() {
    if [[ ${MINILD_SCRIPT_DATA_KEEP:-0} == 1 ]]; then
        echo "MINILD_SCRIPT_DATA_ARTIFACTS=$work"
    else
        rm -r -- "$work"
    fi
}
trap finish EXIT

as --32 -o "$work/input.o" \
    "$root/tests/linker/script-synthetic-data-inherit.S"
script="$root/tests/linker/script-synthetic-data-inherit.lds"
ld -m elf_i386 -T "$script" -o "$work/gnu.elf" "$work/input.o"
# GNU Kbuild can nest an outer -m elf_x86_64 and later -m elf_i386;
# the final emulation controls the ELF class, as with GNU ld.
"$MINILD" -m elf_x86_64 -m elf_i386 -T "$script" \
    -o "$work/mini.elf" "$work/input.o"

python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY'
import struct
import sys

EXPECTED_WRITABLE = {
    '.byte_preassign', '.byte_absassign', '.byte_constassign',
    '.long_after_data',
}
SYNTHETIC = {
    '.byte_after_text', '.short_after_text', '.long_after_text',
    '.quad_after_text', '.byte_after_data', '.byte_preassign',
    '.byte_postassign', '.byte_absassign', '.byte_constassign',
    '.short_after_data', '.long_after_data', '.quad_after_data',
}
EXPECTED_DATA = {
    '.byte_after_text': bytes.fromhex('34'),
    '.short_after_text': bytes.fromhex('5634'),
    '.long_after_text': bytes.fromhex('9a785634'),
    '.quad_after_text': bytes.fromhex('ffeeddccbbaa9988'),
}

def sections(path):
    raw = open(path, 'rb').read()
    if raw[:6] != b'\x7fELF\x01\x01':
        raise SystemExit(f'SCRIPT_DATA=FAIL expected-ELF32-LE:{path}')
    hdr = struct.unpack_from('<HHIIIIIHHHHHH', raw, 16)
    if hdr[1] != 3:
        raise SystemExit(f'SCRIPT_DATA=FAIL expected-EM_386:{path}')
    shoff, shentsize, shnum, names_index = hdr[5], hdr[10], hdr[11], hdr[12]
    if shentsize != 40 or shnum <= names_index:
        raise SystemExit(f'SCRIPT_DATA=FAIL invalid-ELF32-sections:{path}')
    sh = [struct.unpack_from('<IIIIIIIIII', raw, shoff + i * shentsize)
          for i in range(shnum)]
    names = raw[sh[names_index][4]:sh[names_index][4] + sh[names_index][5]]
    result = {}
    for section in sh:
        name_start = section[0]
        if name_start >= len(names):
            raise SystemExit(f'SCRIPT_DATA=FAIL corrupt-section-name:{path}')
        name_end = names.find(b'\0', name_start)
        if name_end == -1:
            raise SystemExit(f'SCRIPT_DATA=FAIL unterminated-section-name:{path}')
        name = names[name_start:name_end].decode('ascii')
        typ, flags, address, offset, length = section[1:6]
        data = raw[offset:offset + length] if typ != 8 else b''
        result[name] = (typ, flags, address, data)
    return result

gnu, mini = (sections(path) for path in sys.argv[1:])
for name in sorted(SYNTHETIC | {'.text', '.data'}):
    if name not in gnu or name not in mini:
        raise SystemExit(f'SCRIPT_DATA=FAIL missing-section:{name}')
    if gnu[name] != mini[name]:
        raise SystemExit(f'SCRIPT_DATA=FAIL GNU-difference:{name}:'
                         f'GNU={gnu[name]}:Mini={mini[name]}')
    if name not in SYNTHETIC:
        continue
    typ, flags, _, data = gnu[name]
    if typ != 1 or flags != (3 if name in EXPECTED_WRITABLE else 2):
        raise SystemExit(f'SCRIPT_DATA=FAIL unexpected-GNU-flags:{name}:'
                         f'type={typ}:flags={flags}')
    if name in EXPECTED_DATA and data != EXPECTED_DATA[name]:
        raise SystemExit(f'SCRIPT_DATA=FAIL incorrect-GNU-width-or-endian:'
                         f'{name}:{data.hex()}')
print('MINILD_SCRIPT_DATA_WIDTH=PASS ELF32_i386=true '
      'GNU_BFD_sections_and_bytes=exact '
      'widths=BYTE,SHORT,LONG,QUAD signed_truncation=exact '
      'assignment_order_SHF_WRITE=exact')
PY
