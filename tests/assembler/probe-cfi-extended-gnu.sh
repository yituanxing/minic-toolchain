#!/usr/bin/env bash
set -Eeuo pipefail
: "$MINIAS"
case "$ARCH" in
x86_64) mini=(--target=x86_64); gas=(as --64); sp=7; ra=16; insn=nop;;
riscv) mini=(-march=rv64i -mabi=lp64); gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64); sp=2; ra=1; insn='addi x0,x0,0';;
riscv32) mini=(-march=rv32i -mabi=ilp32); gas=(riscv64-linux-gnu-as -march=rv32i -mabi=ilp32); sp=2; ra=1; insn='addi x0,x0,0';;
aarch64) mini=(--target=aarch64); gas=(aarch64-linux-gnu-as); sp=31; ra=30; insn=nop;;
loongarch64) mini=(--target=loongarch64); gas=(loongarch64-linux-gnu-as); sp=3; ra=1; insn='addi.d $r0,$r0,0';;
*) exit 2;;
esac
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
cat > "$work/cfi.s" <<SRC
.cfi_sections .debug_frame
.text
.globl cfi_probe
cfi_probe:
.cfi_startproc
.cfi_def_cfa $sp,16
.cfi_adjust_cfa_offset 16
.cfi_val_offset $ra,-8
.cfi_register $ra,$sp
.cfi_escape 0,0
$insn
.cfi_remember_state
.cfi_adjust_cfa_offset 8
$insn
.cfi_remember_state
.cfi_adjust_cfa_offset 8
$insn
.cfi_restore_state
.cfi_adjust_cfa_offset -8
$insn
.cfi_restore_state
.cfi_adjust_cfa_offset -8
$insn
.cfi_endproc
SRC
"$MINIAS" "${mini[@]}" -o "$work/mini.o" "$work/cfi.s"
"${gas[@]}" -o "$work/gnu.o" "$work/cfi.s"
readelf --debug-dump=frames "$work/mini.o" > "$work/mini.txt"
readelf --debug-dump=frames "$work/gnu.o" > "$work/gnu.txt"
python3 - "$work/mini.txt" "$work/gnu.txt" "$ARCH" <<'PY'
import re,sys
def parse(path):
    s=open(path,encoding='utf-8').read()
    if s.count(' FDE ')!=1:raise SystemExit(f'missing FDE in {path}')
    s=s.split(' FDE ',1)[1]
    out=[]
    for line in s.splitlines():
        m=re.search(r'DW_CFA_(def_cfa_offset|val_offset|register|remember_state|restore_state):?\s*(.*)',line)
        if m:
            kind,rest=m.groups()
            v=(int(re.search(r'\d+',rest).group()) if kind=='def_cfa_offset'
               else (-8 if 'cfa-8' in rest else 0) if kind=='val_offset' else 0)
            out.append((kind,v))
    return out
a,b=map(parse,sys.argv[1:3])
want=[('def_cfa_offset',32),('val_offset',-8),('register',0),
      ('remember_state',0),('def_cfa_offset',40),
      ('remember_state',0),('def_cfa_offset',48),
      ('restore_state',0),('def_cfa_offset',32),
      ('restore_state',0),('def_cfa_offset',24)]
if a!=b or a!=want:
    raise SystemExit(f'{sys.argv[3]} CFI GNU mismatch mini={a} gnu={b} expected={want}')
print('MINIAS_CFI_EXTENDED=PASS arch='+sys.argv[3])
PY
