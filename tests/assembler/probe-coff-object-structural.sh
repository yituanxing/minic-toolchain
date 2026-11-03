#!/usr/bin/env bash
# Native COFF object structure + target relocation semantics, no Windows host
# needed. Windows link.exe + execution is separately checked by the CI oracle.
set -Eeuo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/entry.s" <<'SRC'
.text
.globl start
start:
    subq $40, %rsp
    call external_helper
    movl %eax, %ecx
    call ExitProcess
.data
.globl helper_address
helper_address:
    .quad external_helper
.bss
.globl uninitialized_buffer
uninitialized_buffer:
    .zero 16
SRC

"$minias" --target=x86_64 --format=coff -o "$work/entry.obj" "$work/entry.s"
if "$minias" --target=riscv --format=coff -o "$work/invalid.obj" "$work/entry.s" >"$work/invalid.log" 2>&1; then
    echo 'MINIAS_COFF_STRUCTURAL=FAIL unsupported target accepted COFF' >&2
    exit 1
fi
if test -f "$work/invalid.obj"; then
    echo 'MINIAS_COFF_STRUCTURAL=FAIL unsupported target published output' >&2
    exit 1
fi

python3 - "$work/entry.obj" <<'PY'
from pathlib import Path
import struct
import sys

image = Path(sys.argv[1]).read_bytes()
assert len(image) >= 20, 'short COFF file header'
machine, section_count, timestamp, symbol_at, symbol_count, optional_size, flags = \
    struct.unpack_from('<HHIIIHH', image)
assert machine == 0x8664, hex(machine)
assert 3 <= section_count <= 16, section_count
assert timestamp == optional_size == flags == 0
assert symbol_at >= 20 + 40 * section_count
assert symbol_count > 0 and symbol_at + 18 * symbol_count + 4 <= len(image)
strings_at = symbol_at + 18 * symbol_count
strings_length, = struct.unpack_from('<I', image, strings_at)
assert 4 <= strings_length <= len(image) - strings_at
strings = image[strings_at:strings_at+strings_length]

def zstring(offset):
    assert 4 <= offset < len(strings), offset
    end = strings.find(b'\0', offset)
    assert end > offset, (offset, strings)
    return strings[offset:end].decode('ascii')

def name(record):
    short = record[:8]
    if short[:4] == b'\0\0\0\0':
        return zstring(struct.unpack_from('<I', short, 4)[0])
    if short.startswith(b'/'):
        return zstring(int(short.split(b'\0', 1)[0][1:]))
    return short.split(b'\0', 1)[0].decode('ascii')

symbols = {}
ordinal = 0
while ordinal < symbol_count:
    record = image[symbol_at+ordinal*18:symbol_at+(ordinal+1)*18]
    sym = name(record)
    value, section, typ, storage, aux = struct.unpack_from('<IhHBB', record, 8)
    symbols[sym] = (ordinal, value, section, typ, storage)
    ordinal += 1 + aux
assert ordinal == symbol_count, 'invalid COFF aux-symbol count'
for sym in ('start', 'helper_address', 'uninitialized_buffer'):
    assert symbols[sym][2] > 0 and symbols[sym][4] == 2, (sym, symbols[sym])
for sym in ('external_helper', 'ExitProcess'):
    assert symbols[sym][2] == 0 and symbols[sym][4] == 2, (sym, symbols[sym])

sections = {}
relocations = []
for index in range(section_count):
    header = image[20+index*40:20+(index+1)*40]
    section = name(header)
    size, raw_at, reloc_at, line_at, reloc_count, line_count, attrs = \
        struct.unpack_from('<IIIIHHI', header, 16)
    if raw_at:
        assert raw_at + size <= symbol_at, (section, size, raw_at)
    if reloc_count:
        assert reloc_at and reloc_at + 10 * reloc_count <= symbol_at
    sections[section] = (size, raw_at, attrs, reloc_count)
    for item in range(reloc_count):
        offset, sym_ordinal, reltype = struct.unpack_from(
            '<IIH', image, reloc_at + item*10)
        assert offset < size and sym_ordinal < symbol_count
        target = next((key for key, entry in symbols.items()
                       if entry[0] == sym_ordinal), None)
        assert target is not None, (section, sym_ordinal)
        relocations.append((section, offset, reltype, target))
        if reltype == 4:
            # ELF64 S+A-P with CALL's A=-4 -> COFF S+0-(P+4).
            assert struct.unpack_from('<i', image, raw_at + offset)[0] == 0, \
                ('bad COFF REL32 in-place addend', section, offset)
        elif reltype == 1:
            assert struct.unpack_from('<Q', image, raw_at + offset)[0] == 0, \
                ('bad COFF ADDR64 in-place addend', section, offset)

assert '.text' in sections and '.data' in sections and '.bss' in sections, sections
assert sections['.text'][2] & 0x20000000, 'text not executable'
assert sections['.data'][2] & 0x80000000, 'data not writable'
assert sections['.bss'][0] == 16 and sections['.bss'][1] == 0, sections['.bss']
expected = [('.text', 4, 'external_helper'),
            ('.text', 4, 'ExitProcess'),
            ('.data', 1, 'external_helper')]
observed = [(sec, reltype, target) for sec, _, reltype, target in relocations]
assert sorted(observed) == sorted(expected), (observed, expected)
print(f'MINIAS_COFF_STRUCTURAL=PASS machine=AMD64 sections={section_count} '
      f'symbol_records={symbol_count} relocations={len(relocations)} '
      'REL32_inplace_A_plus_4=2 ADDR64=1 BSS=16 '
      'undefined_and_defined_symbols=checked')
PY

# GNU BFD independently parses the object we just asserted structurally.
# Its relocation/name decoding catches a self-consistent but invalid custom
# section/symbol layout that a hand-written Python parser might accept.
if command -v objdump >/dev/null 2>&1; then
    objdump -f -r -t "$work/entry.obj" >"$work/gnu-bfd.txt"
    grep -q 'file format pe-x86-64' "$work/gnu-bfd.txt"
    grep -q 'IMAGE_REL_AMD64_REL32' "$work/gnu-bfd.txt"
    grep -q 'IMAGE_REL_AMD64_ADDR64' "$work/gnu-bfd.txt"
    echo 'MINIAS_COFF_GNU_BFD=PASS pe-x86-64 relocation=REL32,ADDR64'
fi

# A real COFF-to-PE link checks the *resolved* relocation, not just whether
# the relocs looked plausible to a header parser. Native execution with the
# MSVC linker is covered separately on windows-latest.
if command -v ld >/dev/null 2>&1 &&
   ld -V 2>&1 | grep -q 'i386pep'; then
    cat >"$work/helper.s" <<'SRC'
.text
.globl external_helper
external_helper:
    movl $42, %eax
    ret
.globl ExitProcess
ExitProcess:
    ret
SRC
    "$minias" --target=x86_64 --format=coff -o "$work/helper.obj" "$work/helper.s"
    ld -m i386pep -e start -o "$work/linked-pe.exe" \
        "$work/entry.obj" "$work/helper.obj"
    python3 - "$work/linked-pe.exe" <<'PY'
from pathlib import Path
import struct
import subprocess
import sys

path = Path(sys.argv[1])
image = path.read_bytes()
assert image[:2] == b'MZ', 'linker did not generate DOS PE envelope'
pe, = struct.unpack_from('<I', image, 0x3c)
assert image[pe:pe+4] == b'PE\0\0', 'missing PE magic'
machine, count = struct.unpack_from('<HH', image, pe+4)
optional_size, = struct.unpack_from('<H', image, pe+20)
optional = pe + 24
assert machine == 0x8664 and struct.unpack_from('<H', image, optional)[0] == 0x20b
base, = struct.unpack_from('<Q', image, optional+24)

sections = {}
for i in range(count):
    at = optional + optional_size + 40*i
    name = image[at:at+8].split(b'\0', 1)[0].decode('ascii')
    virtual_size, virtual_address, raw_size, raw_offset = \
        struct.unpack_from('<IIII', image, at+8)
    sections[name] = (virtual_address, raw_offset, raw_size)
assert '.data' in sections and '.text' in sections, sections

names = subprocess.check_output(['nm', str(path)], text=True).splitlines()
matches = [int(line.split()[0], 16) for line in names
           if line.split()[-1] == 'external_helper']
assert len(matches) == 1, names
callee = matches[0]
data_rva, data_offset, data_size = sections['.data']
assert data_size >= 8
pointer, = struct.unpack_from('<Q', image, data_offset)
assert pointer == callee, ('COFF ADDR64 linked wrong', hex(pointer), hex(callee))

disassembly = subprocess.check_output(['objdump', '-d', str(path)], text=True)
assert any('call' in line and '<external_helper>' in line
           for line in disassembly.splitlines()), 'COFF REL32 linked wrong'
print(f'MINIAS_COFF_GNU_PE_LINK=PASS machine=AMD64 linked_addr={callee:#x} '
      'REL32_call_target=resolved ADDR64_data_pointer=resolved')
PY
fi
