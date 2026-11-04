#!/usr/bin/env bash
# RV64 final-layout fixups share one semantic contract across ET_EXEC/ET_DYN.
# Probe encoded instruction bytes, source relocation types, GOT/PLT metadata,
# CFI PCREL32 and fail-closed mismatched/overflow relocations. No cross-GNU or
# QEMU binaries are required; supported MiniAS source forms are used directly.
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
command -v python3 >/dev/null 2>&1 || {
    echo 'MINILD_RISCV_FINAL_FIXUP=INCONCLUSIVE missing_tool=python3' >&2
    exit 8
}

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-rv-final-fixup.XXXXXX")
cleanup() {
    if [[ ${MINILD_RV_FIXUP_KEEP:-0} == 1 ]]; then
        echo "MINILD_RISCV_FINAL_FIXUP_ARTIFACTS=$work"
    else
        rm -rf -- "$work"
    fi
}
trap cleanup EXIT

cat >"$work/fixups.s" <<'ASM'
.option norelax
.option rvc
.cfi_sections .eh_frame
.section .text.fixups,"ax",@progbits
.balign 4
.globl fix_entry
.type fix_entry,@function
fix_entry:
.cfi_startproc
    lui t0,%hi(fix_target)
    addi t0,t0,%lo(fix_target)
    sw t1,%lo(fix_target)(t0)
    lla a0,fix_target
.option pic
    la a1,fix_target
.option nopic
    sw t3,fix_target,t2
    lla a2,fix_target+8
    sw t4,fix_target-4,t5
fix_branch:
    .word 0x00b50063
fix_jal:
    .word 0x000000ef
fix_cbranch:
    .half 0xc001
fix_cjump:
    .half 0xa001
    # MiniAS currently expands unresolved branches before relocation.
    # These address references become relocation records at the four raw
    # instruction labels via the fixture projection below.
    .dword branch_target
    .dword jal_target
    .dword rvc_target
    .dword rvc_target
    call branch_target@plt
    ret
.cfi_endproc
.size fix_entry,.-fix_entry
ASM

cat >"$work/targets.s" <<'ASM'
.option rvc
.section .text.targets,"ax",@progbits
.balign 4
.globl branch_target
.type branch_target,@function
branch_target:
    ret
.globl jal_target
.type jal_target,@function
jal_target:
    ret
.globl rvc_target
.type rvc_target,@function
rvc_target:
    ret
.section .data.target,"aw",@progbits
.balign 8
.globl fix_target
.type fix_target,@object
fix_target:
    .dword 0x1122334455667788
.size fix_target,8
ASM

cat >"$work/missing-pcrel.s" <<'ASM'
.option norelax
.section .text.missing,"ax",@progbits
.globl broken_start
broken_start:
    lla t0,fix_target
    ret
ASM

cat >"$work/overflow.s" <<'ASM'
.option norelax
.section .text.overflow,"ax",@progbits
.globl far_start
far_start:
    .word 0x00b50063
    .dword far_branch_target
    ret
ASM

cat >"$work/far.s" <<'ASM'
.section .text.far,"ax",@progbits
    .zero 8192
.globl far_branch_target
far_branch_target:
    ret
ASM

cat >"$work/kernel.s" <<'ASM'
.option norelax
.option norvc
.section .text.kernel,"ax",@progbits
.globl kernel_entry
kernel_entry:
    lla a0,kernel_target+8
    sw a1,kernel_target-4,t0
    jal ra,kernel_jump
    ret
.section .text.kerneljump,"ax",@progbits
.globl kernel_jump
kernel_jump:
    ret
.section .data.kernel,"aw",@progbits
.balign 8
.globl kernel_target
kernel_target:
    .dword 0x123456789abcdef0
ASM

cat >"$work/dead.s" <<'ASM'
.option norelax
.section .text.dead,"ax",@progbits
.globl dead_entry
dead_entry:
    lla t0,kernel_target
    ret
ASM

cat >"$work/kernel.lds" <<'LDS'
OUTPUT_ARCH(riscv)
ENTRY(kernel_entry)
SECTIONS {
  . = 0xffffffff80000000;
  .text : { KEEP(*(.text.kernel)) *(.text.kerneljump) }
  .data : { *(.data.kernel) }
  /DISCARD/ : { *(.text.dead) }
}
LDS

for name in fixups targets missing-pcrel overflow far kernel dead; do
    "$MINIAS" -march=rv64ic -mabi=lp64 \
        -o "$work/$name.o" "$work/$name.s"
done

# MiniAS deliberately generates matching anchors for normal LA. To test a
# missing-HI negative without depending on unsupported direct %pcrel_lo
# syntax, neutralize only its HI relocation in the temporary ELF fixture.
# The remaining LO references a valid anchor, but no matching HI survives.
python3 - "$work/fixups.o" "$work/missing-pcrel.o" \
    "$work/overflow.o" "$work/dead.o" <<'PY'
from pathlib import Path
import struct
import sys

for filename in (sys.argv[2], sys.argv[4]):
    path = Path(filename)
    raw = bytearray(path.read_bytes())
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    count = struct.unpack_from('<H', raw, 60)[0]
    patched = 0
    for i in range(count):
        sh = struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
        if sh[1] != 4:
            continue
        for pos in range(sh[4], sh[4] + sh[5], 24):
            info = struct.unpack_from('<Q', raw, pos + 8)[0]
            if info & 0xffffffff == 23:  # PCREL_HI20 -> R_RISCV_NONE
                struct.pack_into('<Q', raw, pos + 8, info & ~0xffffffff)
                patched += 1
    assert patched == 1, f'{filename}: expected one HI, got {patched}'
    path.write_bytes(raw)

# MiniAS currently expands unresolved far B branches into inverted B + JAL,
# and has no RV `.reloc` directive.  A valid raw B-type instruction plus a
# surgical relocation conversion gives a genuine R_RISCV_BRANCH overflow
# without depending on either assembler behavior.
path = Path(sys.argv[3])
raw = bytearray(path.read_bytes())
shoff = struct.unpack_from('<Q', raw, 40)[0]
count = struct.unpack_from('<H', raw, 60)[0]
patched = 0
for i in range(count):
    sh = struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
    if sh[1] != 4:
        continue
    for pos in range(sh[4], sh[4] + sh[5], 24):
        info = struct.unpack_from('<Q', raw, pos + 8)[0]
        if info & 0xffffffff == 2:  # R_RISCV_64 -> R_RISCV_BRANCH
            struct.pack_into('<Q', raw, pos, 0)
            struct.pack_into('<Q', raw, pos + 8,
                             (info & ~0xffffffff) | 16)
            patched += 1
assert patched == 1, f'expected one RV64 address relocation, got {patched}'
path.write_bytes(raw)

# Rewrite four temporary R_RISCV_64 source placeholders to proper RV B/J/C
# relocation records on actual instruction-shaped data.  This is the only
# host-independent route while MiniAS's RV `.reloc` directive is unavailable.
path = Path(sys.argv[1])
raw = bytearray(path.read_bytes())
shoff = struct.unpack_from('<Q', raw, 40)[0]
count = struct.unpack_from('<H', raw, 60)[0]
sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
            for i in range(count)]
names_idx = struct.unpack_from('<H', raw, 62)[0]
shstrings = raw[sections[names_idx][4]:sections[names_idx][4] + sections[names_idx][5]]
def name(offset):
    end = shstrings.index(0, offset)
    return shstrings[offset:end].decode()

symbol_offsets = {}
for sh in sections:
    if sh[1] != 2:
        continue
    strs = sections[sh[6]]
    pool = raw[strs[4]:strs[4] + strs[5]]
    for pos in range(sh[4], sh[4] + sh[5], 24):
        nameoff, info, other, shndx, value, size = struct.unpack_from(
            '<IBBHQQ', raw, pos)
        end = pool.index(0, nameoff)
        symbol_offsets[pool[nameoff:end].decode()] = value
records = []
for sh in sections:
    if sh[1] == 4 and name(sections[sh[7]][0]) == '.text.fixups':
        for pos in range(sh[4], sh[4] + sh[5], 24):
            info = struct.unpack_from('<Q', raw, pos + 8)[0]
            if info & 0xffffffff == 2:
                records.append(pos)
assert len(records) == 4, f'expected four temporary B/J/C relocs, got {len(records)}'
for pos, (symbol, kind) in zip(records, (
    ('fix_branch', 16), ('fix_jal', 17),
    ('fix_cbranch', 44), ('fix_cjump', 45))):
    old = struct.unpack_from('<Q', raw, pos + 8)[0]
    assert symbol in symbol_offsets, f'missing instruction anchor {symbol}'
    struct.pack_into('<Q', raw, pos, symbol_offsets[symbol])
    struct.pack_into('<Q', raw, pos + 8, (old & ~0xffffffff) | kind)
path.write_bytes(raw)
PY

"$MINILD" -melf64lriscv -static -e fix_entry \
    -o "$work/final.elf" "$work/fixups.o" "$work/targets.o"
"$MINILD" -melf64lriscv -shared --hash-style=both \
    -soname libfinal-fixup.so -o "$work/final.so" \
    "$work/fixups.o" "$work/targets.o"
"$MINILD" -melf64lriscv -static --gc-sections \
    -T "$work/kernel.lds" -o "$work/kernel.elf" \
    "$work/kernel.o" "$work/dead.o"

for kind in static shared; do
    options=(-static -e broken_start)
    if [[ $kind == shared ]]; then options=(-shared); fi
    options=(-static -e far_start)
    if [[ $kind == shared ]]; then options=(-shared); fi
    if "$MINILD" -melf64lriscv "${options[@]}" \
        -o "$work/$kind-missing.elf" "$work/missing-pcrel.o" "$work/targets.o" \
        >"$work/$kind-missing.log" 2>&1; then
        echo "MINILD_RISCV_FINAL_FIXUP=FAIL case=$kind-missing accepted" >&2
        exit 1
    fi
    if [[ -e $work/$kind-missing.elf ]]; then
        echo "MINILD_RISCV_FINAL_FIXUP=FAIL case=$kind-missing partial_output" >&2
        exit 1
    fi
    if "$MINILD" -melf64lriscv "${options[@]}" \
        -o "$work/$kind-overflow.elf" "$work/overflow.o" "$work/far.o" \
        >"$work/$kind-overflow.log" 2>&1; then
        echo "MINILD_RISCV_FINAL_FIXUP=FAIL case=$kind-overflow accepted" >&2
        exit 1
    fi
    if [[ -e $work/$kind-overflow.elf ]]; then
        echo "MINILD_RISCV_FINAL_FIXUP=FAIL case=$kind-overflow partial_output" >&2
        exit 1
    fi
done

python3 - "$work/fixups.o" "$work/final.elf" "$work/final.so" \
    "$work/static-missing.log" "$work/shared-missing.log" \
    "$work/static-overflow.log" "$work/shared-overflow.log" \
    "$work/kernel.o" "$work/dead.o" "$work/kernel.elf" <<'PY'
import struct
import sys
from pathlib import Path


def ensure(ok, detail):
    if not ok:
        raise SystemExit(detail)


class Elf:
    def __init__(self, file):
        self.file = str(file)
        self.data = Path(file).read_bytes()
        d = self.data
        ensure(d[:6] == b'\x7fELF\x02\x01' and len(d) >= 64,
               f'{file}: invalid ELF64LE')
        self.kind, machine = struct.unpack_from('<HH', d, 16)
        ensure(machine == 243, f'{file}: not RV64')
        shoff = struct.unpack_from('<Q', d, 40)[0]
        stride, count, names = struct.unpack_from('<HHH', d, 58)
        ensure(stride == 64 and 0 < names < count and
               shoff + stride * count <= len(d), f'{file}: bad section table')
        self.sections = [struct.unpack_from('<IIQQQQIIQQ', d, shoff + i * 64)
                         for i in range(count)]
        pool = self.payload(names)
        self.names = [self.cstr(pool, sh[0]) for sh in self.sections]
        self.named = {}
        for i, name in enumerate(self.names):
            self.named.setdefault(name, []).append(i)
        self.symbols = []
        self.symnames = {}
        for name in ('.symtab',):
            if name not in self.named:
                continue
            tab = self.sections[self.one(name)]
            strs = self.payload(tab[6])
            ensure(tab[9] == 24 and tab[5] % 24 == 0,
                   f'{file}: malformed {name}')
            for off in range(tab[4], tab[4] + tab[5], 24):
                pos, info, other, sec, addr, size = struct.unpack_from(
                    '<IBBHQQ', d, off)
                name = self.cstr(strs, pos)
                record = (name, sec, addr, size)
                self.symbols.append(record)
                self.symnames.setdefault(name, []).append(record)

    @staticmethod
    def cstr(pool, off):
        ensure(off < len(pool), 'ELF string offset out of range')
        end = pool.find(b'\0', off)
        ensure(end != -1, 'unterminated ELF string')
        return pool[off:end].decode('ascii')

    def payload(self, sec):
        sh = self.sections[sec]
        ensure(sh[1] != 8 and sh[4] + sh[5] <= len(self.data),
               f'{self.file}: missing section data {sec}')
        return self.data[sh[4]:sh[4] + sh[5]]

    def one(self, name):
        matches = self.named.get(name, ())
        ensure(len(matches) == 1,
               f'{self.file}: expected unique {name}, got {matches}')
        return matches[0]

    def sym(self, name):
        records = [r for r in self.symnames.get(name, ()) if r[1] != 0]
        ensure(len(records) == 1, f'{self.file}: missing/duplicate {name}')
        return records[0]

    def instruction(self, base, off, bits=32):
        start = self.sym(base)[2] - self.sections[self.sym(base)[1]][3] + off
        payload = self.payload(self.sym(base)[1])
        ensure(0 <= start <= len(payload) and bits // 8 <= len(payload) - start,
               f'{self.file}: instruction range invalid {off}')
        return int.from_bytes(payload[start:start + bits // 8], 'little')

    def relocs(self, target):
        result = []
        for index, sh in enumerate(self.sections):
            if sh[1] != 4 or self.names[sh[7]] != target:
                continue
            ensure(sh[5] % 24 == 0, f'{self.file}: malformed RELA')
            for pos in range(sh[4], sh[4] + sh[5], 24):
                off, info, addend = struct.unpack_from('<QQq', self.data, pos)
                result.append((off, info & 0xffffffff,
                               self.symbols[info >> 32][0], addend))
        return result


def sign(value, width):
    mask = 1 << (width - 1)
    return (value ^ mask) - mask


def immediate_u(word):
    return sign(word & 0xfffff000, 32)


def immediate_i(word):
    return sign(word >> 20, 12)


def immediate_s(word):
    return sign(((word >> 25) << 5) | ((word >> 7) & 31), 12)


def branch_delta(word):
    bits = (((word >> 31) & 1) << 12 | ((word >> 7) & 1) << 11 |
            ((word >> 25) & 63) << 5 | ((word >> 8) & 15) << 1)
    return sign(bits, 13)


def jal_delta(word):
    bits = (((word >> 31) & 1) << 20 | ((word >> 12) & 255) << 12 |
            ((word >> 20) & 1) << 11 | ((word >> 21) & 1023) << 1)
    return sign(bits, 21)


def c_branch_delta(word):
    bits = (((word >> 12) & 1) << 8 | ((word >> 10) & 3) << 3 |
            ((word >> 5) & 3) << 6 | ((word >> 3) & 3) << 1 |
            ((word >> 2) & 1) << 5)
    return sign(bits, 9)


def c_jump_delta(word):
    bits = (((word >> 12) & 1) << 11 | ((word >> 11) & 1) << 4 |
            ((word >> 9) & 3) << 8 | ((word >> 8) & 1) << 10 |
            ((word >> 7) & 1) << 6 | ((word >> 6) & 1) << 7 |
            ((word >> 3) & 7) << 1 | ((word >> 2) & 1) << 5)
    return sign(bits, 12)


source = Elf(sys.argv[1])
ensure(source.kind == 1, 'source: not ET_REL')
source_relocs = source.relocs('.text.fixups')
types = [r[1] for r in source_relocs]
required = {16, 17, 20, 23, 24, 25, 26, 27, 28, 44, 45}
ensure(required <= set(types),
       f'source: assembler missing required final relocations {required - set(types)}')
frame_relocs = source.relocs('.eh_frame')
ensure(len([r for r in frame_relocs if r[1] == 57]) >= 1,
       'source: no R_RISCV_32_PCREL from .eh_frame')

by_type = {}
for offset, kind, symbol, addend in source_relocs:
    by_type.setdefault(kind, []).append(offset)

for name, file, kind in (('static', sys.argv[2], 2),
                         ('shared', sys.argv[3], 3)):
    elf = Elf(file)
    ensure(elf.kind == kind, f'{name}: output ELF kind wrong')
    entry = elf.sym('fix_entry')[2]
    target = elf.sym('fix_target')[2]
    branch = elf.sym('branch_target')[2]
    jal = elf.sym('jal_target')[2]
    rvc = elf.sym('rvc_target')[2]

    def insn(offset, width=32):
        return elf.instruction('fix_entry', offset, width)

    hi = by_type[26][0]
    lo_i = by_type[27][0]
    lo_s = by_type[28][0]
    ensure(immediate_u(insn(hi)) + immediate_i(insn(lo_i)) == target and
           immediate_s(insn(lo_s)) == sign(target & 0xfff, 12),
           f'{name}: absolute HI20/LO12_I/S mismatch')
    pcrel_hi = [(off, addend) for off, typ, sym, addend in source_relocs
                if typ == 23]
    ensure(len(pcrel_hi) >= 4 and {addend for _, addend in pcrel_hi} >=
           {-4, 0, 8}, f'{name}: missing PCREL I/S and signed addend pairs')
    for off, addend in pcrel_hi:
        nextword = insn(off + 4)
        imm = immediate_s(nextword) if nextword & 127 == 0x23 else \
              immediate_i(nextword)
        ensure(immediate_u(insn(off)) + imm == target + addend - (entry + off),
               f'{name}: PCREL HI20/LO12 pair mismatch at {off} addend={addend}')

    got_hi = by_type[20][0]
    got_pc_delta = immediate_u(insn(got_hi)) + immediate_i(insn(got_hi + 4))
    got_entry = entry + got_hi + got_pc_delta
    got_index = elf.one('.got')
    got_sh = elf.sections[got_index]
    ensure(got_sh[3] <= got_entry and got_entry + 8 <= got_sh[3] + got_sh[5],
           f'{name}: GOT_HI20 points outside .got')
    if kind == 2:
        got_data = elf.payload(got_index)
        ensure(struct.unpack_from('<Q', got_data, got_entry - got_sh[3])[0] == target,
               f'{name}: static GOT entry not patched to target')

    for rtype, destination, decoder, width in (
        (16, branch, branch_delta, 32),
        (17, jal, jal_delta, 32),
        (44, rvc, c_branch_delta, 16),
        (45, rvc, c_jump_delta, 16),
    ):
        off = by_type[rtype][0]
        ensure(decoder(insn(off, width)) == destination - (entry + off),
               f'{name}: encoded PC-relative branch/jump wrong relocation={rtype}')

    eh = elf.sections[elf.one('.eh_frame')]
    eh_bytes = elf.payload(elf.one('.eh_frame'))
    check32 = next(r[0] for r in frame_relocs if r[1] == 57)
    ensure(check32 + 4 <= len(eh_bytes) and
           sign(struct.unpack_from('<I', eh_bytes, check32)[0], 32) ==
               entry - (eh[3] + check32),
           f'{name}: R_RISCV_32_PCREL .eh_frame is not final')
    ensure('.rela.eh_frame' not in elf.named and
           '.rela.text.fixups' not in elf.named,
           f'{name}: final link leaked input RELA sections')

    if kind == 3:
        dyn = elf.sections[elf.one('.dynsym')]
        dynstr = elf.payload(dyn[6])
        dyn_names = []
        for pos in range(dyn[4], dyn[4] + dyn[5], 24):
            name_pos = struct.unpack_from('<I', elf.data, pos)[0]
            dyn_names.append(Elf.cstr(dynstr, name_pos))
        reloc_entries = []
        for sec in ('.rela.dyn', '.rela.plt'):
            for idx in elf.named.get(sec, ()):
                sh = elf.sections[idx]
                for off in range(sh[4], sh[4] + sh[5], 24):
                    place, info, addend = struct.unpack_from('<QQq', elf.data, off)
                    symidx = info >> 32
                    ensure(symidx < len(dyn_names), f'{name}: bad dynsym index')
                    reloc_entries.append((sec, place, info & 0xffffffff,
                                          dyn_names[symidx], addend))
        ensure(any(sec == '.rela.dyn' and symbol == 'fix_target' and
                   got_sh[3] <= addr < got_sh[3] + got_sh[5]
                   for sec, addr, typ, symbol, addend in reloc_entries),
               'shared: GOT has no dynamic relocation for fix_target')
        ensure(any(sec == '.rela.plt' and typ == 5 and
                   symbol == 'branch_target'
                   for sec, addr, typ, symbol, addend in reloc_entries),
               'shared: branch_target CALL_PLT missing JUMP_SLOT')
        print('MINILD_RISCV_FINAL_FIXUP_CHECK=PASS mode=shared '
              'pcrel=4 got=1 branch=4 cfi_pcrel32=1 rela_plt=true')
    else:
        print('MINILD_RISCV_FINAL_FIXUP_CHECK=PASS mode=static '
              'pcrel=4 got=1 branch=4 cfi_pcrel32=1')

# A Linux-style high-half kernel VMA is a valid signed RV64 address. The
# resulting PC-relative distances remain small; signed INT64_MAX checks must
# not reject either the instruction or its correctly paired LO12.
kernel_input = Elf(sys.argv[8])
dead_input = Elf(sys.argv[9])
kernel = Elf(sys.argv[10])
ensure(kernel.kind == 2, 'kernel: not ET_EXEC')
kernel_entry = kernel.sym('kernel_entry')[2]
kernel_target = kernel.sym('kernel_target')[2]
kernel_jump = kernel.sym('kernel_jump')[2]
ensure(kernel_entry >= 0xffffffff80000000 and
       kernel_target >= 0xffffffff80000000,
       'kernel: high-half scripted addresses lost')
kernel_relocs = kernel_input.relocs('.text.kernel')
for rtype in (23, 24, 25, 17):
    ensure(any(r[1] == rtype for r in kernel_relocs),
           f'kernel: missing input relocation type={rtype}')
for off, typ, symbol, addend in kernel_relocs:
    if typ != 23:
        continue
    second = kernel.instruction('kernel_entry', off + 4)
    imm = immediate_s(second) if second & 127 == 0x23 else immediate_i(second)
    ensure(immediate_u(kernel.instruction('kernel_entry', off)) + imm ==
           kernel_target + addend - (kernel_entry + off),
           f'kernel: high-half PCREL HI/LO mismatch addend={addend}')
for off, typ, symbol, addend in kernel_relocs:
    if typ == 17:
        ensure(jal_delta(kernel.instruction('kernel_entry', off)) ==
               kernel_jump - (kernel_entry + off),
               'kernel: high-half JAL fixup mismatch')
dead_relocs = dead_input.relocs('.text.dead')
ensure(any(r[1] == 24 for r in dead_relocs) and
       not any(r[1] == 23 for r in dead_relocs),
       'kernel: invalid dead LO12 fixture')
ensure('.text.dead' not in kernel.named and
       not any(s[1] != 0 for s in kernel.symnames.get('dead_entry', [])),
       'kernel: discarded source emitted or unresolved LO12 survived GC')
print('MINILD_RISCV_FINAL_FIXUP_CHECK=PASS mode=kernel-highhalf '
      'vma=0xffffffff80000000 pcrel=2 jal=1 discarded_lo12=1')

for name, file, key in (
    ('static-missing', sys.argv[4], 'missing-matching-pcrel-hi20'),
    ('shared-missing', sys.argv[5], 'missing-shared-pcrel-hi20'),
    ('static-overflow', sys.argv[6], 'R_RISCV_BRANCH-overflow'),
    ('shared-overflow', sys.argv[7], 'R_RISCV_BRANCH-overflow'),
):
    actual = Path(file).read_text()
    ensure(key in actual, f'{name}: wrong rejection diagnostic: {actual[:500]}')
    print(f'MINILD_RISCV_FINAL_FIXUP_CHECK=PASS mode={name} fail_closed=true')

print('MINILD_RISCV_FINAL_FIXUP=PASS modes=static,shared,kernel-highhalf,negative_4')
PY
