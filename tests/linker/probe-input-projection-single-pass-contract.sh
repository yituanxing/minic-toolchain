#!/usr/bin/env bash
# Black-box contract for projecting ELF input sections into linker work
# sections.  It must pass with either eager merge/materialize or a single
# projection pass: no implementation-specific phase names or logs are used.
# The same source objects exercise -r -> -r, default/static, script/GC and
# shared links; no RISC-V GNU tools or QEMU are required.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_INPUT_PROJECTION=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-single-projection.XXXXXX")
cleanup() {
    if [[ ${MINILD_INPUT_PROJECTION_KEEP:-0} == 1 ]]; then
        echo "MINILD_INPUT_PROJECTION_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

cat >"$work/alpha.s" <<'ASM'
.section .text.same,"ax",@progbits
.balign 8
.globl alpha_fn
alpha_fn:
    .byte 0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8
    .dword beta_fn
.section .bss.same,"aw",@nobits
.balign 16
.globl alpha_bss
alpha_bss:
    .zero 9
.section .meta.order,"o",@progbits,alpha_fn
    .word 0x11223344
.section .debug.source,"",@progbits
    .byte 0xa1,0xa2,0xa3,0xa4
.section .text.group,"axG",@progbits,sig_alpha,comdat
.globl sig_alpha
sig_alpha:
    .word 0x0a0b0c0d
    ret
.section .meta.group,"aoG",@progbits,sig_alpha,sig_alpha,comdat
    .word 0x1a1b1c1d
ASM

cat >"$work/beta.s" <<'ASM'
.section .text.same,"ax",@progbits
.balign 32
.globl beta_fn
beta_fn:
    .byte 0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8
    .dword alpha_fn
.section .bss.same,"aw",@nobits
.balign 64
.globl beta_bss
beta_bss:
    .zero 13
.section .meta.order,"o",@progbits,beta_fn
    .word 0x55667788
.section .debug.source,"",@progbits
    .byte 0xb1,0xb2,0xb3,0xb4
.section .text.group,"axG",@progbits,sig_beta,comdat
.globl sig_beta
sig_beta:
    .word 0x1a2b3c4d
    ret
.section .meta.group,"aoG",@progbits,sig_beta,sig_beta,comdat
    .word 0x2a2b2c2d
.comm common_slot,24,16
ASM

cat >"$work/loser.s" <<'ASM'
.section .text.group,"axG",@progbits,sig_alpha,comdat
.globl sig_alpha
sig_alpha:
    .word 0xdeadbeef
    .dword losing_undefined
.section .meta.group,"aoG",@progbits,sig_alpha,sig_alpha,comdat
    .word 0xdeadcafe
ASM

cat >"$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl _start
.type _start,@function
_start:
    .dword alpha_fn
    .dword beta_fn
    .dword sig_alpha
    .dword sig_beta
    .dword alpha_bss
    .dword beta_bss
    .dword common_slot
    .dword input_refs
.size _start,.-_start
.section .data.refs,"aw",@progbits
.balign 8
.globl input_refs
input_refs:
    .dword beta_fn
    .dword alpha_fn
    .dword common_slot
ASM

cat >"$work/direct.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : {
    KEEP(*(.text.entry))
    *beta.o(.text.same)
    *alpha.o(.text.same)
    *(.text.group)
  }
  . = ALIGN(0x1000);
  .data : { *(.data.refs) }
  .meta : { *(.meta.order) }
  .meta_group : { *(.meta.group) }
  .dbg : { *beta.o(.debug.source) *alpha.o(.debug.source) }
  .bss : { *beta.o(.bss.same) *alpha.o(.bss.same) *(.bss) *(COMMON) }
}
LDS

cat >"$work/after-r.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { KEEP(*(.text.entry)) *(.text.same) *(.text.group) }
  . = ALIGN(0x1000);
  .data : { *(.data.refs) }
  .meta : { *(.meta.order) }
  .meta_group : { *(.meta.group) }
  .dbg : { *(.debug.source) }
  .bss : { *(.bss.same) *(.bss) *(COMMON) }
}
LDS

for name in alpha beta loser entry; do
    "$MINIAS" -march=rv64i -mabi=lp64 \
        -o "$work/$name.o" "$work/$name.s"
done

inputs=("$work/alpha.o" "$work/beta.o" "$work/loser.o" "$work/entry.o")
"$MINILD" -melf64lriscv -r -o "$work/first.o" "${inputs[@]}"
"$MINILD" -melf64lriscv -r -o "$work/second.o" "$work/first.o"
"$MINILD" -melf64lriscv -static -e _start \
    -o "$work/default.elf" "${inputs[@]}"
"$MINILD" -melf64lriscv -static --gc-sections \
    -T "$work/direct.lds" -o "$work/script-gc.elf" "${inputs[@]}"
"$MINILD" -melf64lriscv -static -T "$work/after-r.lds" \
    -o "$work/after-r.elf" "$work/second.o"
"$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libsingle-projection.so -o "$work/shared.so" "${inputs[@]}"

python3 - "$work" <<'PY'
import struct
import sys
from pathlib import Path


def check(value, message):
    if not value:
        raise SystemExit(message)


class Elf:
    def __init__(self, path):
        self.path = path
        self.raw = Path(path).read_bytes()
        d = self.raw
        check(d[:6] == b'\x7fELF\x02\x01' and len(d) >= 64,
              f'{path}: not ELF64LE')
        self.kind, machine = struct.unpack_from('<HH', d, 16)
        check(machine == 243, f'{path}: not RISC-V')
        off = struct.unpack_from('<Q', d, 40)[0]
        stride, count, names_index = struct.unpack_from('<HHH', d, 58)
        check(stride == 64 and 0 < names_index < count and
              off + count * stride <= len(d), f'{path}: bad SHDR array')
        self.headers = [struct.unpack_from('<IIQQQQIIQQ', d, off + i * stride)
                        for i in range(count)]
        names = self.payload(names_index)
        self.names = [self.cstr(names, sh[0]) for sh in self.headers]
        self.by_name = {}
        for idx, name in enumerate(self.names):
            self.by_name.setdefault(name, []).append(idx)
        self.symbols = {}
        self.sym_by_index = []
        if '.symtab' in self.by_name:
            symtab = self.unique('.symtab')
            table = self.headers[symtab]
            check(table[6] < count and table[9] == 24 and table[5] % 24 == 0,
                  f'{path}: invalid symtab')
            strings = self.payload(table[6])
            for pos in range(table[4], table[4] + table[5], 24):
                nameoff, info, other, index, value, size = struct.unpack_from(
                    '<IBBHQQ', d, pos)
                name = self.cstr(strings, nameoff)
                symbol = (name, info, other, index, value, size)
                self.sym_by_index.append(symbol)
                if name:
                    self.symbols.setdefault(name, []).append(symbol)

    @staticmethod
    def cstr(pool, start):
        check(start < len(pool), 'ELF string offset outside table')
        end = pool.find(b'\0', start)
        check(end != -1, 'unterminated ELF string')
        return pool[start:end].decode('ascii')

    def payload(self, idx):
        sh = self.headers[idx]
        check(sh[1] != 8 and sh[4] + sh[5] <= len(self.raw),
              f'{self.path}: invalid data for section {idx}')
        return self.raw[sh[4]:sh[4] + sh[5]]

    def unique(self, name):
        hits = self.by_name.get(name, [])
        check(len(hits) == 1, f'{self.path}: expected one {name}, got {hits}')
        return hits[0]

    def symbol(self, name):
        hits = [s for s in self.symbols.get(name, []) if s[3] != 0]
        check(len(hits) == 1,
              f'{self.path}: expected defined symbol {name}: {hits}')
        return hits[0]

    def at(self, name, length):
        symbol = self.symbol(name)
        sh = self.headers[symbol[3]]
        start = symbol[4] - (0 if self.kind == 1 else sh[3])
        check(0 <= start <= sh[5] and length <= sh[5] - start,
              f'{self.path}: {name} out of bounds')
        return self.payload(symbol[3])[start:start + length]


root = Path(sys.argv[1])
partial = [Elf(root / f'{stage}.o') for stage in ('first', 'second')]
finals = {stage: Elf(root / f'{stage}.elf') for stage in
          ('default', 'script-gc', 'after-r')}
shared = Elf(root / 'shared.so')

for stage, elf in zip(('r', 'rr'), partial):
    check(elf.kind == 1, f'{stage}: not ET_REL')
    bss = elf.unique('.bss.same')
    texts = elf.by_name.get('.text.same', [])
    check(len(texts) == 2 and
          sorted((elf.headers[i][8], elf.headers[i][5]) for i in texts) ==
          [(8, 16), (32, 16)],
          f'{stage}: LINK_ORDER targets lost distinct source identity')
    check(elf.headers[bss][1] == 8 and elf.headers[bss][8] == 64 and
          elf.headers[bss][5] == 77,
          f'{stage}: source BSS NOBITS size/alignment changed')
    for name, section, offset in (('alpha_bss', bss, 0),
                                  ('beta_bss', bss, 64)):
        sym = elf.symbol(name)
        check(sym[3] == section and sym[4] == offset,
              f'{stage}: wrong fragment coordinate {name}')
    for name, marker in (('alpha_fn', bytes(range(0xa1, 0xa9))),
                         ('beta_fn', bytes(range(0xb1, 0xb9)))):
        sym = elf.symbol(name)
        check(sym[3] in texts and sym[4] == 0 and
              elf.payload(sym[3])[:8] == marker,
              f'{stage}: wrong source text identity {name}')
    common = elf.symbol('common_slot')
    check(common[3] == 0xfff2 and common[4] == 16 and common[5] == 24,
          f'{stage}: COMMON must stay unallocated in ET_REL')
    groups = {}
    for index, sh in enumerate(elf.headers):
        if sh[1] != 17:
            continue
        check(sh[7] < len(elf.sym_by_index) and sh[5] % 4 == 0,
              f'{stage}: bad group symbol or size')
        signature = elf.sym_by_index[sh[7]][0]
        words = struct.unpack('<' + 'I' * (sh[5] // 4), elf.payload(index))
        check(words[0] == 1, f'{stage}: group is not COMDAT')
        groups[signature] = set(words[1:])
    check(set(groups) == {'sig_alpha', 'sig_beta'},
          f'{stage}: COMDAT winner/identity changed: {groups.keys()}')
    for signature, payload in (('sig_alpha', b'\x1d\x1c\x1b\x1a'),
                               ('sig_beta', b'\x2d\x2c\x2b\x2a')):
        target = elf.symbol(signature)[3]
        linked = [idx for idx in groups[signature]
                  if elf.names[idx] == '.meta.group']
        check(target in groups[signature] and len(linked) == 1 and
              elf.headers[linked[0]][6] == target and
              elf.payload(linked[0]) == payload,
              f'{stage}: COMDAT LINK_ORDER source identity broken')
    for name, marker in (('alpha_fn', bytes.fromhex('44332211')),
                         ('beta_fn', bytes.fromhex('88776655'))):
        target = elf.symbol(name)[3]
        matches = [idx for idx in elf.by_name.get('.meta.order', [])
                   if elf.headers[idx][6] == target and
                   elf.payload(idx) == marker]
        check(len(matches) == 1,
              f'{stage}: nonalloc LINK_ORDER input identity changed: {name}')
    print(f'MINILD_INPUT_PROJECTION_CHECK=PASS stage={stage} '
          'group=2 nobits=77 common=unallocated')

for stage, elf in finals.items():
    check(elf.kind == 2, f'{stage}: not ET_EXEC')
    required = ('_start', 'alpha_fn', 'beta_fn', 'sig_alpha', 'sig_beta',
                'alpha_bss', 'beta_bss', 'common_slot', 'input_refs')
    for name in required:
        sym = elf.symbol(name)
        check(sym[3] < len(elf.headers) and sym[3] != 0,
              f'{stage}: symbol {name} lost final section')
    check(elf.at('alpha_fn', 8) == bytes(range(0xa1, 0xa9)) and
          elf.at('beta_fn', 8) == bytes(range(0xb1, 0xb9)),
          f'{stage}: input fragment content displaced')
    check(elf.at('sig_alpha', 4) == bytes.fromhex('0d0c0b0a') and
          elf.at('sig_beta', 4) == bytes.fromhex('4d3c2b1a'),
          f'{stage}: COMDAT winning bytes changed')
    addresses = [elf.symbol(name)[4] for name in
                 ('alpha_fn', 'beta_fn', 'sig_alpha', 'sig_beta',
                  'alpha_bss', 'beta_bss', 'common_slot', 'input_refs')]
    check(elf.at('_start', 64) == struct.pack('<8Q', *addresses),
          f'{stage}: R_RISCV_64 fixups contain stale source indices')
    check(elf.at('alpha_fn', 16)[8:] == struct.pack('<Q', addresses[1]) and
          elf.at('beta_fn', 16)[8:] == struct.pack('<Q', addresses[0]),
          f'{stage}: cross-fragment relocations use stale offsets')
    check(elf.at('input_refs', 24) ==
          struct.pack('<3Q', addresses[1], addresses[0], addresses[6]),
          f'{stage}: data relocation source identity broken')
    bss_a, bss_b, common = (elf.symbol(name)[4] for name in
                            ('alpha_bss', 'beta_bss', 'common_slot'))
    check(bss_a % 16 == 0 and bss_b % 64 == 0 and common % 16 == 0 and
          not (bss_a < bss_b + 13 and bss_b < bss_a + 9) and
          all(common + 24 <= start or common >= start + size
              for start, size in ((bss_a, 9), (bss_b, 13))),
          f'{stage}: COMMON overlaps input .bss or lost alignment')
    check(elf.headers[elf.symbol('common_slot')[3]][1] == 8,
          f'{stage}: COMMON not allocated to NOBITS')
    check(bytes.fromhex('efbeadde') not in elf.raw and
          bytes.fromhex('fecaadde') not in elf.raw,
          f'{stage}: discarded COMDAT payload leaked')
    if stage == 'script-gc':
        check(elf.symbol('beta_fn')[4] < elf.symbol('alpha_fn')[4] and
              bss_b < bss_a < common,
              'script-gc: filename-qualified member ordering lost')
        meta = elf.headers[elf.unique('.meta')]
        check(meta[2] & 0x80 and meta[6] == elf.unique('.text') and
              elf.payload(elf.unique('.meta')) ==
              bytes.fromhex('4433221188776655'),
              'script-gc: nonalloc LINK_ORDER sh_link or contents wrong')
        check(elf.payload(elf.unique('.dbg')) ==
              bytes.fromhex('b1b2b3b4a1a2a3a4'),
              'script-gc: filename-selected nonalloc bytes reordered')
    print(f'MINILD_INPUT_PROJECTION_CHECK=PASS stage={stage} '
          'symbols=9 absolute_fixups=13 common=24')

check(shared.kind == 3, 'shared: not ET_DYN')
for name in ('alpha_fn', 'beta_fn', 'sig_alpha', 'sig_beta',
             'alpha_bss', 'beta_bss', 'common_slot'):
    source = shared.symbol(name)
    check(source[3] not in (0, 0xfff2) and
          source[3] < len(shared.headers),
          f'shared: unresolved source symbol {name}')
check(shared.at('alpha_fn', 8) == bytes(range(0xa1, 0xa9)) and
      shared.at('beta_fn', 8) == bytes(range(0xb1, 0xb9)),
      'shared: same-name source fragment values changed')
dynsym = shared.headers[shared.unique('.dynsym')]
dynstr = shared.payload(dynsym[6])
exports = set()
for pos in range(dynsym[4], dynsym[4] + dynsym[5], 24):
    nameoff, info, other, index, value, size = struct.unpack_from(
        '<IBBHQQ', shared.raw, pos)
    if nameoff:
        exports.add(Elf.cstr(dynstr, nameoff))
check({'alpha_fn', 'beta_fn', 'common_slot'} <= exports,
      'shared: dynamically exported source symbols missing')
check(shared.headers[shared.unique('.dynamic')][6] ==
      shared.unique('.dynstr'),
      'shared: synthetic dynamic sh_link stale after input projection')
print('MINILD_INPUT_PROJECTION_CHECK=PASS stage=shared '
      'exports=3 commons=allocated')
print('MINILD_INPUT_PROJECTION=PASS stages=r,rr,default,script-gc,after-r,shared')
PY
