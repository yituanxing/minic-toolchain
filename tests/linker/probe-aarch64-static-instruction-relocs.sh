#!/usr/bin/env bash
# AArch64 AAELF64 instruction relocation classes, with positive/negative
# displacements, MOVW signed/unchecked semantics and fail-closed negatives.
# GNU cross-ld differential is mandatory only when requested by CI.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-aarch64-instruction-relocs.XXXXXX")
trap 'if [[ ${MINILD_AARCH64_RELOCS_KEEP:-0} == 1 ]]; then echo "MINILD_AARCH64_RELOCS_ARTIFACTS=$work"; else rm -rf -- "$work"; fi' EXIT

cat >"$work/entry.s" <<'ASM'
.text
.p2align 2
.global _start
.type _start,%function
_start:
    adr x0, target
    ldr x1, target
    b.eq target
    cbz w2, target
    tbz x3, #1, target
    movz x4, :abs_g0:target
    movz x5, :abs_g0_nc:target
    movk x6, :abs_g1_nc:target
    movz x7, :abs_g2:target
    movz x8, :abs_g3:target
    movz x9, :abs_g0_s:target
    movn x10, :abs_g0_s:negative
    movn x11, :abs_g1_s:negative
    ret
.size _start, .-_start
ASM
cat >"$work/defs.s" <<'ASM'
.text
.p2align 2
.global target
.type target,%function
target:
    ret
.size target, .-target
.global negative
.set negative, -1
.global oversized
.set oversized, 0x123456
.global high_absolute
.set high_absolute, 0x123456789abcdef0
ASM
cat >"$work/overflow.s" <<'ASM'
.text
.global _start
_start:
    movz x0, :abs_g0:oversized
    ret
ASM
cat >"$work/far-branch.s" <<'ASM'
.text
.global _start
_start:
    b.eq oversized
    tbz x0, #1, oversized
    adr x0, oversized
    ldr x0, oversized
    ret
ASM
cat >"$work/high.s" <<'ASM'
.text
.global _start
_start:
    movz x0, :abs_g3:high_absolute
    movk x0, :abs_g2_nc:high_absolute
    movk x0, :abs_g1_nc:high_absolute
    movk x0, :abs_g0_nc:high_absolute
    ret
ASM
cat >"$work/kernel-pcrel.s" <<'ASM'
.text
.p2align 2
.global _start
.type _start,%function
_start:
    adr x0, target
    ldr x1, target
    b.eq target
    cbz w2, target
    tbz x3, #1, target
    adrp x4, target
    ret
.size _start, .-_start
.section .data.rebase,"aw",%progbits
.global relative_data
relative_data:
    .word target - .
    .quad target - .
ASM
cat >"$work/kernel-absolute.s" <<'ASM'
.section .data.pointer,"aw",%progbits
.global absolute_pointer
absolute_pointer:
    .quad target
ASM
cat >"$work/layout.lds" <<'LDS'
ENTRY(_start)
SECTIONS {
    . = 0x2000;
    .text : { *(.text) }
    . = ALIGN(0x1000);
    .rela.dyn : { *(.rela .rela*) }
    .data : { *(.data*) }
}
LDS

for file in entry defs overflow far-branch high kernel-pcrel kernel-absolute; do
    "$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
        -o "$work/$file.o" "$work/$file.s"
done

# Genuine AArch64 Linux kernel ET_DYN admission: all of these expressions
# preserve their result when both the symbol and the relocation's place move
# by the same page-aligned KASLR slide. PREL32/PREL64 live in allocated data
# and therefore test the semantic rule, not merely instruction opcodes.
readelf -Wr "$work/kernel-pcrel.o" >"$work/kernel-relocs.txt"
for reloc in ADR_PREL_LO21 LD_PREL_LO19 CONDBR19 TSTBR14 \
             ADR_PREL_PG_HI21 PREL32 PREL64; do
    grep -q "R_AARCH64_$reloc" "$work/kernel-relocs.txt"
done
# MiniAS currently emits the checked ADRP form.  Exercise the matching
# psABI _NC relocation with the *same* legal ADRP instruction and symbol by
# changing only r_type in the original relocatable object's RELA entry.
python3 - "$work/kernel-pcrel.o" "$work/kernel-pcrel-nc.o" <<'PY_NC'
from pathlib import Path
import struct
import sys
b = bytearray(Path(sys.argv[1]).read_bytes())
off = struct.unpack_from('<Q', b, 40)[0]
num = struct.unpack_from('<H', b, 60)[0]
changed = False
for i in range(num):
    s = struct.unpack_from('<IIQQQQIIQQ', b, off+i*64)
    if s[1] != 4: continue
    for rela in range(s[4], s[4]+s[5], 24):
        info = struct.unpack_from('<Q', b, rela+8)[0]
        if info & 0xffffffff != 275: continue
        struct.pack_into('<Q', b, rela+8, (info & ~0xffffffff) | 276)
        changed = True
assert changed, 'missing original ADRP RELA'
Path(sys.argv[2]).write_bytes(b)
PY_NC
"$MINILD" -maarch64linux -EL -shared -Bsymbolic --no-undefined \
    --no-apply-dynamic-relocs --pic-veneer -z notext \
    -T "$work/layout.lds" -o "$work/kernel-pcrel.elf" \
    "$work/kernel-pcrel.o" "$work/defs.o"
python3 - "$work/kernel-pcrel.elf" <<'PY_KERNEL'
from pathlib import Path
import struct
import sys
b = Path(sys.argv[1]).read_bytes()
assert b[:6] == b'\x7fELF\x02\x01', 'not ELF64 LE'
assert struct.unpack_from('<HH', b, 16) == (3, 183), 'not AArch64 ET_DYN'
phoff = struct.unpack_from('<Q', b, 32)[0]
entsize, num = struct.unpack_from('<HH', b, 54)
types = [struct.unpack_from('<I', b, phoff+i*entsize)[0]
         for i in range(num)]
assert 1 in types and 2 not in types and 3 not in types, 'unexpected PT_DYNAMIC/INTERP'
print('MINILD_AARCH64_KERNEL_PCREL=PASS static_invariant_relocs=7 ET_DYN=1')
PY_KERNEL
"$MINILD" -maarch64linux -EL -shared -Bsymbolic --no-undefined \
    --no-apply-dynamic-relocs --pic-veneer -z notext \
    -T "$work/layout.lds" -o "$work/kernel-pcrel-nc.elf" \
    "$work/kernel-pcrel-nc.o" "$work/defs.o"
readelf -h "$work/kernel-pcrel-nc.elf" | grep -q 'Type:.*DYN'
echo 'MINILD_AARCH64_KERNEL_PCREL=PASS adrp_nc_rebase_invariant=1'

# True allocated absolute pointers need a runtime R_AARCH64_RELATIVE, not a
# static S+A write.  --no-apply-dynamic-relocs retains their input zero bytes.
"$MINILD" -maarch64linux -EL -shared -Bsymbolic --no-undefined \
    --no-apply-dynamic-relocs --pic-veneer -z notext \
    -T "$work/layout.lds" -o "$work/kernel-relative.elf" \
    "$work/kernel-pcrel.o" "$work/kernel-absolute.o" "$work/defs.o" \
    >"$work/kernel-relative.stdout" 2>"$work/kernel-relative.stderr"
python3 - "$work/kernel-relative.elf" <<'PY_RELA'
import struct,sys
b=open(sys.argv[1],'rb').read()
shoff=struct.unpack_from('<Q',b,40)[0]
shnum,shnames=struct.unpack_from('<HH',b,60)
s=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+i*64) for i in range(shnum)]
pool=b[s[shnames][4]:s[shnames][4]+s[shnames][5]]
def name(sec):
    off=sec[0]
    return pool[off:pool.index(0,off)].decode()
sec={name(x):x for x in s}
rela=sec['.rela.dyn']
data=sec['.data']
assert rela[5] == 24 and rela[1] == 4, 'one actual ELF64 RELA'
roff,info,addend=struct.unpack_from('<QQq',b,rela[4])
assert (info & 0xffffffff)==1027 and (info>>32)==0
assert data[3]<=roff and roff+8<=data[3]+data[5], 'runtime pointer location'
assert addend>=sec['.text'][3], 'RELA addend uses resolved image VMA'
off=data[4]+roff-data[3]
assert b[off:off+8]==b'\0'*8, 'dynamic pointer was pre-applied'
print('MINILD_AARCH64_KERNEL_RELATIVE=PASS records=1 original_bytes_preserved=1')
PY_RELA

# MOVW encodes a non-relocatable absolute pointer in code; R_RELATIVE on an
# 8-byte data slot cannot repair that instruction. Preserve fail-closed mode.
if "$MINILD" -maarch64linux -EL -shared -Bsymbolic --no-undefined \
    --no-apply-dynamic-relocs --pic-veneer -z notext \
    -T "$work/layout.lds" -o "$work/kernel-unsafe-movw.elf" \
    "$work/entry.o" "$work/defs.o" \
    >"$work/kernel-unsafe.stdout" 2>"$work/kernel-unsafe.stderr"; then
    echo 'MINILD_AARCH64_KERNEL_PCREL=FAIL absolute_MovW_runtime_pointer_accepted' >&2
    exit 1
fi
grep -q 'unsupported-kernel-shared-runtime-relocation:26' \
    "$work/kernel-unsafe.stderr"
test ! -e "$work/kernel-unsafe-movw.elf"
echo 'MINILD_AARCH64_KERNEL_PCREL=PASS unsafe_movw_rejected=1'

# The high 64-bit address must survive every group without unsigned-host
# narrowing or a 32-bit G2/G3 shift.  G3 is the checked final chunk.
"$MINILD" -maarch64linux -static -T "$work/layout.lds" \
    -o "$work/high.elf" "$work/high.o" "$work/defs.o"
python3 - "$work/high.elf" <<'PY_HIGH'
from pathlib import Path
import struct
import sys
b = Path(sys.argv[1]).read_bytes()
off = struct.unpack_from('<Q', b, 40)[0]
shnum, shnames = struct.unpack_from('<HH', b, 60)
ss = [struct.unpack_from('<IIQQQQIIQQ', b, off+i*64) for i in range(shnum)]
names = b[ss[shnames][4]:ss[shnames][4]+ss[shnames][5]]
for s in ss:
    if names[s[0]:names.index(0,s[0])] != b'.text': continue
    words = struct.unpack_from('<IIIII', b, s[4])
    expected = 0x123456789abcdef0
    for group, word in zip((3, 2, 1, 0), words[:4]):
        assert ((word >> 21) & 3) == group
        assert ((word >> 5) & 0xffff) == (expected >> (group*16)) & 0xffff
    print('MINILD_AARCH64_MOVW64=PASS unsigned_high_chunk=1')
    break
else: raise AssertionError('missing .text')
PY_HIGH
readelf -Wr "$work/entry.o" >"$work/input-relocs.txt"
for relocation in ADR_PREL_LO21 LD_PREL_LO19 CONDBR19 TSTBR14 \
    MOVW_UABS_G0 MOVW_UABS_G0_NC MOVW_UABS_G1_NC \
    MOVW_UABS_G2 MOVW_UABS_G3 MOVW_SABS_G0 MOVW_SABS_G1; do
    grep -q "R_AARCH64_$relocation" "$work/input-relocs.txt"
done

# Both source orders must work: one has forward PC relocs, one backward.
for order in forward backward; do
    if [[ $order == forward ]]; then objects=("$work/entry.o" "$work/defs.o")
    else objects=("$work/defs.o" "$work/entry.o"); fi
    "$MINILD" -maarch64linux -static -T "$work/layout.lds" \
        -o "$work/mini.$order.elf" "${objects[@]}"
    python3 - "$work/mini.$order.elf" <<'PY_CHECK'
from pathlib import Path
import struct
import sys

b = Path(sys.argv[1]).read_bytes()
assert b[:6] == b'\x7fELF\x02\x01'
shoff = struct.unpack_from('<Q', b, 40)[0]
num, names = struct.unpack_from('<HH', b, 60)
sections = [struct.unpack_from('<IIQQQQIIQQ', b, shoff + i*64)
            for i in range(num)]
pool = b[sections[names][4]:sections[names][4]+sections[names][5]]
def string(p, off):
    end = p.index(0, off)
    return p[off:end].decode()
sec = {string(pool, s[0]):s for s in sections}
start = target = negative = None
for s in sections:
    if s[1] != 2: continue
    strs = sections[s[6]]
    strpool = b[strs[4]:strs[4]+strs[5]]
    for off in range(s[4], s[4]+s[5], 24):
        idx, _, _, _, value, _ = struct.unpack_from('<IBBHQQ', b, off)
        name = string(strpool, idx) if idx else ''
        if name == '_start': start = value
        elif name == 'target': target = value
        elif name == 'negative': negative = value
assert None not in (start, target, negative), 'missing_symbols'
text = sec['.text']
data = b[text[4]:text[4]+text[5]]
base = text[3]
def insn(addr): return struct.unpack_from('<I', data, addr-base)[0]
def sign(v, bits): return (v ^ (1 << (bits-1))) - (1 << (bits-1))
delta = target - start
for offset, kind in ((0, 'adr'), (4, 'literal'), (8, 'cond'),
                     (12, 'cond'), (16, 'test')):
    word = insn(start+offset)
    actual = delta-offset
    if kind == 'adr':
        imm = ((word >> 29) & 3) | (((word >> 5) & 0x7ffff) << 2)
        assert sign(imm, 21) == actual, ('adr', actual, word)
    else:
        bits = 14 if kind == 'test' else 19
        imm = sign((word >> 5) & ((1 << bits)-1), bits)
        assert imm*4 == actual, (kind, actual, word)
    if kind == 'literal': assert (word & 0x3b000000) == 0x18000000
    if kind == 'test': assert (word & 0x7e000000) == 0x36000000
for offset, group, symbol, expected_opcode in (
    (20, 0, target, 0x52800000),
    (24, 0, target, 0x52800000),
    (28, 1, target, 0x72800000),
    (32, 2, target, 0x52800000),
    (36, 3, target, 0x52800000),
    (40, 0, target, 0x52800000),
    (44, 0, negative, 0x12800000),
    (48, 1, negative, 0x12800000)):
    word = insn(start+offset)
    value = ~symbol if symbol == negative else symbol
    assert (word & 0x7f800000) == expected_opcode, (offset, hex(word))
    assert ((word >> 21) & 3) == group
    assert ((word >> 5) & 0xffff) == ((value >> (group*16)) & 0xffff)
print('MINILD_AARCH64_INSTRUCTION_RELOCS=PASS '
      f'file={Path(sys.argv[1]).name} direction={"backward" if delta < 0 else "forward"} '
      'pcrel=5 movw=8')
PY_CHECK
done

# A checked MOVW G0 cannot truncate a 0x123456 symbol address.
if "$MINILD" -maarch64linux -static -T "$work/layout.lds" \
    -o "$work/overflow.elf" "$work/overflow.o" "$work/defs.o" \
    >"$work/overflow.stdout" 2>"$work/overflow.stderr"; then
    echo 'MINILD_AARCH64_INSTRUCTION_RELOCS=FAIL overflow_accepted' >&2
    exit 1
fi
grep -q 'nonriscv-relocation-overflow-or-encoding:183:263' "$work/overflow.stderr"

# All four PC-relative instruction encodings must reject out-of-range
# displacement rather than dropping upper bits and producing an unbootable
# branch. A single fixture can exercise every negative by moving the first
# failing relocation to the head of the stream.
if "$MINILD" -maarch64linux -static -T "$work/layout.lds" \
    -o "$work/far.elf" "$work/far-branch.o" "$work/defs.o" \
    >"$work/far.stdout" 2>"$work/far.stderr"; then
    echo 'MINILD_AARCH64_INSTRUCTION_RELOCS=FAIL far_branch_accepted' >&2
    exit 1
fi
grep -q 'nonriscv-relocation-overflow-or-encoding:183:280' "$work/far.stderr"

# A MOVW G2/G3 relocation encoded into a 32-bit W register is illegal,
# including object files from nonconforming producers. Mutate only the SF bit
# in the signed/unsigned relocation's original object and require fail closed.
python3 - "$work/entry.o" "$work/malformed-w32.o" <<'PY_MUTATE'
from pathlib import Path
import struct
import sys
b = bytearray(Path(sys.argv[1]).read_bytes())
off = struct.unpack_from('<Q', b, 40)[0]
shnum, shstr = struct.unpack_from('<HH', b, 60)
ss = [struct.unpack_from('<IIQQQQIIQQ', b, off+i*64) for i in range(shnum)]
names = b[ss[shstr][4]:ss[shstr][4]+ss[shstr][5]]
for s in ss:
    if names[s[0]:names.index(0,s[0])] == b'.text':
        instr = struct.unpack_from('<I', b, s[4]+32)[0]
        assert (instr & 0x80000000) != 0
        struct.pack_into('<I', b, s[4]+32, instr & ~0x80000000)
        Path(sys.argv[2]).write_bytes(b)
        break
else: raise AssertionError('missing .text')
PY_MUTATE
if "$MINILD" -maarch64linux -static -T "$work/layout.lds" \
    -o "$work/malformed-w32.elf" "$work/malformed-w32.o" "$work/defs.o" \
    >"$work/malformed-w32.stdout" 2>"$work/malformed-w32.stderr"; then
    echo 'MINILD_AARCH64_INSTRUCTION_RELOCS=FAIL w32_group2_accepted' >&2
    exit 1
fi
grep -q 'nonriscv-relocation-overflow-or-encoding:183:267' "$work/malformed-w32.stderr"

if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] && \
    ! command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    echo 'MINILD_AARCH64_INSTRUCTION_RELOCS=FAIL required_gnu_missing' >&2
    exit 1
fi
if command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    for order in forward backward; do
        if [[ $order == forward ]]; then objects=("$work/entry.o" "$work/defs.o")
        else objects=("$work/defs.o" "$work/entry.o"); fi
        aarch64-linux-gnu-ld -maarch64linux -static -T "$work/layout.lds" \
            -o "$work/gnu.$order.elf" "${objects[@]}"
        # Preserve ELF-wide GNU layout freedom; compare the final resolved
        # executable instruction stream rather than unrelated symbol padding.
        python3 - "$work/gnu.$order.elf" "$work/mini.$order.elf" <<'PY_GNU'
from pathlib import Path
import struct
import sys
def text(path):
    b = Path(path).read_bytes()
    off = struct.unpack_from('<Q', b, 40)[0]
    num, strs = struct.unpack_from('<HH', b, 60)
    ss = [struct.unpack_from('<IIQQQQIIQQ', b, off+i*64) for i in range(num)]
    p = b[ss[strs][4]:ss[strs][4]+ss[strs][5]]
    for s in ss:
        end = p.index(0,s[0])
        if p[s[0]:end] == b'.text': return b[s[4]:s[4]+s[5]]
    raise AssertionError('missing .text')
assert text(sys.argv[1]) == text(sys.argv[2]), 'GNU_MiniLD_text_differential'
print('MINILD_AARCH64_INSTRUCTION_GNU=PASS text_byte_exact')
PY_GNU
    done
else
    echo 'MINILD_AARCH64_INSTRUCTION_GNU=SKIP missing_cross_ld'
fi
echo 'MINILD_AARCH64_INSTRUCTION_RELOCS=PASS forward=1 backward=1 negative=1'
