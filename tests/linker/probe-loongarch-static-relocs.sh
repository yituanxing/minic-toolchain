#!/usr/bin/env bash
# LoongArch64 ET_EXEC target-leaf relocation acceptance.  The same MiniAS
# inputs are presented to GNU BFD and MiniLD when cross tools are available.
# GNU/QEMU absence is reported as SKIP, never claimed as runtime PASS.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
MINIAS=${MINIAS:-"$repo/build/release/bin/minic-as"}
MINILD=${MINILD:-"$repo/build/release/bin/minic-ld"}
GNU_LD=${GNU_LD:-loongarch64-linux-gnu-ld.bfd}
QEMU_USER=${QEMU_USER:-qemu-loongarch64}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/control.s" <<'ASM'
.text
.option relax
.globl _start
.type _start,@function
_start:
    beq $r4,$r5,.Lbranch
    beqz $r4,.Lbranch
    bl .Lcall
    la.global $r12,fixture_data
    ld.d $r13,$r12,0
    addi.d $r4,$r13,-123
    b .Lbranch
.Lcall:
    jirl $r0,$r1,0
.Lbranch:
    ori $r11,$r0,93
    syscall 0
.Lend:
.size _start,.-_start
.section .data,"aw",@progbits
.globl fixture_len
fixture_len:
    .long .Lend-_start
.section .rodata,"a",@progbits
.globl fixture_data
fixture_data:
    .quad 123
ASM

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/control.o" "$work/control.s"
readelf -Wr "$work/control.o" >"$work/control.reloc"
for kind in R_LARCH_B16 R_LARCH_B21 R_LARCH_B26 \
            R_LARCH_GOT_PC_HI20 R_LARCH_GOT_PC_LO12; do
    grep -Fq "$kind" "$work/control.reloc" || {
        echo "MINILD_LOONGARCH_STATIC=FAIL missing_reloc:$kind" >&2
        exit 1
    }
done
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/mini.elf" "$work/control.o"

python3 - "$work/control.o" "$work/mini.elf" <<'PY'
from pathlib import Path
import struct
import sys

def load(path):
    data = bytearray(Path(path).read_bytes())
    assert data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data, 18)[0] == 258
    shoff = struct.unpack_from('<Q', data, 40)[0]
    shnum = struct.unpack_from('<H', data, 60)[0]
    shstr = struct.unpack_from('<H', data, 62)[0]
    sec = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i*64)
           for i in range(shnum)]
    strings = data[sec[shstr][4]:sec[shstr][4]+sec[shstr][5]]
    def cstr(pos):
        return strings[pos:strings.find(0,pos)].decode()
    names = {cstr(s[0]): s for s in sec}
    symsec = names['.symtab']; strsec = sec[symsec[6]]
    strs = data[strsec[4]:strsec[4]+strsec[5]]
    symbols = {}
    for off in range(symsec[4],symsec[4]+symsec[5],24):
        name, _, _, index, value, size = struct.unpack_from('<IBBHQQ',data,off)
        if name:
            sym = strs[name:strs.find(0,name)].decode()
            symbols[sym] = (index,value,size)
    return data,names,symbols

raw,secs,symbols = load(sys.argv[1])
linked,sections,syms = load(sys.argv[2])
assert struct.unpack_from('<H',linked,16)[0] == 2, 'not ET_EXEC'
assert '.got' in sections and sections['.got'][5] >= 8, 'no GOT slot'
got = sections['.got']; got_data = linked[got[4]:got[4]+got[5]]
symbol = syms['fixture_data'][1]
assert any(struct.unpack_from('<Q',got_data,i)[0] == symbol
           for i in range(0,len(got_data),8)), ('GOT does not contain S',symbol)
fixture = syms['fixture_len'][1]; data_sec = sections['.data']
actual_len = struct.unpack_from('<I',linked,data_sec[4]+fixture-data_sec[3])[0]
assert actual_len == syms['_start'][2], (actual_len,syms['_start'][2])
text = sections['.text']; pc = syms['_start'][1]
insn = struct.unpack_from('<I',linked,text[4]+pc-text[3])[0]
assert insn & 0xfc000000 == 0x58000000, ('B16 opcode',hex(insn))
assert insn & 0x03fffc00 != 0, 'unpatched B16 displacement'
print('MINILD_LOONGARCH_STATIC=PASS B16_B21_B26=3 GOT_PC_PAIR=1 GOT_value=resolved ADD32_SUB32=value')
PY

# ADD/SUB with genuine MiniAS CFI is exercised by the five-stage GCC ladder;
# the synthetic .data symbol difference is checked when the assembler emits
# its pair rather than folds the constant locally.
if grep -Fq R_LARCH_ADD32 "$work/control.reloc"; then
    grep -Fq R_LARCH_SUB32 "$work/control.reloc"
    echo 'MINILD_LOONGARCH_STATIC=PASS ADD32_SUB32=1'
else
    echo 'MINILD_LOONGARCH_STATIC=INFO ADD32_SUB32=covered_by_gcc_ladder_artifact'
fi

# A target instruction relocation cannot legally be patched in arbitrary
# writable data.  Mutate an ordinary R_LARCH_64 .data relocation to the GOT
# PC high relocation and insist on a rejected output.
cat >"$work/reject.s" <<'ASM'
.text
.globl _start
_start:
    jirl $r0,$r1,0
.data
    .quad _start
ASM
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/reject.o" "$work/reject.s"
python3 - "$work/reject.o" <<'PY'
import struct,sys
from pathlib import Path
p=Path(sys.argv[1]); raw=bytearray(p.read_bytes())
shoff=struct.unpack_from('<Q',raw,40)[0]
shnum=struct.unpack_from('<H',raw,60)[0]
count=0
for i in range(shnum):
    s=struct.unpack_from('<IIQQQQIIQQ',raw,shoff+64*i)
    if s[1]!=4: continue
    for off in range(s[4],s[4]+s[5],24):
        info=struct.unpack_from('<Q',raw,off+8)[0]
        if (info&0xffffffff)==2:
            struct.pack_into('<Q',raw,off+8,(info&~0xffffffff)|75)
            count+=1
assert count==1,count
p.write_bytes(raw)
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/rejected.elf" "$work/reject.o" >"$work/rejected.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL accepted_instruction_reloc_in_data' >&2
    exit 1
fi
grep -Fq loongarch-instruction-reloc-in-nonexec "$work/rejected.log"
echo 'MINILD_LOONGARCH_STATIC=PASS got_mode_leak=rejected'

cp "$work/control.o" "$work/bad-opcode.o"
python3 - "$work/bad-opcode.o" <<'PY'
import struct,sys
from pathlib import Path
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
shoff=struct.unpack_from('<Q',b,40)[0]
num=struct.unpack_from('<H',b,60)[0]
sections=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+i*64)
          for i in range(num)]
patched=0
for sec in sections:
    if sec[1] != 4: continue
    target=sections[sec[7]]
    for at in range(sec[4],sec[4]+sec[5],24):
        off,info,_=struct.unpack_from('<QQq',b,at)
        if info&0xffffffff==75:
            wordoff=target[4]+off
            old=struct.unpack_from('<I',b,wordoff)[0]
            assert old&0xfe000000==0x1a000000,hex(old)
            struct.pack_into('<I',b,wordoff,0)
            patched+=1
assert patched==1,patched
p.write_bytes(b)
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/bad-opcode.elf" "$work/bad-opcode.o" \
    >"$work/bad-opcode.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL accepted_invalid_GOT_pc_opcode' >&2
    exit 1
fi
grep -Fq 'nonriscv-relocation-overflow-or-encoding:258:75' \
    "$work/bad-opcode.log"
echo 'MINILD_LOONGARCH_STATIC=PASS invalid_GOT_opcode=rejected'

# MiniAS currently has typed PCALA/GOT producer fixups, but not a TLS-specific
# surface syntax.  The assembler emits a real STT_TLS/.tdata definition and
# real four-byte words; change only each relocation type (and the one lu12i.w
# opcode) to make a legal psABI LE and IE relocation pair.
cat >"$work/tls.s" <<'ASM'
.text
.globl _start
_start:
    la.local $r12,tls_value
    la.global $r13,tls_value
    la.global $r14,missing_weak_tls
    ori $r4,$r0,0
    ori $r11,$r0,93
    syscall 0
.section .tdata,"awT",@progbits
.p2align 3
.globl tls_prefix
.type tls_prefix,@tls_object
tls_prefix:
    .quad 9
.globl tls_value
.type tls_value,@tls_object
tls_value:
    .quad 123
.weak missing_weak_tls
.type missing_weak_tls,@tls_object
ASM
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/tls.o" "$work/tls.s"
python3 - "$work/tls.o" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
shoff=struct.unpack_from('<Q',b,40)[0]
num=struct.unpack_from('<H',b,60)[0]
sects=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+i*64)
       for i in range(num)]
seen=[]
for sec in sects:
    if sec[1] != 4: continue
    target=sects[sec[7]]
    for off in range(sec[4],sec[4]+sec[5],24):
        rel_offset, info, addend = struct.unpack_from('<QQq',b,off)
        typ=info&0xffffffff
        if typ==71:
            new=83
            wordoff=target[4]+rel_offset
            word=struct.unpack_from('<I',b,wordoff)[0]
            assert word&0xfe000000==0x1a000000,hex(word)
            struct.pack_into('<I',b,wordoff,(word&~0xfe000000)|0x14000000)
        elif typ==72: new=84
        elif typ==75: new=87
        elif typ==76: new=88
        else: continue
        struct.pack_into('<Q',b,off+8,(info&~0xffffffff)|new)
        seen.append(new)
assert sorted(seen)==[83,84,87,87,88,88],seen
p.write_bytes(b)
PY
readelf -Wr "$work/tls.o" >"$work/tls.reloc"
for name in R_LARCH_TLS_LE_HI20 R_LARCH_TLS_LE_LO12 \
            R_LARCH_TLS_IE_PC_HI20 R_LARCH_TLS_IE_PC_LO12; do
    grep -Fq "$name" "$work/tls.reloc"
done
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/tls-mini.elf" "$work/tls.o"
python3 - "$work/tls-mini.elf" <<'PY'
import struct,sys
from pathlib import Path
b=Path(sys.argv[1]).read_bytes()
phoff=struct.unpack_from('<Q',b,32)[0]
phnum=struct.unpack_from('<H',b,56)[0]
ph=[struct.unpack_from('<IIQQQQQQ',b,phoff+i*56)
    for i in range(phnum)]
tls=[p for p in ph if p[0]==7]
assert len(tls)==1 and tls[0][6]>=8,('no PT_TLS',tls)
shoff=struct.unpack_from('<Q',b,40)[0]
num=struct.unpack_from('<H',b,60)[0]
si=struct.unpack_from('<H',b,62)[0]
sec=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+i*64)
     for i in range(num)]
st=sec[si]; strings=b[st[4]:st[4]+st[5]]
def name(i):
    n=sec[i][0]; return strings[n:strings.index(0,n)].decode()
got=[sec[i] for i in range(num) if name(i)=='.got']
assert len(got)==1 and got[0][5]>=16,('missing defined+weak TLS IE GOT',got)
vals=[struct.unpack_from('<Q',b,got[0][4]+i)[0]
      for i in range(0,got[0][5],8)]
assert vals[:2]==[8,0],('defined TLS TPREL=8, weak undefined TLS TPREL=0',vals)
textsec=next(sec[i] for i in range(num) if name(i)=='.text')
le_low12=struct.unpack_from('<I',b,textsec[4]+4)[0]
assert ((le_low12>>10)&0xfff)==8,('local-exec encoded TPREL low12',hex(le_low12))
print('MINILD_LOONGARCH_STATIC=PASS TLS_LE_IE=2 PT_TLS=1 LE_TPREL=8 IE_GOT_TPREL=8 weak_undef_tls=0')
PY

# A weak undefined TLS symbol is legal in a static GOT, but that exception
# must never admit a weak non-TLS symbol through a TLS relocation.  Mutate
# only its symbol type, preserving the already valid IE relocation pair.
cp "$work/tls.o" "$work/tls-nontls.o"
python3 - "$work/tls-nontls.o" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
shoff=struct.unpack_from('<Q',b,40)[0]
num=struct.unpack_from('<H',b,60)[0]
ss=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+64*i) for i in range(num)]
symtab=next(s for s in ss if s[1]==2)
strtab=ss[symtab[6]]
strings=b[strtab[4]:strtab[4]+strtab[5]]
patched=0
for at in range(symtab[4],symtab[4]+symtab[5],24):
    name,info,other,shndx,value,size=struct.unpack_from('<IBBHQQ',b,at)
    if name and strings[name:strings.find(0,name)]==b'missing_weak_tls':
        assert info>>4==2 and info&15==6 and shndx==0
        b[at+4]=info&0xf0
        patched+=1
assert patched==1,patched
p.write_bytes(b)
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/tls-nontls.elf" "$work/tls-nontls.o" \
    >"$work/tls-nontls.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL admitted_weak_nontls_as_tls' >&2
    exit 1
fi
grep -Fq invalid-loongarch-static-got-reloc "$work/tls-nontls.log"
echo 'MINILD_LOONGARCH_STATIC=PASS weak_undef_tls=zero weak_undef_nontls=fail_closed'

# R_LARCH_ALIGN must delete unnecessary NOPs while preserving symbol
# addresses and instruction fixups.  The historical failure here was the
# missing target relaxation hook, not a legal fail-closed output.
cat >"$work/align.s" <<'ASM'
.text
.option relax
.globl _start
_start:
    la.local $r4,after_align
    .p2align 4
after_align:
    jirl $r0,$r1,0
ASM
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/align.o" "$work/align.s"
grep -Fq R_LARCH_ALIGN <(readelf -Wr "$work/align.o")
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/align.elf" "$work/align.o"
python3 - "$work/align.o" "$work/align.elf" <<'PY'
from pathlib import Path
import struct,sys

def load(path):
    b=Path(path).read_bytes()
    off=struct.unpack_from('<Q',b,40)[0]
    num=struct.unpack_from('<H',b,60)[0]
    names_index=struct.unpack_from('<H',b,62)[0]
    sec=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i)
         for i in range(num)]
    tab=sec[names_index]; strings=b[tab[4]:tab[4]+tab[5]]
    def name(s):
        p=s[0]; return strings[p:strings.index(0,p)].decode()
    sections={name(s):s for s in sec}
    sym=sections['.symtab']; strings_section=sec[sym[6]]
    st=b[strings_section[4]:strings_section[4]+strings_section[5]]
    symbols={}
    for pos in range(sym[4],sym[4]+sym[5],24):
        p,_,_,_,value,_=struct.unpack_from('<IBBHQQ',b,pos)
        if p: symbols[st[p:st.index(0,p)].decode()]=value
    return sections,symbols

raw,raw_symbols=load(sys.argv[1]); exe,exe_symbols=load(sys.argv[2])
assert exe['.text'][5]==raw['.text'][5]-4, ('NOP shrink',raw['.text'][5],exe['.text'][5])
assert (exe_symbols['after_align']-exe['.text'][3])%16==0, 'misaligned label'
assert exe_symbols['after_align']-exe['.text'][3]==16, 'wrong aligned symbol'
print('MINILD_LOONGARCH_STATIC=PASS mandatory_ALIGN_shrink=4 symbol_aligned=16')
PY

# Two independent alignment sites affect the branch relocation and function
# size simultaneously.  Test source is also independently linked/run by GNU
# BFD + QEMU below; MiniAS is deliberately the common input producer.
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/two-aligns.o" \
    "$repo/tests/linker/fixtures/loongarch-align-two-shrinks.S"
test "$(readelf -Wr "$work/two-aligns.o" | grep -c R_LARCH_ALIGN)" -eq 2
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/two-aligns-mini.elf" "$work/two-aligns.o"
python3 - "$work/two-aligns.o" "$work/two-aligns-mini.elf" <<'PY'
from pathlib import Path
import struct,sys

def load(path):
    b=Path(path).read_bytes()
    assert b[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',b,18)[0]==258
    off=struct.unpack_from('<Q',b,40)[0]
    n=struct.unpack_from('<H',b,60)[0]
    si=struct.unpack_from('<H',b,62)[0]
    secs=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(n)]
    names=b[secs[si][4]:secs[si][4]+secs[si][5]]
    def name(s):
        p=s[0]; return names[p:names.index(0,p)].decode()
    sections={name(s):s for s in secs}
    sym=sections['.symtab']; st=secs[sym[6]]
    strings=b[st[4]:st[4]+st[5]]; symbols={}
    for p in range(sym[4],sym[4]+sym[5],24):
        idx,info,_,_,value,size=struct.unpack_from('<IBBHQQ',b,p)
        if idx: symbols[strings[idx:strings.index(0,idx)].decode()]=(value,size,info&15)
    return b,sections,symbols

raw,raw_sec,raw_sym=load(sys.argv[1]); b,sec,sym=load(sys.argv[2])
text=sec['.text']; old=raw_sec['.text']
assert old[5]==52 and text[5]==44, ('two shrink events',old[5],text[5])
assert text[3]%16==0 and sym['aligned_one'][0]-text[3]==16
assert sym['aligned_two'][0]-text[3]==32
assert sym['_start']==(text[3],44,2), ('function size must track shrink',sym['_start'])
insn=struct.unpack_from('<I',b,text[4])[0]
assert insn&0xfc000000==0x50000000
disp=((insn>>10)&0xffff)|((insn&0x3ff)<<16)
assert disp<<2==32, ('B26 relocation should target shrunk aligned_two',hex(insn))
phoff=struct.unpack_from('<Q',b,32)[0]
phnum=struct.unpack_from('<H',b,56)[0]
assert any(struct.unpack_from('<I',b,phoff+i*56)[0]==1 for i in range(phnum)), 'no PT_LOAD'
print('MINILD_LOONGARCH_STATIC=PASS ALIGN_two_shrinks=8 remapped_symbols=2 branch_B26=32 function_size=44')
PY

# Corrupt one assembler-reserved NOP.  An invalid R_LARCH_ALIGN must never be
# silently removed by a generic no-op relocation filter.
cp "$work/two-aligns.o" "$work/two-aligns-bad-nop.o"
python3 - "$work/two-aligns-bad-nop.o" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
secs=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(n)]
patched=0
for sec in secs:
    if sec[1]!=4: continue
    text=secs[sec[7]]
    for pos in range(sec[4],sec[4]+sec[5],24):
        at,info,addend=struct.unpack_from('<QQq',b,pos)
        if info&0xffffffff==102:
            start=text[4]+at
            assert struct.unpack_from('<I',b,start)[0]==0x03400000
            struct.pack_into('<I',b,start,0)
            patched+=1
            break
    if patched: break
assert patched==1,patched
p.write_bytes(b)
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/two-aligns-bad-nop.elf" "$work/two-aligns-bad-nop.o" \
    >"$work/two-aligns-bad-nop.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL accepted_malformed_ALIGN_NOP' >&2
    exit 1
fi
grep -Fq invalid-loongarch-align-nop "$work/two-aligns-bad-nop.log"
echo 'MINILD_LOONGARCH_STATIC=PASS malformed_ALIGN_NOP=rejected'

# A second alignment relocation cannot claim bytes inside a preceding NOP
# reservation; otherwise the linker would consume bytes twice.
cp "$work/two-aligns.o" "$work/two-aligns-overlap.o"
python3 - "$work/two-aligns-overlap.o" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
secs=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(n)]
aligns=[]
for sec in secs:
    if sec[1]==4:
        aligns += [pos for pos in range(sec[4],sec[4]+sec[5],24)
                   if struct.unpack_from('<Q',b,pos+8)[0]&0xffffffff==102]
assert len(aligns)==2,aligns
struct.pack_into('<Q',b,aligns[1],12)
p.write_bytes(b)
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/two-aligns-overlap.elf" "$work/two-aligns-overlap.o" \
    >"$work/two-aligns-overlap.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL accepted_overlapping_ALIGN_sites' >&2
    exit 1
fi
grep -Fq loongarch-align-overlap "$work/two-aligns-overlap.log"
echo 'MINILD_LOONGARCH_STATIC=PASS overlapping_ALIGN_sites=rejected'

# A named live symbol in the removable tail of NOPs cannot be projected to
# an arbitrary address.  Leave it fatal instead of fabricating its meaning.
cp "$work/two-aligns.o" "$work/two-aligns-deleted-symbol.o"
python3 - "$work/two-aligns-deleted-symbol.o" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
secs=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(n)]
sym=next(s for s in secs if s[1]==2)
st=secs[sym[6]]; names=b[st[4]:st[4]+st[5]]
changed=0
for pos in range(sym[4],sym[4]+sym[5],24):
    idx=struct.unpack_from('<I',b,pos)[0]
    if idx and names[idx:names.index(0,idx)]==b'aligned_one':
        assert struct.unpack_from('<Q',b,pos+8)[0]==20
        struct.pack_into('<Q',b,pos+8,18)
        changed+=1
assert changed==1,changed
p.write_bytes(b)
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/two-aligns-deleted-symbol.elf" "$work/two-aligns-deleted-symbol.o" \
    >"$work/two-aligns-deleted-symbol.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL accepted_symbol_in_deleted_ALIGN_NOP' >&2
    exit 1
fi
grep -Fq loongarch-align-deleted-symbol "$work/two-aligns-deleted-symbol.log"
echo 'MINILD_LOONGARCH_STATIC=PASS symbol_in_deleted_ALIGN_NOP=rejected'

# BFD RELOCATE_CALC_PC32_HI20 differs from the 64-bit four-instruction
# pcalau12i adjustment.  At the low12=0x800 sign-extension boundary, a
# PCALA_HI20 pair must carry +0x1000, NOT subtract 0x100000000 (which would
# spuriously overflow a perfectly near target in real glibc objects).
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/pcala-rounding.o" \
    "$repo/tests/linker/fixtures/loongarch-pcala-page-rounding.S"
test "$(readelf -Wr "$work/pcala-rounding.o" | grep -c R_LARCH_PCALA_HI20)" -eq 2
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/pcala-rounding-mini.elf" "$work/pcala-rounding.o"
"$MINILD" -m elf64loongarch -static -e _start \
    -z max-page-size=16384 -o "$work/pcala-rounding-explicit-16k.elf" \
    "$work/pcala-rounding.o"
cmp "$work/pcala-rounding-mini.elf" \
    "$work/pcala-rounding-explicit-16k.elf"
"$MINILD" -m elf64loongarch -static -e _start \
    -z max-page-size=65536 -o "$work/pcala-rounding-explicit-64k.elf" \
    "$work/pcala-rounding.o"
python3 - "$work/pcala-rounding-mini.elf" \
    "$work/pcala-rounding-explicit-64k.elf" <<'PY'
from pathlib import Path
import struct,sys
for path, page, expected_hi20 in (
        (sys.argv[1], 0x4000, (4, 5)),
        (sys.argv[2], 0x10000, (16, 17))):
    b=Path(path).read_bytes()
    off=struct.unpack_from('<Q',b,40)[0]
    n=struct.unpack_from('<H',b,60)[0]
    si=struct.unpack_from('<H',b,62)[0]
    secs=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*64) for i in range(n)]
    st=secs[si]; names=b[st[4]:st[4]+st[5]]
    def name(s):
        i=s[0]; return names[i:names.index(0,i)].decode()
    sections={name(s):s for s in secs}
    sym=sections['.symtab']; stab=secs[sym[6]]
    strings=b[stab[4]:stab[4]+stab[5]]
    symbols={}
    for pos in range(sym[4],sym[4]+sym[5],24):
        idx,_,_,_,value,_=struct.unpack_from('<IBBHQQ',b,pos)
        if idx: symbols[strings[idx:strings.index(0,idx)].decode()]=value
    text=sections['.text']
    assert sections['.data'][3]-text[3]==page, (path,sections['.data'])
    assert symbols['pcala_low_7ff']&0xfff==0x7ff
    assert symbols['pcala_low_800']&0xfff==0x800
    words=[struct.unpack_from('<I',b,text[4]+p)[0] for p in (0,4,8,12)]
    assert words[0]&0xfe000000==0x1a000000
    assert words[2]&0xfe000000==0x1a000000
    for word, site, label, expected in (
            (words[0],0,'pcala_low_7ff',expected_hi20[0]),
            (words[2],8,'pcala_low_800',expected_hi20[1])):
        target=symbols[label]
        place=text[3]+site
        # BFD RELOCATE_CALC_PC32_HI20 subtracts the 4K-rounded PC and
        # rounds up exactly when the target's low12 has its sign bit set.
        delta=((target&~0xfff)-(place&~0xfff) +
               (0x1000 if target&0x800 else 0))
        assert ((word>>5)&0xfffff)==((delta>>12)&0xfffff)==expected, (
            label,hex(word),hex(target),hex(place),hex(delta))
    assert (words[1]>>10)&0xfff==0x7ff
    assert (words[3]>>10)&0xfff==0x800
print('MINILD_LOONGARCH_STATIC=PASS PCALA_HI20_page_rounding '
      'low12=7ff/800 GNU_default_16KiB_hi20=4/5 '
      'explicit_16KiB=byte_exact explicit_64KiB_hi20=16/17')
PY

# The new width-specific calculation must not weaken opcode validation.
cp "$work/pcala-rounding.o" "$work/pcala-rounding-bad-opcode.o"
python3 - "$work/pcala-rounding-bad-opcode.o" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
sections=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*64) for i in range(n)]
text=next(s for s in sections if s[1]==1 and s[2]&4)
assert struct.unpack_from('<I',b,text[4])[0]&0xfe000000==0x1a000000
struct.pack_into('<I',b,text[4],0)
p.write_bytes(b)
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/pcala-rounding-bad-opcode.elf" "$work/pcala-rounding-bad-opcode.o" \
    >"$work/pcala-rounding-bad-opcode.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL accepted_malformed_PCALA_opcode' >&2
    exit 1
fi
grep -Fq 'nonriscv-relocation-overflow-or-encoding:258:71' \
    "$work/pcala-rounding-bad-opcode.log"
echo 'MINILD_LOONGARCH_STATIC=PASS malformed_PCALA_HI20_opcode=rejected'

# In a static ET_EXEC, a defined weak is PC-relative but an *undefined* weak
# resolves to S+A=0.  GNU BFD rewrites its PCALA_HI20 pcalau12i to lu12i.w;
# otherwise even a plain address-zero optional function incorrectly overflows
# from the LoongArch default image VMA 0x120000000.
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/pcala-weak.o" \
    "$repo/tests/linker/fixtures/loongarch-pcala-undefweak.S"
grep -Eq 'FUNC[[:space:]]+WEAK[[:space:]]+DEFAULT[[:space:]]+UND[[:space:]]+optional_weak' \
    <(readelf -Ws "$work/pcala-weak.o")
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/pcala-weak-mini.elf" "$work/pcala-weak.o"
python3 - "$work/pcala-weak-mini.elf" <<'PY'
from pathlib import Path
import struct,sys
b=Path(sys.argv[1]).read_bytes()
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
si=struct.unpack_from('<H',b,62)[0]
sections=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(n)]
shstr=sections[si]; names=b[shstr[4]:shstr[4]+shstr[5]]
def name(s):
    p=s[0]; return names[p:names.index(0,p)].decode()
text=next(s for s in sections if name(s)=='.text')
hi,lo=struct.unpack_from('<II',b,text[4])
assert hi==0x1400000c, ('undefined weak becomes LU12I.W target zero',hex(hi))
assert lo==0x02c0018c, ('undefined weak LO12 zero',hex(lo))
print('MINILD_LOONGARCH_STATIC=PASS PCALA_HI20_undefweak=absolute_lu12i_zero')
PY

# GNU glibc also uses PCALA_LO12 with a JIRL (function call), not just a K12
# addi.d/ld.d.  Emit the relocation via MiniAS, substitute only its consumer
# instruction into JIRL and leave the zero-address weak call unreachable.
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/pcala-weak-jirl.o" \
    "$repo/tests/linker/fixtures/loongarch-pcala-weak-jirl.S"
python3 - "$work/pcala-weak-jirl.o" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
sections=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*64) for i in range(n)]
text=next(s for s in sections if s[1]==1 and s[2]&4)
assert struct.unpack_from('<I',b,text[4]+8)[0]&0xffc00000==0x02c00000
struct.pack_into('<I',b,text[4]+8,0x4c000181)
p.write_bytes(b)
PY
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/pcala-weak-jirl-mini.elf" "$work/pcala-weak-jirl.o"
python3 - "$work/pcala-weak-jirl-mini.elf" <<'PY'
from pathlib import Path
import struct,sys
b=Path(sys.argv[1]).read_bytes()
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
text=next(s for i in range(n)
          if (s:=struct.unpack_from('<IIQQQQIIQQ',b,off+i*64))[1]==1
          and s[2]&4)
hi,lo=struct.unpack_from('<II',b,text[4]+4)
assert hi==0x1400000c,('weak PCALA HI20 becomes absolute LU12I.W',hex(hi))
assert lo==0x4c000181,('weak PCALA LO12 JIRL immediate zero',hex(lo))
print('MINILD_LOONGARCH_STATIC=PASS PCALA_LO12_undefweak_JIRL=zero')
PY

# GNU BFD resolves direct b/bl to an undefined weak FUNC by replacing each
# call instruction with JIRL through $r0 (address zero); a PC-relative B26
# displacement from ET_EXEC's high image VMA would overflow signed 28 bits.
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/b26-weak.o" \
    "$repo/tests/linker/fixtures/loongarch-b26-undefweak.S"
test "$(readelf -Wr "$work/b26-weak.o" | grep -c R_LARCH_B26)" -eq 2
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/b26-weak-mini.elf" "$work/b26-weak.o"
python3 - "$work/b26-weak-mini.elf" <<'PY'
from pathlib import Path
import struct,sys
b=Path(sys.argv[1]).read_bytes()
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
si=struct.unpack_from('<H',b,62)[0]
sections=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(n)]
shstr=sections[si]; names=b[shstr[4]:shstr[4]+shstr[5]]
def name(s):
    p=s[0]; return names[p:names.index(0,p)].decode()
text=next(s for s in sections if name(s)=='.text')
branch,weak_b,weak_bl=struct.unpack_from('<III',b,text[4])
assert branch&0xfc000000==0x50000000, 'live local branch remains b'
assert weak_b==0x4c000000, ('weak b becomes JIRL $r0,$r0,0',hex(weak_b))
assert weak_bl==0x4c000001, ('weak bl becomes JIRL $r1,$r0,0',hex(weak_bl))
print('MINILD_LOONGARCH_STATIC=PASS B26_undefweak=JIRL_r0_r1_zero')
PY

# Even an undefined-weak call must first have a legal B26 instruction.
cp "$work/b26-weak.o" "$work/b26-weak-bad-opcode.o"
python3 - "$work/b26-weak-bad-opcode.o" <<'PY'
from pathlib import Path
import struct,sys
p=Path(sys.argv[1]); b=bytearray(p.read_bytes())
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
sections=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(n)]
text=next(s for s in sections if s[1]==1 and s[2]&4)
assert struct.unpack_from('<I',b,text[4]+4)[0]&0xfc000000==0x50000000
struct.pack_into('<I',b,text[4]+4,0)
p.write_bytes(b)
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/b26-weak-bad-opcode.elf" "$work/b26-weak-bad-opcode.o" \
    >"$work/b26-weak-bad-opcode.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL accepted_malformed_B26_weak_opcode' >&2
    exit 1
fi
grep -Fq 'nonriscv-relocation-overflow-or-encoding:258:66' \
    "$work/b26-weak-bad-opcode.log"
echo 'MINILD_LOONGARCH_STATIC=PASS malformed_B26_weak_opcode=rejected'

# GNU-as glibc objects may attach PCALA_LO12 to any LoongArch K12 consumer,
# including prefetch, FP and LSX/LASX loads/stores.  MiniAS's scalar producer
# does not expose symbolic FP/vector operands yet, so preserve a real MiniAS
# PCALA pair and mutate only the instruction opcode as an ELF input fixture.
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/pcala-k12.o" \
    "$repo/tests/linker/fixtures/loongarch-pcala-k12-consumers.S"
grep -Fq R_LARCH_PCALA_LO12 <(readelf -Wr "$work/pcala-k12.o")
python3 - "$work/pcala-k12.o" "$work" <<'PY'
from pathlib import Path
import struct,sys
b=bytearray(Path(sys.argv[1]).read_bytes())
off=struct.unpack_from('<Q',b,40)[0]
n=struct.unpack_from('<H',b,60)[0]
ss=[struct.unpack_from('<IIQQQQIIQQ',b,off+i*64) for i in range(n)]
text=next(s for s in ss if s[1]==1 and s[2]&4)
place=text[4]+8
word=struct.unpack_from('<I',b,place)[0]
assert word&0xffc00000==0x02c00000,hex(word)
consumers={'preld':0x2ac00000,'fld_s':0x2b000000,
           'fst_s':0x2b400000,'fld_d':0x2b800000,
           'fst_d':0x2bc00000,'vld':0x2c000000,
           'vst':0x2c400000,'xvld':0x2c800000,
           'xvst':0x2cc00000,'invalid':0x00000000}
for name,opcode in consumers.items():
    obj=bytearray(b)
    struct.pack_into('<I',obj,place,(word&0x003fffff)|opcode)
    Path(sys.argv[2],f'pcala-k12-{name}.o').write_bytes(obj)
PY
for consumer in preld fld_s fst_s fld_d fst_d vld vst xvld xvst; do
    "$MINILD" -m elf64loongarch -static -e _start \
        -o "$work/pcala-k12-$consumer-mini.elf" \
        "$work/pcala-k12-$consumer.o"
done
python3 - "$work" <<'PY'
from pathlib import Path
import struct,sys
work=Path(sys.argv[1])
ops={'preld':0x2ac00000,'fld_s':0x2b000000,
     'fst_s':0x2b400000,'fld_d':0x2b800000,
     'fst_d':0x2bc00000,'vld':0x2c000000,
     'vst':0x2c400000,'xvld':0x2c800000,
     'xvst':0x2cc00000}
for name,op in ops.items():
    b=(work/f'pcala-k12-{name}-mini.elf').read_bytes()
    off=struct.unpack_from('<Q',b,40)[0]
    n=struct.unpack_from('<H',b,60)[0]
    si=struct.unpack_from('<H',b,62)[0]
    ss=[struct.unpack_from('<IIQQQQIIQQ',b,off+64*i) for i in range(n)]
    tab=ss[si]; names=b[tab[4]:tab[4]+tab[5]]
    def sname(s):
        p=s[0]; return names[p:names.index(0,p)].decode()
    text=next(s for s in ss if sname(s)=='.text')
    insn=struct.unpack_from('<I',b,text[4]+8)[0]
    assert insn&0xffc00000==op,(name,hex(insn))
    assert (insn>>10)&0xfff==0x808,(name,hex(insn))
print('MINILD_LOONGARCH_STATIC=PASS PCALA_LO12_K12_consumer_families=9')
PY
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/pcala-k12-invalid.elf" "$work/pcala-k12-invalid.o" \
    >"$work/pcala-k12-invalid.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL accepted_PCALA_LO12_non_K12_opcode' >&2
    exit 1
fi
grep -Fq 'nonriscv-relocation-overflow-or-encoding:258:72' \
    "$work/pcala-k12-invalid.log"
echo 'MINILD_LOONGARCH_STATIC=PASS malformed_PCALA_LO12_non_K12=rejected'

cat >"$work/align-ok.s" <<'ASM'
.text
.option relax
.globl _start
_start:
    nop
    .p2align 4
aligned:
    ori $r4,$r0,0
    ori $r11,$r0,93
    syscall 0
ASM
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/align-ok.o" "$work/align-ok.s"
grep -Fq R_LARCH_ALIGN <(readelf -Wr "$work/align-ok.o")
"$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/align-ok.elf" "$work/align-ok.o"
python3 - "$work/align-ok.elf" <<'PY'
from pathlib import Path
import struct,sys
b=Path(sys.argv[1]).read_bytes()
shoff=struct.unpack_from('<Q',b,40)[0]
num=struct.unpack_from('<H',b,60)[0]
index=struct.unpack_from('<H',b,62)[0]
s=[struct.unpack_from('<IIQQQQIIQQ',b,shoff+i*64) for i in range(num)]
st=s[index]; data=b[st[4]:st[4]+st[5]]
def n(i):
    off=s[i][0]; return data[off:data.index(0,off)].decode()
t=[s[i] for i in range(num) if n(i)=='.text']
assert len(t)==1 and t[0][3]%16==0 and t[0][5]>=16,t
assert b[t[0][4]+4:t[0][4]+16]==bytes.fromhex('00004003')*3
print('MINILD_LOONGARCH_STATIC=PASS ALIGN_preserved_nops=3 aligned_boundary=16')
PY

# An optional local smoke run may skip cross tools, but the CI contract
# requires independent GNU BFD and QEMU oracles.  Do not silently downgrade
# an explicitly required runtime comparison into a structural-only PASS.
if [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]] && \
   ! command -v "$GNU_LD" >/dev/null 2>&1; then
    echo "MINILD_LOONGARCH_STATIC=FAIL required_gnu_oracle_missing:$GNU_LD" >&2
    exit 1
fi
if [[ ${MINILD_REQUIRE_QEMU:-0} == 1 ]] && \
   ! command -v "$QEMU_USER" >/dev/null 2>&1; then
    echo "MINILD_LOONGARCH_STATIC=FAIL required_qemu_missing:$QEMU_USER" >&2
    exit 1
fi
if command -v "$GNU_LD" >/dev/null && command -v "$QEMU_USER" >/dev/null; then
    "$GNU_LD" -m elf64loongarch -static -e _start \
        -o "$work/gnu.elf" "$work/control.o"
    "$GNU_LD" -m elf64loongarch -static -e _start \
        -o "$work/two-aligns-gnu.elf" "$work/two-aligns.o"
    "$GNU_LD" -m elf64loongarch -static -e _start \
        -o "$work/pcala-rounding-gnu.elf" "$work/pcala-rounding.o"
    "$GNU_LD" -m elf64loongarch -static -e _start \
        -o "$work/pcala-weak-gnu.elf" "$work/pcala-weak.o"
    "$GNU_LD" -m elf64loongarch -static -e _start \
        -o "$work/pcala-weak-jirl-gnu.elf" "$work/pcala-weak-jirl.o"
    "$GNU_LD" -m elf64loongarch -static -e _start \
        -o "$work/b26-weak-gnu.elf" "$work/b26-weak.o"
    for consumer in preld fld_s fst_s fld_d fst_d vld vst xvld xvst; do
        "$GNU_LD" -m elf64loongarch -static -e _start \
            -o "$work/pcala-k12-$consumer-gnu.elf" \
            "$work/pcala-k12-$consumer.o"
    done
    mini_rc=0; gnu_rc=0
    timeout 15s "$QEMU_USER" "$work/mini.elf" >"$work/mini.out" \
        2>"$work/mini.err" || mini_rc=$?
    timeout 15s "$QEMU_USER" "$work/gnu.elf" >"$work/gnu.out" \
        2>"$work/gnu.err" || gnu_rc=$?
    test "$mini_rc" -eq 0 && test "$gnu_rc" -eq 0
    cmp "$work/mini.out" "$work/gnu.out"
    cmp "$work/mini.err" "$work/gnu.err"
    echo 'MINILD_LOONGARCH_STATIC=PASS GNU_BFD_QEMU_runtime_parity=1'
    mini_align_rc=0; gnu_align_rc=0
    timeout 15s "$QEMU_USER" "$work/two-aligns-mini.elf" \
        >"$work/two-aligns-mini.out" 2>"$work/two-aligns-mini.err" || mini_align_rc=$?
    timeout 15s "$QEMU_USER" "$work/two-aligns-gnu.elf" \
        >"$work/two-aligns-gnu.out" 2>"$work/two-aligns-gnu.err" || gnu_align_rc=$?
    test "$mini_align_rc" -eq 0 && test "$gnu_align_rc" -eq 0
    cmp "$work/two-aligns-mini.out" "$work/two-aligns-gnu.out"
    cmp "$work/two-aligns-mini.err" "$work/two-aligns-gnu.err"
    echo 'MINILD_LOONGARCH_STATIC=PASS GNU_BFD_QEMU_ALIGN_two_shrink_runtime_parity=1'
    mini_pcala_rc=0; gnu_pcala_rc=0
    timeout 15s "$QEMU_USER" "$work/pcala-rounding-mini.elf" \
        >"$work/pcala-rounding-mini.out" 2>"$work/pcala-rounding-mini.err" || mini_pcala_rc=$?
    timeout 15s "$QEMU_USER" "$work/pcala-rounding-gnu.elf" \
        >"$work/pcala-rounding-gnu.out" 2>"$work/pcala-rounding-gnu.err" || gnu_pcala_rc=$?
    test "$mini_pcala_rc" -eq 0 && test "$gnu_pcala_rc" -eq 0
    cmp "$work/pcala-rounding-mini.out" "$work/pcala-rounding-gnu.out"
    cmp "$work/pcala-rounding-mini.err" "$work/pcala-rounding-gnu.err"
    echo 'MINILD_LOONGARCH_STATIC=PASS GNU_BFD_QEMU_PCALA_rounding_runtime_parity=1'
    mini_weak_rc=0; gnu_weak_rc=0
    timeout 15s "$QEMU_USER" "$work/pcala-weak-mini.elf" \
        >"$work/pcala-weak-mini.out" 2>"$work/pcala-weak-mini.err" || mini_weak_rc=$?
    timeout 15s "$QEMU_USER" "$work/pcala-weak-gnu.elf" \
        >"$work/pcala-weak-gnu.out" 2>"$work/pcala-weak-gnu.err" || gnu_weak_rc=$?
    test "$mini_weak_rc" -eq 0 && test "$gnu_weak_rc" -eq 0
    cmp "$work/pcala-weak-mini.out" "$work/pcala-weak-gnu.out"
    cmp "$work/pcala-weak-mini.err" "$work/pcala-weak-gnu.err"
    echo 'MINILD_LOONGARCH_STATIC=PASS GNU_BFD_QEMU_PCALA_undefweak_runtime_parity=1'
    mini_weak_jirl_rc=0; gnu_weak_jirl_rc=0
    timeout 15s "$QEMU_USER" "$work/pcala-weak-jirl-mini.elf" \
        >"$work/pcala-weak-jirl-mini.out" 2>"$work/pcala-weak-jirl-mini.err" || mini_weak_jirl_rc=$?
    timeout 15s "$QEMU_USER" "$work/pcala-weak-jirl-gnu.elf" \
        >"$work/pcala-weak-jirl-gnu.out" 2>"$work/pcala-weak-jirl-gnu.err" || gnu_weak_jirl_rc=$?
    test "$mini_weak_jirl_rc" -eq 0 && test "$gnu_weak_jirl_rc" -eq 0
    cmp "$work/pcala-weak-jirl-mini.out" "$work/pcala-weak-jirl-gnu.out"
    cmp "$work/pcala-weak-jirl-mini.err" "$work/pcala-weak-jirl-gnu.err"
    echo 'MINILD_LOONGARCH_STATIC=PASS GNU_BFD_QEMU_PCALA_undefweak_JIRL_runtime_parity=1'
    mini_b26_rc=0; gnu_b26_rc=0
    timeout 15s "$QEMU_USER" "$work/b26-weak-mini.elf" \
        >"$work/b26-weak-mini.out" 2>"$work/b26-weak-mini.err" || mini_b26_rc=$?
    timeout 15s "$QEMU_USER" "$work/b26-weak-gnu.elf" \
        >"$work/b26-weak-gnu.out" 2>"$work/b26-weak-gnu.err" || gnu_b26_rc=$?
    test "$mini_b26_rc" -eq 0 && test "$gnu_b26_rc" -eq 0
    cmp "$work/b26-weak-mini.out" "$work/b26-weak-gnu.out"
    cmp "$work/b26-weak-mini.err" "$work/b26-weak-gnu.err"
    echo 'MINILD_LOONGARCH_STATIC=PASS GNU_BFD_QEMU_B26_undefweak_runtime_parity=1'
    for consumer in preld fld_s fst_s fld_d fst_d vld vst xvld xvst; do
        mini_k12_rc=0; gnu_k12_rc=0
        timeout 15s "$QEMU_USER" "$work/pcala-k12-$consumer-mini.elf" \
            >"$work/pcala-k12-$consumer-mini.out" \
            2>"$work/pcala-k12-$consumer-mini.err" || mini_k12_rc=$?
        timeout 15s "$QEMU_USER" "$work/pcala-k12-$consumer-gnu.elf" \
            >"$work/pcala-k12-$consumer-gnu.out" \
            2>"$work/pcala-k12-$consumer-gnu.err" || gnu_k12_rc=$?
        test "$mini_k12_rc" -eq 0 && test "$gnu_k12_rc" -eq 0
        cmp "$work/pcala-k12-$consumer-mini.out" \
            "$work/pcala-k12-$consumer-gnu.out"
        cmp "$work/pcala-k12-$consumer-mini.err" \
            "$work/pcala-k12-$consumer-gnu.err"
    done
    echo 'MINILD_LOONGARCH_STATIC=PASS GNU_BFD_QEMU_PCALA_K12_9_consumer_runtime_parity=1'
else
    echo 'MINILD_LOONGARCH_STATIC=SKIP GNU_BFD_QEMU_runtime_parity missing_cross_tools'
fi

# IFUNC is a separate target-owned runtime contract: a call must go through
# an IPLT stub whose IRELATIVE slot glibc fills with the resolver result.
bash "$repo/tests/linker/probe-loongarch-static-ifunc.sh"
