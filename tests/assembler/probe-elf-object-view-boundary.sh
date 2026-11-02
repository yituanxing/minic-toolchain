#!/usr/bin/env bash
set -Eeuo pipefail

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source="$repo/tests/assembler/elf-object-view-compile.c"
compiler=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

"$compiler" -std=c11 -pedantic-errors -Wall -Wextra -Werror -fsyntax-only \
    -I"$repo/assembler/src" "$source"
for case in SECTION_WRITE SYMBOL_WRITE FIXUP_WRITE FILENAME_WRITE \
            TARGET_WRITE SESSION_LOOKUP EMISSION_ACCESS; do
    if "$compiler" -std=c11 -pedantic-errors -Wall -Wextra -Werror \
        -fsyntax-only -I"$repo/assembler/src" -D"TRY_$case" "$source" \
        >"$work/$case.log" 2>&1; then
        echo "MINIAS_ELF_OBJECT_VIEW=FAIL mutable_or_broad_case=$case" >&2
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
header = (root / 'assembler/src/minias_elf_object_view.h').read_text()

# The serializer's entire call graph receives only one borrowed ObjectModel
# projection. This is not a spare MiniAs layout with writable ownership.
for pattern in (r'\bMiniAs\s*\*', r'\bas->stmts\b', r'\bas->emission\b',
                r'\bminias_emission_', r'\bminias_common_context_',
                r'\bminias_set_error\s*\('):
    if re.search(pattern, writer):
        raise SystemExit('MINIAS_ELF_OBJECT_VIEW=FAIL broad_serializer:' + pattern)
for name in ('sections', 'symbols', 'fixups', 'cu_file', 'policy', 'target_abi',
             'find_symbol', 'find_section', 'fragment_location', 'section_bytes'):
    if not re.search(r'object\.' + name + r'\s*=', driver):
        raise SystemExit('MINIAS_ELF_OBJECT_VIEW=FAIL missing_projection:' + name)
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

echo 'MINIAS_ELF_OBJECT_VIEW_COMPILE=PASS negative=7 positive=1'
