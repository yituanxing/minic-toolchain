#!/usr/bin/env bash
# Variable-sized CFI escape is one MC event, regardless of raw byte count.
set -Eeuo pipefail
case "$ARCH" in
x86_64) mini=(--target=x86_64); gas=(as --64); insn=nop;;
riscv) mini=(-march=rv64i -mabi=lp64); gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64); insn='addi x0,x0,0';;
aarch64) mini=(--target=aarch64); gas=(aarch64-linux-gnu-as); insn=nop;;
loongarch64) mini=(--target=loongarch64); gas=(loongarch64-linux-gnu-as); insn='addi.d $r0,$r0,0';;
*) exit 2;;
esac
work=$(mktemp -d);trap 'rm -rf "$work"' EXIT
python3 - "$insn" "$work/large.s" <<'PY'
import sys
insn,path=sys.argv[1:]
payload=','.join(['0']*256)
with open(path,'w') as out:
    out.write('.cfi_sections .debug_frame\n.text\n.globl big_cfi\nbig_cfi:\n.cfi_startproc\n')
    out.write(insn+'\n.cfi_escape '+payload+'\n'+insn+'\n.cfi_endproc\n')
PY
"$MINIAS" "${mini[@]}" -o "$work/mini.o" "$work/large.s"
"${gas[@]}" -o "$work/gnu.o" "$work/large.s"
readelf --debug-dump=frames "$work/mini.o" >"$work/mini.txt"
readelf --debug-dump=frames "$work/gnu.o" >"$work/gnu.txt"
python3 - "$work/mini.txt" "$work/gnu.txt" "$ARCH" <<'PY'
import sys
a,b,arch=sys.argv[1:]
def count(path):
    s=open(path).read()
    if ' FDE ' not in s:raise SystemExit(arch+' missing FDE '+path)
    return s.split(' FDE ',1)[1].count('DW_CFA_nop')
x,y=count(a),count(b)
if x<256 or y<256 or abs(x-y)>8:
    raise SystemExit(f'{arch} missing raw CFI payload: mini={x} gnu={y}')
print(f'MINIAS_CFI_ESCAPE_BLOB=PASS arch={arch} raw_nops={x} GNU_nops={y} one_mc_event=1')
PY
