#!/usr/bin/env bash
# An output-script command can own its own ELF output section even when it
# selects no input fragment.  Do not make its section identity depend on a
# synthetic input identity; check bytes, fill, symbols and PT_LOAD mapping.
# Host-only: MiniAS/MiniLD/Python, with GNU as/ld as an independent x86 oracle.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must name minic-as}"
: "${MINILD:?MINILD must name minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_OUTPUT_PLAN_OWNER=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-output-plan-owner.XXXXXX")
finish() {
    if [[ ${MINILD_OUTPUT_PLAN_OWNER_KEEP:-0} == 1 ]]; then
        echo "MINILD_OUTPUT_PLAN_OWNER_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.balign 4
.globl _start
.type _start,@function
_start:
    addi a0,zero,42
    ret
.size _start,.-_start
ASM

cat >"$work/layout.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .header : {
    BYTE(0x11);
    . = . + 0x10;
    BYTE(0x22);
  } = 0x5a
  .text : { *(.text.entry) } = 0
}
LDS

"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/entry.o" "$work/entry.s"

cat >"$work/check.py" <<'PY'
import struct
import sys
from pathlib import Path

label, machine, filename = sys.argv[1:]
machine = int(machine)
data = Path(filename).read_bytes()
if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<HH', data, 16) != (2, machine):
    raise SystemExit(f'{label}: expected little-endian ELF64 ET_EXEC machine={machine}')

shoff = struct.unpack_from('<Q', data, 40)[0]
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', data, 58)
if shentsize != 64 or not 0 < shstrndx < shnum or shoff + 64 * shnum > len(data):
    raise SystemExit(f'{label}: invalid section header table')
sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + 64 * i)
            for i in range(shnum)]

def section_bytes(sh):
    if sh[1] == 8 or sh[4] + sh[5] > len(data):
        raise SystemExit(f'{label}: malformed file-backed section {sh}')
    return data[sh[4]:sh[4] + sh[5]]

def string(pool, offset):
    if offset >= len(pool):
        raise SystemExit(f'{label}: invalid string offset')
    end = pool.find(b'\0', offset)
    if end < 0:
        raise SystemExit(f'{label}: unterminated ELF name')
    return pool[offset:end].decode('ascii')

names = section_bytes(sections[shstrndx])
by_name = {}
for i, sh in enumerate(sections):
    by_name.setdefault(string(names, sh[0]), []).append((i, sh))

def unique(name, kind):
    matches = by_name.get(name, [])
    if len(matches) != 1 or matches[0][1][1] != kind:
        raise SystemExit(f'{label}: expected one {name} type={kind}; got {matches}')
    return matches[0]

header_i, header = unique('.header', 1)
text_i, text = unique('.text', 1)
if '.text.entry' in by_name:
    raise SystemExit(f'{label}: original input section leaked into ELF output')
if not (header[2] & 2 and text[2] & 6 == 6 and
        header[5] == 0x12 and header[3] + header[5] <= text[3]):
    raise SystemExit(f'{label}: wrong section flags, VMA or header span')
expected = b'\x11' + b'\x5a' * 0x10 + b'\x22'
if section_bytes(header) != expected:
    raise SystemExit(f'{label}: pure-script section fill/bytes mismatch '
                     f'got={section_bytes(header).hex()} expected={expected.hex()}')

symtab_i, symtab = unique('.symtab', 2)
strtab_i, strtab = unique('.strtab', 3)
if symtab[6] != strtab_i or symtab[9] != 24 or symtab[5] % 24:
    raise SystemExit(f'{label}: invalid symtab ownership')
strings = section_bytes(strtab)
found = None
for pos in range(symtab[4], symtab[4] + symtab[5], 24):
    name, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', data, pos)
    if name and string(strings, name) == '_start':
        if found is not None:
            raise SystemExit(f'{label}: duplicate _start')
        found = (shndx, value, size)
if found is None or found[0] != text_i or not text[3] <= found[1] < text[3] + text[5]:
    raise SystemExit(f'{label}: _start points at wrong output section: {found}')

phoff = struct.unpack_from('<Q', data, 32)[0]
phentsize, phnum = struct.unpack_from('<HH', data, 54)
if phentsize != 56 or not phnum or phoff + phnum * phentsize > len(data):
    raise SystemExit(f'{label}: invalid program header table')
loads = [struct.unpack_from('<IIQQQQQQ', data, phoff + 56 * i)
         for i in range(phnum)
         if struct.unpack_from('<I', data, phoff + 56 * i)[0] == 1]
if not loads:
    raise SystemExit(f'{label}: missing PT_LOAD')

def require_load(sh):
    for p in loads:
        ptype, flags, fileoff, vaddr, paddr, filesz, memsz, align = p
        if (vaddr <= sh[3] and sh[3] + sh[5] <= vaddr + memsz and
                fileoff <= sh[4] and sh[4] + sh[5] <= fileoff + filesz and
                sh[3] - vaddr == sh[4] - fileoff):
            return
    raise SystemExit(f'{label}: output section not file/VMA mapped by PT_LOAD: {sh}')

require_load(header)
require_load(text)
if not text[4] > header[4] or found[1] != text[3]:
    raise SystemExit(f'{label}: output order or _start address changed')

print(f'MINILD_OUTPUT_PLAN_OWNER_CHECK=PASS case={label} machine={machine}'
      f' header_shndx={header_i} text_shndx={text_i}'
      f' pure_bytes={len(expected)} loads={len(loads)}')
PY

gnu_oracle=INCONCLUSIVE
if command -v as >/dev/null 2>&1 && command -v ld >/dev/null 2>&1; then
    cat >"$work/host.s" <<'ASM'
.section .text.entry,"ax",@progbits
.balign 4
.globl _start
_start:
    nop
    ret
ASM
    as -o "$work/host.o" "$work/host.s"
    sed 's/OUTPUT_ARCH(riscv)/OUTPUT_ARCH(i386:x86-64)/' \
        "$work/layout.lds" >"$work/host.lds"
    ld -m elf_x86_64 -static -T "$work/host.lds" \
        -o "$work/gnu.elf" "$work/host.o"
    python3 "$work/check.py" gnu 62 "$work/gnu.elf"
    gnu_oracle=PASS
fi

# A completed -r pass must not change the meaning of a later script-only
# output command or the real source section which follows it. Also test the
# GC path: synthetic script bytes must survive independently of source GC.
for stage in direct after_r gc; do
    input="$work/entry.o"
    options=()
    if [[ $stage == after_r ]]; then
        "$MINILD" -melf64lriscv -r -o "$work/partial.o" "$input"
        input="$work/partial.o"
    fi
    if [[ $stage == gc ]]; then
        options=(--gc-sections)
    fi
    if ! "$MINILD" -melf64lriscv -static -T "$work/layout.lds" \
        "${options[@]}" -o "$work/$stage.elf" "$input" \
        >"$work/$stage.stdout" 2>"$work/$stage.stderr"; then
        echo "MINILD_OUTPUT_PLAN_OWNER=BLOCKED case=$stage" >&2
        sed -n '1,14p' "$work/$stage.stderr" >&2
        exit 1
    fi
    python3 "$work/check.py" "$stage" 243 "$work/$stage.elf"
done

echo "MINILD_OUTPUT_PLAN_OWNER=PASS stages=direct,after_r,gc gnu_oracle=$gnu_oracle"
