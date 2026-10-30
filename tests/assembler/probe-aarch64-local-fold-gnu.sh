#!/usr/bin/env bash
# AArch64 function veneers and GOT entries need the original local symbol,
# even though ordinary relocations to local data may use STT_SECTION+addend.
# GNU GAS 2.42 predates both 2026 corrections, so it is a useful legacy
# evidence source but NOT a legitimate strict oracle for these four fixups.
set -Eeuo pipefail

: "${MINIAS:?MiniAS executable required}"
: "${AARCH64_AS:=aarch64-linux-gnu-as}"
command -v "$AARCH64_AS" >/dev/null
command -v readelf >/dev/null
command -v python3 >/dev/null
python3 -c 'import elftools' >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/section-fold.s" <<'ASM'
.section .text.callee,"ax",@progbits
.local local_function
.type local_function,%function
local_function:
    nop
.size local_function,.-local_function

.section .text.caller,"ax",@progbits
.globl caller
.type caller,%function
caller:
    bl local_function
    b local_function
    adrp x0,:got:local_data
    ldr x0,[x0,:got_lo12:local_data]
    ret

.data
.local local_data
local_data:
    .xword 0
ASM

"$MINIAS" --target=aarch64 -o "$work/mini.o" "$work/section-fold.s"
"$AARCH64_AS" -o "$work/gnu.o" "$work/section-fold.s"
readelf -rW "$work/mini.o" >"$work/mini.reloc"
readelf -rW "$work/gnu.o" >"$work/gnu.reloc"

# Always enforce correct MiniAS identity independently of the installed GNU
# version. Also reject unexpected GNU relocation counts, types, fields, or
# addends: a legacy exception only covers the identity of these four symbols.
gnu_identity=$(python3 - "$work/mini.reloc" "$work/gnu.reloc" <<'PY'
import pathlib
import re
import sys

expected = {
    "R_AARCH64_CALL26": "local_function",
    "R_AARCH64_JUMP26": "local_function",
    "R_AARCH64_ADR_GOT_PAGE": "local_data",
    "R_AARCH64_LD64_GOT_LO12_NC": "local_data",
}
by_source = []
for path in sys.argv[1:]:
    content = pathlib.Path(path).read_text()
    rows = []
    for relocation, symbol in expected.items():
        entries = [line.split() for line in content.splitlines()
                   if relocation in line.split()]
        if len(entries) != 1:
            raise SystemExit(
                f"AArch64 relocation count must be exactly 1 for "
                f"{relocation} in {path}: {entries}")
        fields = entries[0]
        if len(fields) < 7 or fields[-2] not in ('+', '-'):
            raise SystemExit(f"unsupported readelf relocation format in {path}: {fields}")
        rows.append((fields[0], relocation, fields[-2], fields[-1], fields[4]))
    all_relocations = [line for line in content.splitlines() if re.match(
        r'^\s*[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+R_', line)]
    if len(all_relocations) != 4:
        raise SystemExit(f"unexpected additional AArch64 relocations in {path}: "
                         f"{all_relocations}")
    by_source.append(rows)

mini, gnu = by_source
if [(row[:4]) for row in mini] != [(row[:4]) for row in gnu]:
    raise SystemExit(f"AArch64 relocation offsets/addends differ: mini={mini} gnu={gnu}")
for row, symbol in zip(mini, expected.values()):
    if row[4] != symbol:
        raise SystemExit(f"MiniAS lost required AArch64 symbol {symbol}: {row}")
print('CURRENT' if all(row[4] == symbol for row, symbol in
                       zip(gnu, expected.values())) else 'LEGACY')
PY
)

if python3 "$(dirname "$0")/elf_semantic_compare.py" --strict \
    "$work/gnu.o" "$work/mini.o" >"$work/semantic.json"; then
    if [[ "$gnu_identity" != CURRENT ]]; then
        echo 'MINIAS_AARCH64_LOCAL_SECTION_FOLD=FAIL oracle_false_green' >&2
        exit 1
    fi
    echo 'MINIAS_AARCH64_LOCAL_SECTION_FOLD=PASS Mini_symbol_identity=4/4 GNU_STRICT=PASS'
else
    if [[ "$gnu_identity" != LEGACY ]]; then
        echo 'MINIAS_AARCH64_LOCAL_SECTION_FOLD=FAIL modern_GNU_strict_mismatch' >&2
        python3 -m json.tool "$work/semantic.json" >&2
        exit 1
    fi
    # A legacy GNU GAS mismatch must be confined to symbol identity in
    # relocations. Do not silently ignore section, instruction, CFI, mapping,
    # group, symbol-table or alignment regressions under this exemption.
    python3 - "$work/semantic.json" <<'PY'
import json
import sys

result = json.load(open(sys.argv[1], encoding='utf-8'))
allowed = {'relocations', 'relocation_order'}
other = {key: value for key, value in result['dimensions'].items()
         if not value and key not in allowed}
if (other or not result['strict_section_alignment'] or
        not result['strict_relaxation_markers'] or
        not result['strict_mapping_symbols'] or
        not result['dimensions']['known_relocation_widths']):
    raise SystemExit('legacy GNU has unrelated strict differences: ' +
                     json.dumps(result.get('differences', {}), sort_keys=True))
PY
    echo "GNU_VERSION=$("$AARCH64_AS" --version | sed -n '1p')"
    echo 'GNU_2026_LEGACY_RELOCATION_EVIDENCE_BEGIN'
    sed -n '/^Relocation section/,/^$/p' "$work/gnu.reloc"
    echo 'GNU_2026_LEGACY_RELOCATION_EVIDENCE_END'
    echo 'MINIAS_AARCH64_LOCAL_SECTION_FOLD=PASS Mini_symbol_identity=4/4 GNU_STRICT=SKIP(legacy_GAS_function_or_GOT_section_fold)'
fi
