#!/usr/bin/env bash
# Full Common .debug_frame generation: CIE+FDE+anchored DW_CFA instructions,
# with true target-specific address relocation and GNU GAS oracle. The
# non-x86 minimal .cfi_startproc shim must NOT satisfy this test.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${ARCH:?ARCH=x86_64|riscv|aarch64|loongarch64}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
case "$ARCH" in
  x86_64)
    mini_opts=(--target=x86_64)
    gnu=(as --64)
    cfa=7; ra=16; code='nop'
    ;;
  riscv)
    mini_opts=(-march=rv64i -mabi=lp64)
    gnu=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
    cfa=2; ra=1; code='addi x0,x0,0'
    ;;
  riscv32)
    mini_opts=(-march=rv32i -mabi=ilp32)
    gnu=(riscv64-linux-gnu-as -march=rv32i -mabi=ilp32)
    cfa=2; ra=1; code='addi x0,x0,0'
    ;;
  aarch64)
    mini_opts=(--target=aarch64)
    gnu=(aarch64-linux-gnu-as)
    cfa=31; ra=30; code='nop'
    ;;
  loongarch64)
    mini_opts=(--target=loongarch64)
    gnu=(loongarch64-linux-gnu-as)
    cfa=3; ra=1; code='addi.d $r0,$r0,0'
    ;;
  *) echo "Unsupported ARCH: $ARCH" >&2; exit 2 ;;
esac
cat >"$work/unwind.s" <<SRC
.cfi_sections .debug_frame
.text
.globl unwind_probe
.type unwind_probe,@function
unwind_probe:
.cfi_startproc
.cfi_def_cfa $cfa,16
.cfi_offset $ra,-8
$code
.cfi_restore $ra
$code
.cfi_endproc
.size unwind_probe,.-unwind_probe
SRC
"$MINIAS" "${mini_opts[@]}" -o "$work/mini.o" "$work/unwind.s"
"${gnu[@]}" -o "$work/gnu.o" "$work/unwind.s"
readelf --debug-dump=frames "$work/mini.o" >"$work/mini.frames"
readelf --debug-dump=frames "$work/gnu.o" >"$work/gnu.frames"
readelf -rW "$work/mini.o" >"$work/mini.reloc"
readelf -rW "$work/gnu.o" >"$work/gnu.reloc"
python3 - "$ARCH" "$work/mini.frames" "$work/gnu.frames" <<'PY'
import re,sys
arch,mini,gnu=sys.argv[1:]
def parse(path):
    s=open(path,encoding='utf-8').read()
    result={}
    result['CIE']=len(re.findall(r'\bCIE\b',s))
    result['FDE']=len(re.findall(r'\bFDE\b',s))
    result['def_cfa']=bool(re.search(r'DW_CFA_def_cfa.*16',s))
    result['offset']=bool(re.search(r'DW_CFA_offset.*cfa-',s))
    result['restore']=bool(re.search(r'DW_CFA_restore',s))
    return result
a,b=map(parse,(mini,gnu))
needed=('def_cfa','offset','restore')
if a['CIE']!=1 or a['FDE']!=1 or not all(a[x] for x in needed) or not all(b[x] for x in needed):
    raise SystemExit(f'{arch}: incomplete .debug_frame MiniAS={a} GNU={b}')
print(f'MINIAS_CFI_DEBUG_FRAME=PASS arch={arch} CIE=1 FDE=1 cfa_offset_restore=3')
PY
if ! grep -Eq 'R_(X86_64_64|RISCV_64|RISCV_32|AARCH64_ABS64|LARCH_64)' "$work/mini.reloc"; then
    echo "$ARCH: missing real FDE address relocation" >&2
    cat "$work/mini.reloc" >&2
    exit 1
fi
# ELF32 FDE addresses and ranges must be four bytes, encoded in
# an ELFCLASS32 debug-frame object, not merely accepted as ELF64 bytes.
if test "$ARCH" = riscv32; then
    readelf -h "$work/mini.o" | grep -q 'Class:.*ELF32'
    grep -q 'R_RISCV_32' "$work/mini.reloc"
fi
