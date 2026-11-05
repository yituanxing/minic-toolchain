#!/usr/bin/env bash
# GNU AAELF64 RELA uses S+A in ADRP/ADD/LDST target-address operands.  The
# relocation's instruction encoding must not veto folding SHF_MERGE strings.
set -Eeuo pipefail

: "${MINIAS:?MINIAS must point to minic-as}"
: "${MINILD:?MINILD must point to minic-ld}"
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-aarch-merge-relocs.XXXXXX")
trap 'rm -r -- "$work"' EXIT
fixtures="$repo/tests/linker/fixtures"
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/main.o" "$fixtures/aarch64-merge-insn-main.S"
"$MINIAS" --target=aarch64 -march=armv8-a -mabi=lp64 \
    -o "$work/duplicate.o" "$fixtures/aarch64-merge-insn-duplicate.S"
readelf -Wr "$work/main.o" >"$work/input.relocs"
grep -Fq R_AARCH64_ADR_PREL_PG_HI21 "$work/input.relocs"
grep -Fq R_AARCH64_ADD_ABS_LO12_NC "$work/input.relocs"
grep -Fq R_AARCH64_LDST8_ABS_LO12_NC "$work/input.relocs"
"$MINILD" -m aarch64linux -static -X \
    -T "$fixtures/aarch64-merge-insn.lds" -o "$work/merged.elf" \
    "$work/main.o" "$work/duplicate.o" >"$work/link.log" 2>&1
if command -v aarch64-linux-gnu-as >/dev/null 2>&1 &&
   command -v aarch64-linux-gnu-ld >/dev/null 2>&1; then
    aarch64-linux-gnu-as -o "$work/gnu.main.o" \
        "$fixtures/aarch64-merge-insn-main.S"
    aarch64-linux-gnu-as -o "$work/gnu.duplicate.o" \
        "$fixtures/aarch64-merge-insn-duplicate.S"
    aarch64-linux-gnu-ld -m aarch64linux -static -X \
        -T "$fixtures/aarch64-merge-insn.lds" -o "$work/gnu.elf" \
        "$work/gnu.main.o" "$work/gnu.duplicate.o"
fi
python3 - "$work/merged.elf" <<'PY'
from pathlib import Path
import struct
import sys

b=Path(sys.argv[1]).read_bytes()
assert b[:6]==b'\x7fELF\x02\x01'
e_shoff=struct.unpack_from('<Q',b,40)[0]
entsize,count,shstr=struct.unpack_from('<HHH',b,58)
sh=[struct.unpack_from('<IIQQQQIIQQ',b,e_shoff+entsize*i) for i in range(count)]
pool=b[sh[shstr][4]:sh[shstr][4]+sh[shstr][5]]
names=lambda entry:pool[entry[0]:pool.index(0,entry[0])].decode()
s={names(entry):entry for entry in sh}
ro=s['.rodata']
data=b[ro[4]:ro[4]+ro[5]]
# Both copies of alpha-message and both standalone suffixes collapse to one.
assert data.count(b'alpha-message\0')==1,data
assert data.count(b'cross-object-unique\0')==1,data
assert data.count(b'message\0')==1,data  # standalone atom folds into suffix
text=s['.text']
t=b[text[4]:text[4]+text[5]]
ad0,add0,ad1,ld1=struct.unpack_from('<IIII',t)
def adrp_destination(insn,place):
    assert insn&0x9f000000==0x90000000,hex(insn)
    imm=((insn>>5)&0x7ffff)<<2 | ((insn>>29)&3)
    if imm&(1<<20):imm-=1<<21
    return (place&~0xfff)+(imm<<12)
def decode_add_lo12(insn):
    assert insn&0xffc00000==0x91000000,hex(insn)
    return (insn>>10)&0xfff
def decode_ldrb_lo12(insn):
    assert insn&0xffc00000==0x39400000,hex(insn)
    return (insn>>10)&0xfff
base=text[3]
addr0=adrp_destination(ad0,base)+decode_add_lo12(add0)
addr1=adrp_destination(ad1,base+8)+decode_ldrb_lo12(ld1)
assert addr0==ro[3]+data.index(b'alpha-message\0'),(hex(addr0),hex(ro[3]))
assert addr1==addr0+6,(hex(addr1),hex(addr0))
assert b[ro[4]+addr1-ro[3]]==ord('m')
print('MINILD_AARCH64_MERGE_INSN=PASS ADRP_ADD_LDST8=resolved '
      'section_RELA_addend_remapped=1 cross_input_fold=1 suffix_fold=1')
PY
if [[ -f "$work/gnu.elf" ]]; then
    python3 - "$work/gnu.elf" "$work/merged.elf" <<'PY_BFD'
import struct,sys
def allocated(path):
    b=open(path,'rb').read()
    off=struct.unpack_from('<Q',b,40)[0]
    entsize,count,idx=struct.unpack_from('<HHH',b,58)
    sh=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*entsize)
        for i in range(count)]
    names=b[sh[idx][4]:sh[idx][4]+sh[idx][5]]
    out={}
    for s in sh:
        name=names[s[0]:names.index(0,s[0])].decode()
        if name in ('.text','.rodata'):
            out[name]=(s[3],b[s[4]:s[4]+s[5]])
    return out
assert allocated(sys.argv[1])==allocated(sys.argv[2])
print('MINILD_AARCH64_MERGE_INSN_GNU=PASS allocated_bytes=exact')
PY_BFD
else
    echo 'MINILD_AARCH64_MERGE_INSN_GNU=SKIP missing_cross_binutils'
fi
