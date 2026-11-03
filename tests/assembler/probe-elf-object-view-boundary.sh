#!/usr/bin/env bash
set -Eeuo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source="$repo/tests/assembler/elf-object-view-compile.c"
generic_source="$repo/tests/assembler/mc-elf-policy-compile.c"
compiler=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

"$compiler" -std=c11 -pedantic-errors -Wall -Wextra -Werror -fsyntax-only \
    -I"$repo/assembler/src" "$source"
for case in SECTION_WRITE ELF_POLICY_WRITE SYMBOL_WRITE FIXUP_WRITE FILENAME_WRITE \
            TARGET_WRITE SESSION_LOOKUP EMISSION_ACCESS LEGACY_TARGET_POLICY; do
    if "$compiler" -std=c11 -pedantic-errors -Wall -Wextra -Werror \
        -fsyntax-only -I"$repo/assembler/src" -D"TRY_$case" "$source" \
        >"$work/$case.log" 2>&1; then
        echo "MINIAS_ELF_OBJECT_VIEW=FAIL mutable_or_broad_case=$case" >&2
        exit 1
    fi
done

"$compiler" -std=c11 -pedantic-errors -Wall -Wextra -Werror -fsyntax-only \
    -I"$repo/assembler/src" "$generic_source"
for case in ELF_CONSTANT ELF_MACHINE ELF_FIXUP ELF_FOLDING ELF_POLICY_MUTATE; do
    if "$compiler" -std=c11 -pedantic-errors -Wall -Wextra -Werror \
        -fsyntax-only -I"$repo/assembler/src" -D"TRY_$case" \
        "$generic_source" >"$work/$case.log" 2>&1; then
        echo "MINIAS_ELF_POLICY=FAIL leaked_generic_api=$case" >&2
        exit 1
    fi
done

python3 - "$repo" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
writer = (root / 'assembler/src/elf_writer.c').read_text()
driver = (root / 'assembler/src/assembler.c').read_text()
header = (root / 'assembler/src/minias_object_model.h').read_text()

# The serializer's entire call graph receives only one borrowed ObjectModel
# projection. This is not a spare MiniAs layout with writable ownership.
for pattern in (r'\bMiniAs\s*\*', r'\bas->stmts\b', r'\bas->emission\b',
                r'\bminias_emission_', r'\bminias_common_context_',
                r'\bminias_set_error\s*\('):
    if re.search(pattern, writer):
        raise SystemExit('MINIAS_ELF_OBJECT_VIEW=FAIL broad_serializer:' + pattern)
for name in ('sections', 'symbols', 'fixups', 'cu_file',
             'find_symbol_id', 'find_section', 'numeric_reference',
             'fragment_location', 'section_bytes'):
    if not re.search(r'object\.model\.' + name + r'\s*=', driver):
        raise SystemExit('MINIAS_ELF_OBJECT_VIEW=FAIL missing_projection:' + name)
for name in ('elf.policy', 'target_abi', 'spec', 'target_output_features'):
    if not re.search(r'object\.' + re.escape(name) + r'\s*=', driver):
        raise SystemExit('MINIAS_ELF_OBJECT_VIEW=FAIL missing_writer_request:' + name)
if not (driver.index('minias_finalize_target_output(&as,') <
        driver.index('minias_write_final_object(&as,')):
    raise SystemExit('MINIAS_ELF_OBJECT_VIEW=FAIL wrong_finalization_order')
if not re.search(r'const\s+MiniAsSection\s*\*\s*sections', header) or \
   not re.search(r'const\s+MiniAsSymbol\s*\*\s*symbols', header) or \
   not re.search(r'const\s+MiniAsFixup\s*\*\s*fixups', header):
    raise SystemExit('MINIAS_ELF_OBJECT_VIEW=FAIL not_const_borrows')
print('MINIAS_ELF_OBJECT_VIEW=PASS serializer_session=opaque '
      'borrowed_entities=3 source_layout=scalar diagnostics=returned '
      'owner_mutations=forbidden')
PY

echo 'MINIAS_ELF_OBJECT_VIEW_COMPILE=PASS negative=14 positive=2'
