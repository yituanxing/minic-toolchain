#!/usr/bin/env bash
# The GNU elf64loongarch emulation maps scripted R/RX/R/RW transitions
# sharing one 16 KiB page into ONE RWE PT_LOAD.  A 4 KiB profile silently
# splits the same link into three unrelated PT_LOADs.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-loong-maxpage.XXXXXX")
trap 'rm -r -- "$work"' EXIT

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/input.o" "$repo/tests/linker/fixtures/loongarch-default-maxpage.S"
"$MINILD" -m elf64loongarch -T \
    "$repo/tests/linker/fixtures/loongarch-default-maxpage.lds" \
    -o "$work/output.elf" "$work/input.o"

# GNU's requested page size cannot be weakened below the target ABI default.
if "$MINILD" -m elf64loongarch -z max-page-size=4096 -T \
    "$repo/tests/linker/fixtures/loongarch-default-maxpage.lds" \
    -o "$work/invalid.elf" "$work/input.o" \
    >"$work/invalid.log" 2>&1; then
    echo 'MINILD_LOONG_MAXPAGE=FAIL accepted_undersized_page' >&2
    exit 1
fi
grep -Fq 'invalid-max-page-size:4096' "$work/invalid.log"

python3 - "$work/output.elf" <<'PY'
from pathlib import Path
import struct
import sys

b = Path(sys.argv[1]).read_bytes()
assert b[:6] == b'\x7fELF\x02\x01'
assert struct.unpack_from('<HH', b, 16) == (2, 258)
phoff = struct.unpack_from('<Q', b, 32)[0]
entsize, count = struct.unpack_from('<HH', b, 54)
assert entsize == 56
loads = []
for i in range(count):
    p = struct.unpack_from('<IIQQQQQQ', b, phoff + entsize * i)
    if p[0] == 1:
        loads.append(p)
assert len(loads) == 1, loads
kind, flags, offset, vaddr, paddr, filesz, memsz, align = loads[0]
assert (kind, flags, offset, vaddr, paddr, filesz, memsz, align) == (
    1, 7, 0x4000, 0, 0, 0x3200, 0x3600, 0x4000), loads[0]
shoff = struct.unpack_from('<Q', b, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', b, 58)
headers = [struct.unpack_from('<IIQQQQIIQQ', b, shoff + i * shentsize)
           for i in range(shnum)]
strings = headers[shstrndx]
pool = b[strings[4]:strings[4] + strings[5]]
sections = {pool[s[0]:pool.find(b'\0', s[0])].decode(): s for s in headers}
for name, address, size in (('.head', 0, 0x1000),
                            ('.text', 0x1000, 0x1000),
                            ('.rodata', 0x2000, 0x1000),
                            ('.data', 0x3000, 0x200),
                            ('.bss', 0x3200, 0x400)):
    s = sections[name]
    assert s[3] == address and s[5] == size, (name, s)
    if name != '.bss':
        assert s[4] == 0x4000 + address, (name, s)
print('MINILD_LOONG_MAXPAGE=PASS default_page=16384 '
      'gnu_one_RWE_LOAD=exact negative_4KiB_page=rejected '
      'five_alloc_sections=exact')
PY
