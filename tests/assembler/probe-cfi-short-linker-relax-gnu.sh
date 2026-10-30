#!/usr/bin/env bash
# Verify GNU and MiniAS retain a short CFI code-distance expression across
# LoongArch linker-relaxable instructions.  No external linker is required:
# strict ELF comparison checks exact fixups, opcode bytes and symbol anchors.
set -Eeuo pipefail
: "${MINIAS:?}"
if ! command -v loongarch64-linux-gnu-as >/dev/null; then
  echo 'MINIAS_CFI_SHORT_RELAX=SKIP missing=loongarch64-linux-gnu-as'
  exit 0
fi
repo=$(cd "$(dirname "$0")/../.." && pwd)
source_file="$repo/tests/assembler/fixtures/cfi-loong-short-relax.S"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
  -o "$work/mini.o" "$source_file"
loongarch64-linux-gnu-as -mabi=lp64d -o "$work/gnu.o" "$source_file"
readelf -Wr "$work/mini.o" >"$work/mini.reloc"
readelf -Wr "$work/gnu.o" >"$work/gnu.reloc"
python3 - "$work/mini.reloc" "$work/gnu.reloc" <<'PY'
import sys
for name in sys.argv[1:]:
    relocs=open(name,encoding='utf-8').read()
    expected={'R_LARCH_RELAX':2,'R_LARCH_ADD6':1,'R_LARCH_SUB6':1,
              'R_LARCH_ADD32':2,'R_LARCH_SUB32':2}
    for reloc, count in expected.items():
        actual=relocs.count(reloc)
        assert actual==count,(name,reloc,count,actual,relocs)
PY
python3 "$repo/tests/assembler/elf_semantic_compare.py" --strict \
  "$work/gnu.o" "$work/mini.o" >"$work/strict.json"
echo 'MINIAS_CFI_SHORT_RELAX=PASS arch=loongarch64 strict_gnu=1 reloc_add6_sub6=1 fixed_short=1'
