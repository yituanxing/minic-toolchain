#!/usr/bin/env bash
set -Eeuo pipefail

# Compare real GNU objects with identical section *names* under distinct
# COMDAT signatures. A name-keyed dict used to lose one of these sections.
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
: "${PYTHON:=python3}"
"$PYTHON" -c 'import elftools'

for mode in good bad; do
    value=0x01020304
    if [[ "$mode" == bad ]]; then value=0x11223344; fi
    printf '.section .text.same,"axG",@progbits,first,comdat\n.long 0xdeadbeef\n.section .text.same,"axG",@progbits,second,comdat\n.long %s\n' "$value" >"$work/$mode.s"
    as --64 -o "$work/$mode.o" "$work/$mode.s"
done

"$PYTHON" - "$repo" "$work" <<'PY'
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / "tests" / "assembler"))
from elf_semantic_compare import compare_paths, load_semantics

root = pathlib.Path(sys.argv[2])
good = root / "good.o"
bad = root / "bad.o"
sem = load_semantics(good)
text_sections = [key for key in sem["contents_masked"] if ".text.same" in key]
assert len(text_sections) == 2, text_sections
assert len(sem["groups"]) == 2, sem["groups"]
assert compare_paths(good, good)["equal"]
assert not compare_paths(good, bad)["equal"], "duplicate COMDAT mutation was hidden"
assert not compare_paths(good, bad)["dimensions"]["contents_masked"]
print("MINIAS_ELF_SEMANTIC_COMDAT=PASS duplicate=2 group=2 mutation=detected")
PY

cat >"$work/unknown.s" <<'SRC'
.text
.globl unknown_reloc
unknown_reloc:
  .long 0x01020304
  .reloc 0, R_X86_64_GNU_VTINHERIT, undefined_symbol
SRC
as --64 -o "$work/unknown.o" "$work/unknown.s"
"$PYTHON" - "$repo" "$work" <<'PY'
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / "tests" / "assembler"))
from elf_semantic_compare import compare_paths

root = pathlib.Path(sys.argv[2])
result = compare_paths(root / "unknown.o", root / "unknown.o")
assert not result["equal"] and result["inconclusive"], result
assert not result["dimensions"]["known_relocation_widths"]
print("MINIAS_ELF_SEMANTIC_UNKNOWN_RELOC=PASS verdict=INCONCLUSIVE")
PY
