#!/usr/bin/env bash
# GNU AArch64 .inst is lexically a data-list, but emits executable $x mapping.
# A byte-only object comparison cannot catch an accidental $d at this site.
set -Eeuo pipefail

: "${MINIAS:?MiniAS executable required}"
: "${AARCH64_AS:=aarch64-linux-gnu-as}"
command -v "$AARCH64_AS" >/dev/null
command -v readelf >/dev/null
command -v python3 >/dev/null
python3 -c 'import elftools' >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/inst-map.s" <<'ASM'
.text
    .word 0xd503201f
    .inst 0xd503201f
    nop
    .word 0xd503201f
ASM

"$MINIAS" --target=aarch64 -o "$work/mini.o" "$work/inst-map.s"
"$AARCH64_AS" -o "$work/gnu.o" "$work/inst-map.s"
readelf -sW "$work/mini.o" >"$work/mini.sym"
readelf -sW "$work/gnu.o" >"$work/gnu.sym"

# Verify that the semantic comparator cannot produce a false green when the
# total mapping-symbol count happens to match but $x/$d offset tags differ.
python3 - "$work/mini.sym" "$work/gnu.sym" <<'PY'
import pathlib
import sys

expected = [("$d", 0), ("$x", 4), ("$d", 12)]
for path in sys.argv[1:]:
    mappings = []
    for line in pathlib.Path(path).read_text().splitlines():
        fields = line.split()
        if (len(fields) >= 8 and fields[3] == "NOTYPE" and
                fields[4] == "LOCAL" and fields[7].split(".", 1)[0] in ("$x", "$d")):
            mappings.append((fields[7].split(".", 1)[0], int(fields[1], 16)))
    if sorted(mappings, key=lambda item: item[1]) != expected:
        raise SystemExit(f"incorrect AArch64 .inst mapping in {path}: {mappings}")
PY

python3 "$(dirname "$0")/elf_semantic_compare.py" --strict \
    "$work/gnu.o" "$work/mini.o" >"$work/semantic.json" || {
    echo 'MINIAS_AARCH64_INST_MAPPING=FAIL GNU_strict_object_semantics' >&2
    python3 -m json.tool "$work/semantic.json" >&2
    exit 1
}
echo 'MINIAS_AARCH64_INST_MAPPING=PASS GNU_strict_object_semantics $d0/$x4/$d12'

# GNU's .inst also calls mapping_state(MAP_INSN) in .data, where that call
# raises the section's sh_addralign to 4 (despite Common's data-list model).
cat >"$work/data-inst.s" <<'ASM'
.data
    .inst 0xd503201f
ASM
"$MINIAS" --target=aarch64 -o "$work/data-mini.o" "$work/data-inst.s"
"$AARCH64_AS" -o "$work/data-gnu.o" "$work/data-inst.s"
python3 - "$work/data-mini.o" "$work/data-gnu.o" <<'PY'
import pathlib
import re
import subprocess
import sys

for path in sys.argv[1:]:
    rows = subprocess.check_output(
        ["readelf", "-SW", path], text=True).splitlines()
    section = [row for row in rows if re.search(r"\]\s+\.data\s+PROGBITS\b", row)]
    if len(section) != 1 or int(section[0].split()[-1]) != 4:
        raise SystemExit(f".data .inst must record ELF alignment=4: {path}: {section}")
PY
python3 "$(dirname "$0")/elf_semantic_compare.py" --strict \
    "$work/data-gnu.o" "$work/data-mini.o" >"$work/data-semantic.json" || {
    echo 'MINIAS_AARCH64_INST_MAPPING=FAIL GNU_strict_data_inst_semantics' >&2
    python3 -m json.tool "$work/data-semantic.json" >&2
    exit 1
}
echo 'MINIAS_AARCH64_INST_MAPPING=PASS GNU_strict_data_inst_alignment=4'
