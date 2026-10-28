#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?}"
: "${ARCH:?}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
case "$ARCH" in
x86_64) mini=(--target=x86_64); gas=(as --64); cfa=7; ra=16; insn=nop; kind=R_X86_64_PC32;;
riscv) mini=(-march=rv64i -mabi=lp64); gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64); cfa=2; ra=1; insn='addi x0,x0,0'; kind=R_RISCV_32_PCREL;;
riscv32) mini=(-march=rv32i -mabi=ilp32); gas=(riscv64-linux-gnu-as -march=rv32i -mabi=ilp32); cfa=2; ra=1; insn='addi x0,x0,0'; kind=R_RISCV_32_PCREL;;
aarch64) mini=(--target=aarch64); gas=(aarch64-linux-gnu-as); cfa=31; ra=30; insn=nop; kind=R_AARCH64_PREL32;;
loongarch64) mini=(--target=loongarch64); gas=(loongarch64-linux-gnu-as); cfa=3; ra=1; insn='addi.d $r0,$r0,0'; kind=R_LARCH_32_PCREL;;
*) exit 2;;
esac
cat >"$work/eh.s" <<SRC
.text
.globl eh_normal
eh_normal:
.cfi_startproc
.cfi_def_cfa $cfa,16
.cfi_offset $ra,-8
$insn
.cfi_restore $ra
$insn
.cfi_endproc
.globl eh_simple
eh_simple:
.cfi_startproc simple
.cfi_def_cfa $cfa,8
$insn
.cfi_endproc
.globl eh_signal
eh_signal:
.cfi_startproc
.cfi_signal_frame
$insn
.cfi_endproc
SRC
"$MINIAS" "${mini[@]}" -o "$work/m.o" "$work/eh.s"
"${gas[@]}" -o "$work/g.o" "$work/eh.s"
readelf --debug-dump=frames "$work/m.o" >"$work/m.frame"
readelf --debug-dump=frames "$work/g.o" >"$work/g.frame"
readelf -rW "$work/m.o" >"$work/m.reloc"
readelf -rW "$work/g.o" >"$work/g.reloc"
python3 - "$ARCH" "$kind" "$work/m.frame" "$work/g.frame" "$work/m.reloc" "$work/g.reloc" <<'PY'
import re,sys
arch,kind,a,b,c,d=sys.argv[1:]
def frame(p):
    s=open(p).read()
    return dict(cie=len(re.findall(r'^\s*[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE\b',s,re.M)),
                fde=len(re.findall(r'^\s*[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\b',s,re.M)),
                signal='zRS' in s,offset='DW_CFA_offset' in s,
                restore='DW_CFA_restore' in s)
m,g=map(frame,(a,b))
if m['cie']<2 or m['fde']!=3 or m!=g:
    raise SystemExit(f'{arch}: frame mismatch Mini={m} GNU={g}\nMini:\n'+open(a).read()+'\nGNU:\n'+open(b).read())
mr,gr=open(c).read(),open(d).read()
if mr.count(kind)<3 or gr.count(kind)<3:
    raise SystemExit(f'{arch}: missing {kind} genuine FDE relocations\nMini:\n{mr}\nGNU:\n{gr}')
print(f'MINIAS_CFI_EH_FRAME=PASS arch={arch} FDE=3 relocation={kind}')
PY
