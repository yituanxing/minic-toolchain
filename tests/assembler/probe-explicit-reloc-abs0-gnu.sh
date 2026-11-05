#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in as readelf objcopy python3; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# A two-operand .reloc is not shorthand for a symbol named "0": GNU emits
# an explicit relocation whose ELF symbol index is zero. The ordinary
# three-operand path and source-dot offsets must remain unchanged.
for arch in x86_64 i386 aarch64; do
  case "$arch" in
    x86_64)
      mini=(--target=x86_64); gnu=(as --64); obj=objcopy
      cp tests/assembler/fixtures/reloc-source-no-symbol-gnu.S "$work/source.s";;
    i386)
      mini=(--target=x86_64 -march=i386 -mabi=i386)
      gnu=(as --32); obj=objcopy
      printf '.text\n.long 0\n.reloc 0, R_386_32\n' >"$work/source.s";;
    aarch64)
      mini=(--target=aarch64); gnu=(aarch64-linux-gnu-as)
      obj=aarch64-linux-gnu-objcopy
      printf '.text\n.quad 0\n.reloc 0, R_AARCH64_ABS64\n' >"$work/source.s";;
  esac
  command -v "${gnu[0]}" >/dev/null
  command -v "$obj" >/dev/null
  "${gnu[@]}" -o "$work/gnu.o" "$work/source.s"
  "$MINIAS" "${mini[@]}" -o "$work/mini.o" "$work/source.s"
  for who in gnu mini; do
    readelf -rW "$work/$who.o" >"$work/$who.reloc"
    "$obj" --dump-section ".text=$work/$who.text" "$work/$who.o"
  done
  cmp "$work/gnu.text" "$work/mini.text"
  python3 - "$arch" "$work" <<'PY'
from pathlib import Path
import re
import sys

arch, directory = sys.argv[1], Path(sys.argv[2])

def relocations(who):
    records = []
    for line in (directory / f'{who}.reloc').read_text().splitlines():
        row = re.match(r'^\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(R_\S+)\b', line)
        if row:
            # In both ELF32 and ELF64, GNU encodes symbol index zero as
            # relocation info < 256 (ELF32) / < 2**32 (ELF64).
            info = int(row.group(2), 16)
            limit = 1 << (8 if arch == 'i386' else 32)
            if info >= limit:
                raise SystemExit(f'{who} arch={arch}: expected ELF symbol zero: {line}')
            records.append((int(row.group(1), 16), row.group(3)))
    return records

gnu, mini = relocations('gnu'), relocations('mini')
if len(gnu) != 1 or gnu != mini:
    raise SystemExit(f'GNU symbol-zero reloc mismatch arch={arch}: {gnu=} {mini=}')
PY
  echo "MINIAS_RELOC_ABS0=PASS arch=$arch ELF_symbol_index=0 bytes=exact relocs=exact"
done

# The source-dot case is AMD64-specific; compare semantic relocation fields,
# not the unrelated absolute ELF symbol-table ordinal or section-file offset.
as --64 -o "$work/gnu.o" tests/assembler/fixtures/reloc-source-dot-gnu.S
"$MINIAS" --target=x86_64 -o "$work/mini.o" \
  tests/assembler/fixtures/reloc-source-dot-gnu.S
for who in gnu mini; do
  readelf -rW "$work/$who.o" >"$work/$who.reloc"
done
python3 - "$work" <<'PY'
from pathlib import Path
import re
import sys

directory = Path(sys.argv[1])

def relocations(who):
    records = []
    for line in (directory / f'{who}.reloc').read_text().splitlines():
        row = re.match(r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(R_\S+)\s+[0-9a-fA-F]+\s+(.+)$', line)
        if row:
            records.append((int(row.group(1), 16), row.group(2), row.group(3).strip()))
    return records

gnu, mini = relocations('gnu'), relocations('mini')
if len(gnu) != 3 or gnu != mini:
    raise SystemExit(f'GNU source-dot reloc mismatch: {gnu=} {mini=}')
print('MINIAS_RELOC_DOT=PASS x86_64 current_dot_plus_addend=3 GNU=exact')
PY
