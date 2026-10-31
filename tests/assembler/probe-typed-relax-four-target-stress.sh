#!/usr/bin/env bash
# Dense source-order fragments across all four backends: 160 GNU forward
# .space expressions, typed .fill, late .org and an externally bound RELA.
# Compare actual exact-head objects against an independent GNU assembler.
set -Eeuo pipefail
: "${MINIAS:?exact-head MiniAS executable required}"
: "${ARCH:?target architecture required}"
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
trap 'echo "TYPED_RELAX_STRESS_FAIL arch=$ARCH command=$BASH_COMMAND" >&2; test ! -f "$scratch/trace" || cat "$scratch/trace" >&2' ERR

case "$ARCH" in
  x86_64)
    mini=(--target=x86_64); gas=(as --64); copy=(objcopy);;
  riscv)
    mini=(--target=riscv)
    gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
    copy=(riscv64-linux-gnu-objcopy);;
  aarch64)
    mini=(--target=aarch64)
    gas=(aarch64-linux-gnu-as); copy=(aarch64-linux-gnu-objcopy);;
  loongarch64)
    mini=(--target=loongarch64)
    gas=(loongarch64-linux-gnu-as); copy=(loongarch64-linux-gnu-objcopy);;
  *) echo "unknown ARCH=$ARCH" >&2; exit 2;;
esac

{
  echo '.data'
  echo '.globl stress_data_begin'
  echo 'stress_data_begin:'
  for ((i=0; i<160; ++i)); do
    printf 'typed_slot_%03d:\n' "$i"
    echo '    .space forward_end-forward_begin,0x5a'
    printf '    .fill 2,2,0x%04x\n' "$((0x1200+i))"
    printf '    .long .-typed_slot_%03d\n' "$i"
  done
  echo '.subsection 1'
  echo '    .org 8192,0x7d'
  echo '.globl stress_data_tail'
  echo 'stress_data_tail:'
  echo '    .quad stress_external+19'
  echo '    .byte 0xa7'
  echo '.section .rodata,"a"'
  echo 'forward_begin:'
  echo '    .ascii "hello world"'
  echo 'forward_end:'
} >"$scratch/stress.s"

MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$scratch/mini.o" "$scratch/stress.s" 2>"$scratch/trace"
"${gas[@]}" -o "$scratch/gnu.o" "$scratch/stress.s"

grep -q 'minias-fragment-graph:anchors_built=1.*reparse=0' "$scratch/trace"
grep -Eq '^minias-mcexpr-variable:typed=[1-9][0-9]* fallback=0$' "$scratch/trace"
for section in .data .rodata; do
  "${copy[@]}" -j "$section" -O binary "$scratch/mini.o" "$scratch/mini.${section##.}"
  "${copy[@]}" -j "$section" -O binary "$scratch/gnu.o" "$scratch/gnu.${section##.}"
  cmp "$scratch/mini.${section##.}" "$scratch/gnu.${section##.}"
done
test "$(wc -c <"$scratch/mini.data")" -eq 8201

# ELF section order, symbol indices and GNU's private anchors may differ;
# the relocation's offset, relocation class, external symbol and addend may not.
readelf -rW "$scratch/mini.o" >"$scratch/mini.reloc"
readelf -rW "$scratch/gnu.o" >"$scratch/gnu.reloc"
python3 - "$ARCH" "$scratch/mini.reloc" "$scratch/gnu.reloc" \
  "$scratch/mini.o" "$scratch/gnu.o" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

arch, mini_path, gnu_path, mini_obj, gnu_obj = sys.argv[1:]
reloc_pattern = re.compile(
    r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(R_[A-Z0-9_]+)\s+'
    r'[0-9a-fA-F]+\s+stress_external\s+\+\s+([0-9a-fA-F]+)\s*$')

def external_relocation(path):
    matches = [tuple((int(m.group(1), 16), m.group(2), int(m.group(3), 16)))
               for line in Path(path).read_text().splitlines()
               if (m := reloc_pattern.match(line))]
    if len(matches) != 1:
        raise SystemExit(f'{arch} expected one real external relocation, got '
                         f'{matches}\n{Path(path).read_text()}')
    return matches[0]

mini = external_relocation(mini_path)
gnu = external_relocation(gnu_path)
if mini != gnu or mini[0] != 8192 or mini[2] != 19:
    raise SystemExit(f'{arch} GNU external relocation mismatch: MiniAS={mini}, GNU={gnu}')

symbol_pattern = re.compile(r'^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+\S+\s+\S+\s+\S+\s+\S+\s+stress_data_tail\s*$')
def tail_symbol(path):
    output = subprocess.check_output(['readelf', '-sW', path], text=True)
    values = [int(m.group(1), 16) for line in output.splitlines()
              if (m := symbol_pattern.match(line))]
    if len(values) != 1:
        raise SystemExit(f'{arch} missing/duplicate stress_data_tail symbol: {output}')
    return values[0]

if (tail_symbol(mini_obj), tail_symbol(gnu_obj)) != (8192, 8192):
    raise SystemExit(f'{arch} GNU late .org symbol mismatch')

print(f'MINIAS_TYPED_RELAX_FOUR_TARGET_STRESS=PASS arch={arch} '
      'forward_typed=160 org=8192 relocs=1 GNU_bytes=exact GNU_reloc=exact '
      'reparse=0')
PY
