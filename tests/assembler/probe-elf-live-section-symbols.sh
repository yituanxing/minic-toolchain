#!/usr/bin/env bash
# GNU creates STT_SECTION entries on demand for section-relative relocations.
# Absent such relocations, default .text/.data/.bss must not acquire synthetic
# local SECTION symbols merely because a section header exists.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for command_name in as readelf python3; do
    command -v "$command_name" >/dev/null || exit 1
done
if ! python3 -c 'import elftools' >/dev/null 2>&1; then
    echo 'MINIAS_ELF_LIVE_SECTION_SYMBOLS=NOT_TESTED:no_pyelftools'
    exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/no_section_reloc.s" <<'ASM'
.text
.globl exported_entry
exported_entry:
    nop
.data
regular_data:
    .quad 42
ASM

cat >"$work/cross_section_reloc.s" <<'ASM'
.text
visible_text:
    nop
.data
    .quad visible_text
ASM

cat >"$work/same_section_reloc.s" <<'ASM'
.text
visible_text:
    nop
    .long visible_text
ASM

cat >"$work/external_reloc.s" <<'ASM'
.text
.globl exported_entry
exported_entry:
    movq external_data(%rip), %rax
ASM

for case_name in no_section_reloc cross_section_reloc same_section_reloc external_reloc; do
    "$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/$case_name.s"
    as --64 -o "$work/gnu.o" "$work/$case_name.s"
    if ! python3 tests/assembler/elf_semantic_compare.py --strict \
            "$work/gnu.o" "$work/mini.o" >"$work/report.json"; then
        echo "MINIAS_ELF_LIVE_SECTION_SYMBOLS=FAIL case=$case_name" >&2
        python3 - "$work/report.json" <<'PY' >&2
import json
import sys

with open(sys.argv[1], encoding='utf-8') as stream:
    report = json.load(stream)
print(json.dumps(report.get('differences', {}), indent=2))
PY
        exit 1
    fi
    echo "MINIAS_ELF_LIVE_SECTION_SYMBOLS=PASS case=$case_name GNU_object_semantic=exact"
done
