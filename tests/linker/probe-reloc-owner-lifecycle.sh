#!/usr/bin/env bash
# Regression for stable relocation owners across COMDAT input admission,
# repeated -r, GC, script-induced work-section permutations and R_RISCV_ALIGN
# relaxation's per-section CSR index. MiniAS/MiniLD only, no GNU cross tools.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_RELOC_OWNER=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-reloc-owner.XXXXXX")
cleanup() {
    if [[ ${MINILD_RELOC_OWNER_KEEP:-0} == 1 ]]; then
        echo "MINILD_RELOC_OWNER_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

cat > "$work/winner.s" <<'ASM'
.section .text.alpha,"axG",@progbits,winner,comdat
.option relax
.globl winner
.type winner,@function
winner:
    addi a0,zero,11
    addi a0,a0,1
.p2align 5
.globl winner_aligned
winner_aligned:
    addi a1,zero,51
    ret
.size winner,.-winner
.section .data.winner,"aw",@progbits
    .dword winner_aligned
ASM
cat > "$work/loser.s" <<'ASM'
.section .text.alpha,"axG",@progbits,winner,comdat
.option relax
.globl winner
.type winner,@function
winner:
    addi a0,zero,99
    call losing_unknown
    ret
.size winner,.-winner
ASM
cat > "$work/worker.s" <<'ASM'
.section .text.beta,"ax",@progbits
.option relax
.globl worker
.type worker,@function
worker:
    addi a0,zero,22
    addi a0,a0,1
    addi a0,a0,2
.p2align 5
.globl worker_aligned
worker_aligned:
    addi a1,zero,52
    ret
.size worker,.-worker
.section .text.dead,"ax",@progbits
.globl never_run
never_run:
    call missing_dead_target
    ret
ASM
cat > "$work/entry.s" <<'ASM'
.section .text.entry,"ax",@progbits
.option relax
.globl _start
_start:
    call winner
    call worker
    lui t0,%hi(addresses)
    addi t0,t0,%lo(addresses)
    addi a0,zero,0
    addi a7,zero,93
    ecall
.section .rodata.refs,"a",@progbits
.balign 8
.globl addresses
.type addresses,@object
addresses:
    .dword winner
    .dword winner_aligned
    .dword worker
    .dword worker_aligned
.size addresses,32
ASM
cat > "$work/sections.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.entry) *(.text.alpha) *(.text.beta) }
  .rodata : { *(.rodata.refs) }
  .data : { *(.data.winner) }
  /DISCARD/ : { *(.text.dead) }
}
LDS

for name in winner loser worker entry; do
    "$MINIAS" -march=rv64i -mabi=lp64 -mrelax \
        -o "$work/$name.o" "$work/$name.s"
done

"$MINILD" -melf64lriscv -r -o "$work/once.o" \
    "$work/winner.o" "$work/loser.o" "$work/worker.o" "$work/entry.o"
"$MINILD" -melf64lriscv -r -o "$work/twice.o" "$work/once.o"

for path in direct twice; do
    inputs=("$work/twice.o")
    if [[ $path == direct ]]; then
        inputs=("$work/winner.o" "$work/loser.o" "$work/worker.o" "$work/entry.o")
    fi
    "$MINILD" -melf64lriscv -static --gc-sections -e _start \
        -o "$work/$path-gc.elf" "${inputs[@]}"
    "$MINILD" -melf64lriscv -static --gc-sections \
        -T "$work/sections.lds" -o "$work/$path-script.elf" "${inputs[@]}"
    "$MINILD" -melf64lriscv -shared \
        -o "$work/$path-shared.so" "${inputs[@]}"
done

python3 - "$work/once.o" "$work/twice.o" \
    "$work/direct-gc.elf" "$work/twice-gc.elf" \
    "$work/direct-script.elf" "$work/twice-script.elf" \
    "$work/direct-shared.so" "$work/twice-shared.so" <<'PY'
import struct
import sys
from pathlib import Path

def parse(path):
    data = Path(path).read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise SystemExit(f'{path}: requires ELF64 little-endian')
    elf_type, machine = struct.unpack_from('<HH', data, 16)
    if machine != 243 or elf_type not in (1, 2, 3):
        raise SystemExit(f'{path}: expected ET_REL/EXEC/DYN RISC-V')
    table = struct.unpack_from('<Q', data, 40)[0]
    width, count, names_index = struct.unpack_from('<HHH', data, 58)
    if width != 64 or names_index >= count or table + count * width > len(data):
        raise SystemExit(f'{path}: invalid section headers')
    headers = [struct.unpack_from('<IIQQQQIIQQ', data, table + width * i)
               for i in range(count)]
    def content(sh):
        if sh[4] + sh[5] > len(data):
            raise SystemExit(f'{path}: section contents outside file')
        return data[sh[4]:sh[4] + sh[5]]
    def cstr(pool, offset):
        end = pool.find(b'\0', offset)
        if offset >= len(pool) or end < 0:
            raise SystemExit(f'{path}: invalid string table offset')
        return pool[offset:end].decode()
    names = content(headers[names_index])
    secnames = [cstr(names, h[0]) for h in headers]
    symtab = headers[secnames.index('.symtab')]
    strings = content(headers[symtab[6]])
    symbols = {}
    for off in range(symtab[4], symtab[4] + symtab[5], 24):
        nameoff, info, other, index, value, size = struct.unpack_from('<IBBHQQ', data, off)
        if nameoff and info >> 4:
            name = cstr(strings, nameoff)
            if name in symbols:
                raise SystemExit(f'{path}: duplicate canonical {name}')
            symbols[name] = (index, value, size)
    dynamic_names = []
    if '.dynsym' in secnames:
        dynsym = headers[secnames.index('.dynsym')]
        dynstrings = content(headers[dynsym[6]])
        for off in range(dynsym[4], dynsym[4] + dynsym[5], 24):
            nameoff = struct.unpack_from('<I', data, off)[0]
            dynamic_names.append(cstr(dynstrings, nameoff))
    relocs = []
    for name, sh in zip(secnames, headers):
        if sh[1] == 4:  # SHT_RELA
            section_data = content(sh)
            if sh[5] % 24:
                raise SystemExit(f'{path}: bad RELA table {name}')
            for off in range(0, len(section_data), 24):
                offset, info, addend = struct.unpack_from('<QQq', section_data, off)
                relocs.append((name, offset, info & 0xffffffff, addend,
                               info >> 32))
    return elf_type, data, headers, secnames, symbols, relocs, content, dynamic_names

for stage, path in zip(('r', 'rr', 'direct-gc', 'rr-gc',
                        'direct-script', 'rr-script',
                        'direct-shared', 'rr-shared'), sys.argv[1:]):
    etype, data, headers, secnames, symbols, relocs, content, dynamic_names = parse(path)
    if etype != (1 if stage in ('r', 'rr') else
                 3 if stage.endswith('-shared') else 2):
        raise SystemExit(f'{stage}: wrong ELF type')
    for name in ('winner', 'winner_aligned', 'worker', 'worker_aligned', '_start', 'addresses'):
        if name not in symbols or symbols[name][0] in (0, 0xfff2):
            raise SystemExit(f'{stage}: undefined canonical symbol {name}')
    if bytes.fromhex('13053006') in data:
        raise SystemExit(f'{stage}: losing COMDAT body leaked into output')
    if etype == 1:
        align_relocs = [r for r in relocs if r[2] == 43]
        if len(align_relocs) != 2 or sorted(r[3] for r in align_relocs) != [28, 28]:
            raise SystemExit(f'{stage}: R_RISCV_ALIGN source relocations damaged: {align_relocs}')
        print(f'MINILD_RELOC_OWNER_CHECK=PASS stage={stage} align_relocs=2 comdat_winner=1')
        continue
    if etype == 3:
        if not dynamic_names:
            raise SystemExit(f'{stage}: missing dynamic symbol table')
        dynamic_rel = [(name, typ, dynamic_names[sym]) for name, _, typ, _, sym
                       in relocs if sym < len(dynamic_names)]
        if sorted(name for section, typ, name in dynamic_rel
                  if section == '.rela.dyn' and typ == 2) != sorted(
                      ['winner', 'winner_aligned', 'winner_aligned',
                       'worker', 'worker_aligned']):
            raise SystemExit(f'{stage}: incorrect ET_DYN canonical relocation owners')
        if sorted(name for section, typ, name in dynamic_rel
                  if section == '.rela.plt' and typ == 5) != sorted(
                      ['missing_dead_target', 'winner', 'worker']):
            raise SystemExit(f'{stage}: incorrect ET_DYN JUMP_SLOT owners')
        if any(name == 'losing_unknown' for _, _, name in dynamic_rel):
            raise SystemExit(f'{stage}: discarded COMDAT emitted a relocation')
        print(f'MINILD_RELOC_OWNER_CHECK=PASS stage={stage} '
              'dynamic_relocs=5 plt_relocs=3 comdat_winner=1')
        continue
    if any(name.startswith('.rela.') for name in secnames):
        raise SystemExit(f'{stage}: final output leaked static relocations')
    for name in ('winner_aligned', 'worker_aligned'):
        if symbols[name][1] % 32:
            raise SystemExit(f'{stage}: relaxation alignment lost for {name}')
    for name, expected in (('winner', bytes.fromhex('1305b000')),
                           ('worker', bytes.fromhex('13056001')),
                           ('winner_aligned', bytes.fromhex('93053003')),
                           ('worker_aligned', bytes.fromhex('93054003'))):
        section_index, value, _ = symbols[name]
        sh = headers[section_index]
        pos = value - sh[3]
        if pos < 0 or content(sh)[pos:pos + 4] != expected:
            raise SystemExit(f'{stage}: relocation owner/relaxation misplaced {name}')
    secindex, value, _ = symbols['addresses']
    sh = headers[secindex]
    offset = value - sh[3]
    refs = struct.unpack_from('<4Q', content(sh), offset)
    expected = tuple(symbols[name][1] for name in
                     ('winner', 'winner_aligned', 'worker', 'worker_aligned'))
    if refs != expected:
        raise SystemExit(f'{stage}: relocation target moved after work reorder: {refs} != {expected}')
    if 'never_run' in symbols and symbols['never_run'][0] != 0:
        raise SystemExit(f'{stage}: GC kept dead source')
    print(f'MINILD_RELOC_OWNER_CHECK=PASS stage={stage} '
          'align_targets=2 symbol_fixups=4 comdat_winner=1 dead_gc=1')

print('MINILD_RELOC_OWNER=PASS '
      'stages=r,rr,direct-gc,rr-gc,direct-script,rr-script,direct-shared,rr-shared')
PY
