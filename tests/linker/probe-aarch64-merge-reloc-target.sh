#!/usr/bin/env bash
# SHF_MERGE coordinates are still ordinary S+A for AArch64 ADRP/ADD/PREL32.
# The instruction's page rounding and bit-field encoding happen afterwards.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-aarch64-merge-reloc.XXXXXX")
trap 'rm -r -- "$work"' EXIT

for unit in a b; do
    "$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
        -o "$work/$unit.o" \
        "$repo/tests/linker/fixtures/aarch64-merge-reloc-$unit.S"
done
readelf -Wr "$work/a.o" >"$work/a.relocs"
grep -Fq R_AARCH64_ADR_PREL_PG_HI21 "$work/a.relocs"
grep -Fq R_AARCH64_ADD_ABS_LO12_NC "$work/a.relocs"
grep -Fq R_AARCH64_PREL32 "$work/a.relocs"

for mode in static kernel; do
    args=(-m aarch64linux -T "$repo/tests/linker/fixtures/aarch64-merge-reloc.lds")
    if [[ $mode == static ]]; then
        args+=(-static)
    else
        args+=(-shared -Bsymbolic --no-apply-dynamic-relocs -z notext)
    fi
    "$MINILD" "${args[@]}" -o "$work/$mode.elf" \
        "$work/a.o" "$work/b.o" >"$work/$mode.log" 2>&1
done

python3 - "$work/static.elf" "$work/kernel.elf" <<'PY'
from pathlib import Path
import struct
import sys

def inspect(path, elf_type):
    raw = Path(path).read_bytes()
    assert struct.unpack_from('<HH', raw, 16) == (elf_type, 183)
    off = struct.unpack_from('<Q', raw, 40)[0]
    count, names_index = struct.unpack_from('<HH', raw, 60)
    headers = [struct.unpack_from('<IIQQQQIIQQ', raw, off + i*64)
               for i in range(count)]
    names = headers[names_index]
    pool = raw[names[4]:names[4] + names[5]]
    byname = {pool[s[0]:pool.index(0, s[0])].decode(): s for s in headers}
    text, rodata, data = (byname[name] for name in ('.text', '.rodata', '.data'))
    payload = b'shared-AArch64-merge-string\0'
    assert raw[rodata[4]:rodata[4] + rodata[5]] == payload
    sym = byname['.symtab']
    strings = headers[sym[6]]
    string_pool = raw[strings[4]:strings[4] + strings[5]]
    symbols = {}
    for i in range(sym[4], sym[4] + sym[5], 24):
        start, info, other, section, value, size = struct.unpack_from(
            '<IBBHQQ', raw, i)
        if start:
            name = string_pool[start:string_pool.index(0, start)].decode()
            symbols[name] = value
    assert symbols['a_string'] == symbols['b_string'] == rodata[3]
    assert symbols['relative_a'] == data[3]
    assert symbols['relative_b'] == data[3] + 4
    assert struct.unpack_from('<ii', raw, data[4]) == (
        rodata[3] - data[3], rodata[3] - data[3] - 4)
    words = struct.unpack_from('<4I', raw, text[4])
    # Two instructions name different registers, but resolve one folded
    # literal address. Ignore ADRP Rd and ADD Rd/Rn register fields.
    assert (words[0] & ~31) == (words[2] & ~31)
    assert (words[1] & ~0x3ff) == (words[3] & ~0x3ff)
    assert ((words[1] >> 10) & 0xfff) == (rodata[3] & 0xfff)
    return (rodata[3], payload, tuple(words))

static = inspect(sys.argv[1], 2)
kernel = inspect(sys.argv[2], 3)
assert static == kernel, ('static/kernel target relocation mismatch', static, kernel)
print('MINILD_AARCH64_MERGE_RELOC=PASS shared_str1_8=folded '
      'ADRP_ADD_PREL32=remapped static_ET_EXEC=1 kernel_ET_DYN=1')
PY
