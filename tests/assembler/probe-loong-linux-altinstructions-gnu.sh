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
trap 'status=$?; echo "MINIAS_LOONG_LINUX_ALT=FAIL line=$LINENO exit=$status" >&2; if [[ -s "$work/strict.json" ]]; then sed -n "1,120p" "$work/strict.json" >&2; fi; echo "MINIAS_LOONG_LINUX_ALT_ARTIFACT=$work" >&2' ERR
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
  -o "$work/mini.o" "$source_file"
loongarch64-linux-gnu-as -mabi=lp64d -o "$work/gnu.o" "$source_file"
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
