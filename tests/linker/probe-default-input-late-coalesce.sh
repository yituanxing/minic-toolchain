#!/usr/bin/env bash
# Ordinary final ET_EXEC and ET_DYN must keep the original InputSection graph
# through admission, then reproduce the historical name-based final output
# exactly, including alignment, source-symbol and relocation coordinates.
# This is a host ELF test; it does not claim actual RISC-V execution.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_DEFAULT_INPUT=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-default-input.XXXXXX")
finish() {
    if [[ ${MINILD_DEFAULT_INPUT_KEEP:-0} == 1 ]]; then
        echo "MINILD_DEFAULT_INPUT_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat > "$work/first.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
_start:
    .dword first_fn
    .dword second_fn
.section .text.same,"ax",@progbits
.balign 8
.globl first_fn
.type first_fn,@function
first_fn:
    .byte 0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28
    .dword second_fn
.section .data.refs,"aw",@progbits
.balign 8
.globl first_refs
first_refs:
    .dword first_fn
    .dword second_fn
ASM

cat > "$work/second.s" <<'ASM'
.section .text.same,"ax",@progbits
.balign 64
.globl second_fn
.type second_fn,@function
second_fn:
    .byte 0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38
    .dword first_fn
.section .data.refs,"aw",@progbits
.balign 32
.globl second_refs
second_refs:
    .dword second_fn
    .dword first_fn
ASM

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/first.o" "$work/first.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/second.o" "$work/second.s"
"$MINILD" -melf64lriscv -r -o "$work/partial.o" \
    "$work/first.o" "$work/second.o"
for mode in direct after_r; do
    inputs=("$work/first.o" "$work/second.o")
    if [[ $mode == after_r ]]; then
        inputs=("$work/partial.o")
    fi
    "$MINILD" -melf64lriscv -static -e _start \
        -o "$work/$mode.elf" "${inputs[@]}"
done

python3 - "$work/direct.elf" "$work/after_r.elf" <<'PY'
from pathlib import Path
import struct
import sys


def verify(path):
    raw = Path(path).read_bytes()
    if raw[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', raw, 16) != (2, 243):
        raise SystemExit(f'{path}: not an ELF64 RISC-V executable')
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', raw, 58)
    if shentsize != 64 or shstrndx >= shnum or shoff + shentsize * shnum > len(raw):
        raise SystemExit(f'{path}: broken section table')
    headers = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
               for i in range(shnum)]

    def data_of(sec):
        start, size = sec[4], sec[5]
        if start + size > len(raw):
            raise SystemExit(f'{path}: section bytes overflow')
        return raw[start:start + size]

    def name_of(pool, off):
        end = pool.find(b'\0', off)
        if off >= len(pool) or end < 0:
            raise SystemExit(f'{path}: invalid section/symbol string')
        return pool[off:end].decode()

    names = data_of(headers[shstrndx])
    sections = {}
    for i, h in enumerate(headers):
        sections.setdefault(name_of(names, h[0]), []).append((i, h))
    for name in ('.text.same', '.data.refs', '.symtab'):
        if len(sections.get(name, ())) != 1:
            raise SystemExit(f'{path}: expected one coalesced {name}')
    symtab = sections['.symtab'][0][1]
    strtab = headers[symtab[6]]
    symnames = data_of(strtab)
    symbols = {}
    for off in range(0, len(data_of(symtab)), 24):
        name_off, info, other, sec, value, size = struct.unpack_from(
            '<IBBHQQ', data_of(symtab), off)
        if name_off:
            symbols[name_of(symnames, name_off)] = (sec, value)

    def read_at_symbol(name, length):
        sec_index, value = symbols[name]
        sec = headers[sec_index]
        offset = value - sec[3]
        if offset < 0 or offset + length > sec[5]:
            raise SystemExit(f'{path}: {name} outside final section')
        return data_of(sec)[offset:offset + length]

    for name, alignment in (('first_fn', 8), ('second_fn', 64),
                            ('first_refs', 8), ('second_refs', 32)):
        if name not in symbols or symbols[name][1] % alignment:
            raise SystemExit(f'{path}: wrong source identity/alignment {name}')
    first = symbols['first_fn'][1]
    second = symbols['second_fn'][1]
    if read_at_symbol('first_fn', 16) != bytes(range(0x21, 0x29)) + struct.pack('<Q', second):
        raise SystemExit(f'{path}: first source bytes or R_RISCV_64 fixup wrong')
    if read_at_symbol('second_fn', 16) != bytes(range(0x31, 0x39)) + struct.pack('<Q', first):
        raise SystemExit(f'{path}: second source bytes or R_RISCV_64 fixup wrong')
    if read_at_symbol('first_refs', 16) != struct.pack('<QQ', first, second):
        raise SystemExit(f'{path}: first data relocation offsets wrong')
    if read_at_symbol('second_refs', 16) != struct.pack('<QQ', second, first):
        raise SystemExit(f'{path}: second data relocation offsets wrong')
    attrs = sections.get('.riscv.attributes', ())
    if attrs:
        payload = data_of(attrs[0][1])
        if len(attrs) != 1 or not payload.startswith(b'A') or payload[1:2] == b'A':
            raise SystemExit(f'{path}: invalid merged RISC-V attribute marker')
    print(f'MINILD_DEFAULT_INPUT_CHECK=PASS mode={Path(path).stem} '
          'source_sections=5 final_text=1 final_data=1 relocations=8')


for path in sys.argv[1:]:
    verify(path)
PY
echo 'MINILD_DEFAULT_INPUT=PASS modes=direct,after_r'
