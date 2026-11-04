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

# For a relaxation-enabled alignment whose reserved NOPs do not already
# produce a correct boundary, fail closed until target section relaxation is
# implemented.  A link success here would silently corrupt alignment.
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
if "$MINILD" -m elf64loongarch -static -e _start \
    -o "$work/align.elf" "$work/align.o" >"$work/align.log" 2>&1; then
    echo 'MINILD_LOONGARCH_STATIC=FAIL ignored_alignment_shrink_requirement' >&2
    exit 1
fi
grep -Eq 'loongarch-align-needs-relaxation|invalid-loongarch-static-align' \
    "$work/align.log"
echo 'MINILD_LOONGARCH_STATIC=PASS mandatory_align_relaxation=fail_closed'

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
    mini_rc=0; gnu_rc=0
    timeout 15s "$QEMU_USER" "$work/mini.elf" >"$work/mini.out" \
        2>"$work/mini.err" || mini_rc=$?
    timeout 15s "$QEMU_USER" "$work/gnu.elf" >"$work/gnu.out" \
        2>"$work/gnu.err" || gnu_rc=$?
    test "$mini_rc" -eq 0 && test "$gnu_rc" -eq 0
    cmp "$work/mini.out" "$work/gnu.out"
    cmp "$work/mini.err" "$work/gnu.err"
    echo 'MINILD_LOONGARCH_STATIC=PASS GNU_BFD_QEMU_runtime_parity=1'
else
    echo 'MINILD_LOONGARCH_STATIC=SKIP GNU_BFD_QEMU_runtime_parity missing_cross_tools'
fi
