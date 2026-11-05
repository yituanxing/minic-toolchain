#!/usr/bin/env bash
# ET_REL is an output projection of selected source sections, not an excuse to
# destroy their provenance during input admission.  Check ordinary same-name
# coalescing alongside independently owned COMDAT/LINK_ORDER sections across
# two -r hops and a final static link.  Host MiniAS/MiniLD and Python only.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must name minic-as}"
: "${MINILD:?MINILD must name minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_RELOCATABLE_PROJECTION=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-reloc-projection.XXXXXX")
finish() {
    if [[ ${MINILD_RELOCATABLE_PROJECTION_KEEP:-0} == 1 ]]; then
        echo "MINILD_RELOCATABLE_PROJECTION_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap finish EXIT

cat >"$work/first.s" <<'ASM'
.section .text.plain,"ax",@progbits
.balign 8
.globl plain_a
plain_a:
    .byte 0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28
    .dword plain_b
.section .data.refs,"aw",@progbits
.balign 8
.globl ref_a
ref_a:
    .dword plain_a
    .dword sig_a
.section .bss.common,"aw",@nobits
.balign 16
.globl bss_a
bss_a:
    .zero 24
.section .text.group,"axG",@progbits,sig_a,comdat
.globl sig_a
sig_a:
    .word 0x11223344
    ret
.section .meta.group,"aoG",@progbits,sig_a,sig_a,comdat
    .word 0xa1b2c3d4
ASM

cat >"$work/second.s" <<'ASM'
.section .text.plain,"ax",@progbits
.balign 64
.globl plain_b
plain_b:
    .byte 0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38
    .dword plain_a
.section .data.refs,"aw",@progbits
.balign 32
.globl ref_b
ref_b:
    .dword plain_b
    .dword sig_b
.section .bss.common,"aw",@nobits
.balign 128
.globl bss_b
bss_b:
    .zero 40
.section .text.group,"axG",@progbits,sig_b,comdat
.globl sig_b
sig_b:
    .word 0x55667788
    ret
.section .meta.group,"aoG",@progbits,sig_b,sig_b,comdat
    .word 0x10203040
ASM

# The second definition of sig_a must lose the entire COMDAT group,
# including its undefined relocation and link-order metadata.
cat >"$work/loser.s" <<'ASM'
.section .text.group,"axG",@progbits,sig_a,comdat
.globl sig_a
sig_a:
    .word 0xdeadcafe
    .dword discarded_undefined
.section .meta.group,"aoG",@progbits,sig_a,sig_a,comdat
    .word 0xdeadbeef
ASM

cat >"$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    addi a0,zero,0
    addi a7,zero,93
    ecall
.size _start,.-_start
ASM

for name in first second loser entry; do
    "$MINIAS" -march=rv64i -mabi=lp64 \
        -o "$work/$name.o" "$work/$name.s"
done

"$MINILD" -melf64lriscv -r -o "$work/one.o" \
    "$work/first.o" "$work/second.o" "$work/loser.o" "$work/entry.o"
"$MINILD" -melf64lriscv -r -o "$work/two.o" "$work/one.o"
"$MINILD" -melf64lriscv -static -e _start -o "$work/final.elf" \
    "$work/two.o"

python3 - "$work/one.o" "$work/two.o" "$work/final.elf" <<'PY'
import struct
import sys
from pathlib import Path


def ensure(condition, message):
    if not condition:
        raise SystemExit(message)


class Elf:
    def __init__(self, path):
        self.path = Path(path)
        self.raw = self.path.read_bytes()
        d = self.raw
        ensure(d[:6] == b'\x7fELF\x02\x01', f'{path}: not ELF64LE')
        self.kind, self.machine = struct.unpack_from('<HH', d, 16)
        ensure(self.machine == 243, f'{path}: not RISC-V')
        shoff = struct.unpack_from('<Q', d, 40)[0]
        entsize, count, names_index = struct.unpack_from('<HHH', d, 58)
        ensure(entsize == 64 and 0 < names_index < count and
               shoff + count * entsize <= len(d), f'{path}: bad shdr table')
        self.sections = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * 64)
                         for i in range(count)]
        names = self.payload(names_index)
        self.names = [self.cstr(names, s[0]) for s in self.sections]
        self.by_name = {}
        for i, name in enumerate(self.names):
            self.by_name.setdefault(name, []).append(i)
        self.symbols = []
        self.sym_by_name = {}
        for idx in self.by_name.get('.symtab', []):
            h = self.sections[idx]
            ensure(h[6] < count and h[9] == 24 and h[5] % 24 == 0,
                   f'{path}: bad symtab')
            strings = self.payload(h[6])
            for off in range(0, h[5], 24):
                name_off, info, other, shndx, value, size = struct.unpack_from(
                    '<IBBHQQ', d, h[4] + off)
                name = self.cstr(strings, name_off)
                symbol = (name, info, other, shndx, value, size)
                self.symbols.append(symbol)
                if name:
                    self.sym_by_name.setdefault(name, []).append(symbol)
        self.relocations = {}
        for i, h in enumerate(self.sections):
            if h[1] != 4:  # SHT_RELA
                continue
            ensure(h[9] == 24 and h[5] % 24 == 0 and h[7] < count,
                   f'{path}: bad rela section {i}')
            entries = []
            for off in range(0, h[5], 24):
                place, info, addend = struct.unpack_from('<QQq', d, h[4] + off)
                symbol_index = info >> 32
                ensure(symbol_index < len(self.symbols),
                       f'{path}: relocation symbol out of range')
                entries.append((place, info & 0xffffffff,
                                self.symbols[symbol_index][0], addend))
            self.relocations.setdefault(self.names[h[7]], []).extend(entries)

    @staticmethod
    def cstr(pool, off):
        ensure(off < len(pool), 'string offset outside pool')
        end = pool.find(b'\0', off)
        ensure(end != -1, 'unterminated ELF string')
        return pool[off:end].decode('utf-8')

    def payload(self, idx):
        h = self.sections[idx]
        ensure(h[1] != 8 and h[4] + h[5] <= len(self.raw),
               f'{self.path}: invalid section payload {idx}')
        return self.raw[h[4]:h[4] + h[5]]

    def unique(self, name):
        matches = self.by_name.get(name, [])
        ensure(len(matches) == 1,
               f'{self.path}: expected one {name}, got {matches}')
        return matches[0]

    def symbol(self, name):
        matches = [s for s in self.sym_by_name.get(name, [])
                   if s[3] not in (0, 0xfff2)]
        ensure(len(matches) == 1,
               f'{self.path}: expected one defined {name}, got {matches}')
        return matches[0]

    def at_symbol(self, name, count):
        sym = self.symbol(name)
        ensure(sym[3] < len(self.sections), f'{self.path}: {name} shndx bad')
        header = self.sections[sym[3]]
        rel = sym[4] - header[3] if self.kind == 2 else sym[4]
        ensure(0 <= rel <= header[5] and count <= header[5] - rel,
               f'{self.path}: {name} outside section')
        return self.payload(sym[3])[rel:rel + count]


def verify_partial(elf):
    ensure(elf.kind == 1, f'{elf.path}: not ET_REL')
    text = elf.unique('.text.plain')
    data = elf.unique('.data.refs')
    bss = elf.unique('.bss.common')
    attributes = elf.unique('.riscv.attributes')
    ensure(elf.sections[attributes][1] == 0x70000003 and
           elf.payload(attributes).startswith(b'A') and
           elf.payload(attributes)[1:2] != b'A',
           f'{elf.path}: RISC-V vendor attributes were not merged once')
    ensure(elf.sections[text][8] == 64 and elf.sections[data][8] == 32,
           f'{elf.path}: input alignment lost')
    ensure(elf.sections[bss][1] == 8 and elf.sections[bss][8] == 128 and
           elf.sections[bss][5] == 168,
           f'{elf.path}: NOBITS alignment/type/virtual extent lost')
    ensure(elf.sections[text][5] == 80 and elf.sections[data][5] == 48,
           f'{elf.path}: unexpected same-name coalescing/padding')
    for name, section, value in (('plain_a', text, 0), ('plain_b', text, 64),
                                 ('ref_a', data, 0), ('ref_b', data, 32),
                                 ('bss_a', bss, 0), ('bss_b', bss, 128)):
        sym = elf.symbol(name)
        ensure(sym[3] == section and sym[4] == value,
               f'{elf.path}: {name} section/source offset changed')
    for name, offsets in (('.text.plain', {8, 72}),
                          ('.data.refs', {0, 8, 32, 40})):
        entries = elf.relocations.get(name, [])
        ensure({e[0] for e in entries if e[1] == 2} == offsets and
               len(entries) == len(offsets),
               f'{elf.path}: wrong {name} relocation offsets/types: {entries}')
    expected_references = {
        '.text.plain': {(8, 'plain_b'), (72, 'plain_a')},
        '.data.refs': {(0, 'plain_a'), (8, 'sig_a'),
                       (32, 'plain_b'), (40, 'sig_b')},
    }
    for name, expected in expected_references.items():
        actual = {(e[0], e[2]) for e in elf.relocations[name]}
        ensure(actual == expected, f'{elf.path}: {name} symbol mapping {actual}')

    groups = {}
    for i, header in enumerate(elf.sections):
        if header[1] != 17:  # SHT_GROUP
            continue
        ensure(header[7] < len(elf.symbols), f'{elf.path}: group signature OOB')
        signature = elf.symbols[header[7]][0]
        words = struct.unpack('<' + 'I' * (header[5] // 4), elf.payload(i))
        ensure(words and words[0] == 1,
               f'{elf.path}: group {signature} not COMDAT')
        groups[signature] = set(words[1:])
    ensure(set(groups) == {'sig_a', 'sig_b'},
           f'{elf.path}: COMDAT winner/loser wrong: {groups}')
    expected_metadata = {'sig_a': b'\xd4\xc3\xb2\xa1',
                         'sig_b': b'\x40\x30\x20\x10'}
    for signature, marker in expected_metadata.items():
        target = elf.symbol(signature)[3]
        linked = [i for i in groups[signature]
                  if elf.names[i] == '.meta.group']
        ensure(target in groups[signature] and len(linked) == 1,
               f'{elf.path}: incomplete COMDAT {signature}')
        meta = elf.sections[linked[0]]
        ensure(meta[2] & 0x280 == 0x280 and meta[6] == target and
               elf.payload(linked[0]) == marker,
               f'{elf.path}: {signature} LINK_ORDER/group identity lost')
    ensure(elf.symbol('sig_a')[3] != elf.symbol('sig_b')[3],
           f'{elf.path}: different COMDATs merged same-named targets')
    ensure(not any(e[2] == 'discarded_undefined'
                   for entries in elf.relocations.values() for e in entries),
           f'{elf.path}: losing COMDAT leaked a live relocation')
    return ([(e[0], e[1], e[2], e[3]) for e in elf.relocations['.text.plain']],
            [(e[0], e[1], e[2], e[3]) for e in elf.relocations['.data.refs']],
            elf.payload(text), elf.payload(data), elf.payload(attributes),
            elf.sections[bss][5])


def verify_final(elf):
    ensure(elf.kind == 2, f'{elf.path}: not ET_EXEC')
    a_bss = elf.symbol('bss_a')
    b_bss = elf.symbol('bss_b')
    ensure(a_bss[3] == b_bss[3] and
           elf.sections[a_bss[3]][1] == 8 and
           elf.sections[a_bss[3]][8] >= 128 and
           b_bss[4] - a_bss[4] == 128,
           f'{elf.path}: NOBITS virtual positions/type lost')
    phoff = struct.unpack_from('<Q', elf.raw, 32)[0]
    phentsize, phcount = struct.unpack_from('<HH', elf.raw, 54)
    ensure(phentsize == 56 and phoff + phentsize * phcount <= len(elf.raw),
           f'{elf.path}: invalid program headers')
    allocated_end = b_bss[4] + 40
    loads = [struct.unpack_from('<IIQQQQQQ', elf.raw, phoff + i * phentsize)
             for i in range(phcount)]
    ensure(any(p[0] == 1 and p[3] <= a_bss[4] and
               p[3] + p[6] >= allocated_end and
               p[3] + p[5] <= a_bss[4] and p[6] > p[5]
               for p in loads),
           f'{elf.path}: NOBITS was file-backed or absent from LOAD memsz')
    for name, marker in (('sig_a', b'\x44\x33\x22\x11'),
                         ('sig_b', b'\x88\x77\x66\x55')):
        ensure(elf.at_symbol(name, 4) == marker,
               f'{elf.path}: wrong live COMDAT {name} bytes')
    ensure(b'\xfe\xca\xad\xde' not in elf.raw and
           b'\xef\xbe\xad\xde' not in elf.raw,
           f'{elf.path}: losing COMDAT bytes leaked')
    a = elf.symbol('plain_a')[4]
    b = elf.symbol('plain_b')[4]
    ga = elf.symbol('sig_a')[4]
    gb = elf.symbol('sig_b')[4]
    ensure(b - a == 64, f'{elf.path}: plain section alignment changed')
    ensure(elf.at_symbol('plain_a', 16) ==
           bytes(range(0x21, 0x29)) + struct.pack('<Q', b),
           f'{elf.path}: plain_a bytes/relocation wrong')
    ensure(elf.at_symbol('plain_b', 16) ==
           bytes(range(0x31, 0x39)) + struct.pack('<Q', a),
           f'{elf.path}: plain_b bytes/relocation wrong')
    ensure(elf.at_symbol('ref_a', 16) == struct.pack('<QQ', a, ga),
           f'{elf.path}: ref_a fixups wrong')
    ensure(elf.at_symbol('ref_b', 16) == struct.pack('<QQ', b, gb),
           f'{elf.path}: ref_b fixups wrong')


first, second, final = map(Elf, sys.argv[1:])
first_relocations = verify_partial(first)
second_relocations = verify_partial(second)
ensure(first_relocations == second_relocations,
       'repeated -r altered canonical relocations/addends')
verify_final(final)
print('MINILD_RELOCATABLE_PROJECTION=PASS stages=two_ET_REL,ET_EXEC '
      'COMDAT=2 LINK_ORDER=2 source_alignment=8,32,64,128 NOBITS=168 '
      'R_RISCV_64=6 repeated_relocations=stable')
PY
