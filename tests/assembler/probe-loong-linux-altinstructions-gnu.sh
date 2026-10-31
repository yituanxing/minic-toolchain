#!/usr/bin/env bash
# Linux ALTERNATIVE metadata uses mixed-width cross-fragment relocations.
# Verify the production MiniAS path against GNU without testing an ELF
# emitter in isolation or weakening the owning MCData transaction.
set -Eeuo pipefail
: "${MINIAS:?}"
if ! command -v loongarch64-linux-gnu-as >/dev/null; then
  echo 'MINIAS_LOONG_LINUX_ALT=SKIP missing=loongarch64-linux-gnu-as'
  exit 0
fi
repo=$(cd "$(dirname "$0")/../.." && pwd)
source_file="$repo/tests/assembler/fixtures/loong-linux-altinstructions-data-widths.S"
work=${WORK_DIR:-"$repo/build/minias-gcc-runtime-ab/loongarch64/linux-alternative"}
mkdir -p "$work"
report_failure() {
  local status=$?
  echo "MINIAS_LOONG_LINUX_ALT=FAIL line=${BASH_LINENO[0]} exit=$status" >&2
  if [[ -s "$work/strict.json" ]]; then
    # This is often a same-offset ADD/SUB ordering disagreement. Include
    # both sides in one bounded log so the next batch can decide whether the
    # order is linker-significant without downloading the full ELF artifact.
    python3 - "$work/strict.json" <<'PY_FAIL' >&2
import json
import pathlib
import sys
diff = json.loads(pathlib.Path(sys.argv[1]).read_text()).get('differences', {})
print('MINIAS_LOONG_ALT_RELOCATION_ORDER=' +
      json.dumps(diff.get('relocation_order', {}), separators=(',', ':'))[:12000])
PY_FAIL
  fi
  if [[ -s "$work/mini.reloc" && -s "$work/gnu.reloc" ]]; then
    diff -u "$work/gnu.reloc" "$work/mini.reloc" >&2 || true
  fi
  echo "MINIAS_LOONG_LINUX_ALT_ARTIFACT=$work" >&2
  exit "$status"
}
trap report_failure ERR
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
  -o "$work/mini.o" "$source_file"
loongarch64-linux-gnu-as -mabi=lp64d -o "$work/gnu.o" "$source_file"
# The target controls visibility of generated expression-site anchors.  GNU
# LoongArch emits them as LOCAL/HIDDEN, unlike the DEFAULT anchors used by
# other ABIs; this contract must survive the shared ELF writer unchanged.
dot_source="$repo/tests/assembler/fixtures/loong-expr-dot-anchor-gnu.S"
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
  -o "$work/mini-dot.o" "$dot_source"
loongarch64-linux-gnu-as -mabi=lp64d -o "$work/gnu-dot.o" "$dot_source"
python3 - "$repo/tests/assembler" "$work/gnu-dot.o" "$work/mini-dot.o" <<'PY_DOT'
import sys
sys.path.insert(0, sys.argv[1])
from elf_semantic_compare import load_semantics

gnu, mini = (load_semantics(path) for path in sys.argv[2:])
# Some GNU GAS builds emit an implicit .debug_line for this bare source file,
# while MiniAS emits line tables only for explicit .file/.loc directives.
# Compare the actual expression's relocation section, not that unrelated
# assembler-generated DWARF; retain exact offset/type/addend/symbol identity,
# ELF binding/visibility and pair ordering within .rela.debug_info.
def info_relocations(obj):
    return [row for row in obj['relocations']
            if row['key'][0] == '.debug_info']

def info_order(obj):
    return {name: entries for name, entries in obj['relocation_order'].items()
            if name == '.rela.debug_info'}

assert sum(row['count'] for row in info_relocations(gnu)) == 4
assert info_relocations(mini) == info_relocations(gnu), 'dot relocation tuples'
assert info_order(mini) == info_order(gnu), 'dot relocation ordering'
PY_DOT
hidden_count=$(readelf -sW "$work/mini-dot.o" | \
  awk '$5=="LOCAL" && $6=="HIDDEN" && $8 ~ /^\.Lminias_private_/ {n++} END {print n+0}')
test "$hidden_count" -eq 2
echo 'MINIAS_LOONG_EXPR_DOT=PASS local_hidden=2 GNU_relocation_visibility=exact'
readelf -Wr "$work/mini.o" >"$work/mini.reloc"
readelf -Wr "$work/gnu.o" >"$work/gnu.reloc"
python3 - "$work/mini.reloc" "$work/gnu.reloc" <<'PY'
import sys
for name in sys.argv[1:]:
    reloc = open(name, encoding='utf-8').read()
    for typ, count in (('R_LARCH_ADD8', 2), ('R_LARCH_SUB8', 2),
                       ('R_LARCH_ADD32', 2), ('R_LARCH_SUB32', 2)):
        actual = reloc.count(typ)
        assert actual == count, (name, typ, actual, count)
PY
python3 "$repo/tests/assembler/elf_semantic_compare.py" --strict \
  "$work/gnu.o" "$work/mini.o" >"$work/strict.json"
echo 'MINIAS_LOONG_LINUX_ALT=PASS strict_gnu=1 fragment_add8_sub8=2/2 add32_sub32=2/2'
