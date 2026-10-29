#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
: "${ARCH:?Set ARCH to x86_64, riscv, riscv32, aarch64 or loongarch64}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

case "$ARCH" in
  x86_64)
    mini=(--target=x86_64); gnu=(as --64)
    cfa=7; ra=16; insn=nop
    eh_kind=R_X86_64_PC32; debug_kind=R_X86_64_64;;
  riscv)
    mini=(-march=rv64i -mabi=lp64)
    gnu=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
    cfa=2; ra=1; insn='addi x0,x0,0'
    eh_kind=R_RISCV_32_PCREL; debug_kind=R_RISCV_64;;
  riscv32)
    mini=(-march=rv32i -mabi=ilp32)
    gnu=(riscv64-linux-gnu-as -march=rv32i -mabi=ilp32)
    cfa=2; ra=1; insn='addi x0,x0,0'
    eh_kind=R_RISCV_32_PCREL; debug_kind=R_RISCV_32;;
  aarch64)
    mini=(--target=aarch64); gnu=(aarch64-linux-gnu-as)
    cfa=31; ra=30; insn=nop
    eh_kind=R_AARCH64_PREL32; debug_kind=R_AARCH64_ABS64;;
  loongarch64)
    mini=(--target=loongarch64); gnu=(loongarch64-linux-gnu-as)
    cfa=3; ra=1; insn='addi.d $r0,$r0,0'
    eh_kind=R_LARCH_32_PCREL; debug_kind=R_LARCH_64;;
  *) echo "unknown ARCH=$ARCH" >&2; exit 2;;
esac

if ! command -v "${gnu[0]}" >/dev/null; then
  echo "MINIAS_CFI_DUAL_FRAME=SKIP arch=$ARCH missing=${gnu[0]}"
  exit 0
fi

cat >"$work/dual.s" <<EOF
.cfi_sections .eh_frame, .debug_frame
.text
.globl dual_frame
dual_frame:
.Lminias_cfi_0:
.cfi_startproc
.cfi_def_cfa $cfa,16
.cfi_offset $ra,-8
$insn
.cfi_remember_state
.cfi_def_cfa_offset 24
$insn
.cfi_restore_state
.cfi_endproc
EOF

"$MINIAS" "${mini[@]}" -o "$work/mini.o" "$work/dual.s"
"${gnu[@]}" -o "$work/gnu.o" "$work/dual.s"
for producer in mini gnu; do
  readelf -SW "$work/$producer.o" >"$work/$producer.sections"
  readelf -rW "$work/$producer.o" >"$work/$producer.reloc"
  readelf --debug-dump=frames "$work/$producer.o" >"$work/$producer.frames"
done

python3 - "$ARCH" "$eh_kind" "$debug_kind" "$work" <<'PY'
import pathlib
import re
import sys

arch, eh_kind, debug_kind, root = sys.argv[1:]
root = pathlib.Path(root)

def analyze(producer):
    sections = (root / f'{producer}.sections').read_text()
    reloc = (root / f'{producer}.reloc').read_text()
    frames = (root / f'{producer}.frames').read_text()
    for name in ('.eh_frame', '.debug_frame'):
        if not re.search(r'\s' + re.escape(name) + r'\s', sections):
            raise SystemExit(f'{arch}: {producer} missing {name}\n{sections}')
        if not re.search(r'Relocation section [\x27]\.?rela?' + re.escape(name) + r'[\x27]', reloc):
            raise SystemExit(f'{arch}: {producer} missing relocation for {name}\n{reloc}')
    if eh_kind not in reloc or debug_kind not in reloc:
        raise SystemExit(f'{arch}: {producer} wrong EH/debug relocation\n{reloc}')
    fdes = len(re.findall(r'^\s*[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\b', frames, re.M))
    cies = len(re.findall(r'^\s*[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE\b', frames, re.M))
    if fdes != 2 or cies != 2:
        raise SystemExit(f'{arch}: {producer} expected two independent CIE/FDEs, got {cies}/{fdes}\n{frames}')
    return cies, fdes

mini, gnu = analyze('mini'), analyze('gnu')
if mini != gnu:
    raise SystemExit(f'{arch}: MiniAS/GNU frame count mismatch: {mini} != {gnu}')
print(f'MINIAS_CFI_DUAL_FRAME=PASS arch={arch} CIE={mini[0]} FDE={mini[1]}')
PY

# Reversed selection order must describe the same two outputs. Duplicate or
# unknown section names must fail before any unwind event can be emitted.
sed 's/\.cfi_sections \.eh_frame, \.debug_frame/.cfi_sections .debug_frame,.eh_frame/' \
  "$work/dual.s" >"$work/reverse.s"
"$MINIAS" "${mini[@]}" -o "$work/reverse.o" "$work/reverse.s"
readelf -SW "$work/reverse.o" >"$work/reverse.sections"
grep -q '[[:space:]]\.eh_frame[[:space:]]' "$work/reverse.sections"
grep -q '[[:space:]]\.debug_frame[[:space:]]' "$work/reverse.sections"
for invalid in '.cfi_sections .debug_frame,.debug_frame' \
               '.cfi_sections .eh_frame,.unknown_frame'; do
  sed "1c$invalid" "$work/dual.s" >"$work/invalid.s"
  if "$MINIAS" "${mini[@]}" -o "$work/invalid.o" "$work/invalid.s" \
      >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo "invalid CFI section selection was accepted: $invalid" >&2
    exit 1
  fi
done
