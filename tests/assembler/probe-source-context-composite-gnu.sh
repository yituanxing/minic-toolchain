#!/usr/bin/env bash
# The Source/Common boundary must survive a rare source replay while .if,
# .altmacro, macro+REPT, source-site IDs, and .end share one SourceContext.
set -Eeuo pipefail
: "${MINIAS:?point MINIAS at the exact-head minic-as executable}"
for tool in as objcopy cmp readelf python3 timeout; do
    command -v "$tool" >/dev/null
done

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source="$repo/tests/assembler/fixtures/source-context-composite-gnu.S"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

as --64 -o "$work/gnu.o" "$source"
MINIAS_TRACE_RELAYOUT=1 timeout 20s "$MINIAS" --target=x86_64 \
    -o "$work/mini.o" "$source" 2>"$work/mini.trace"

objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.text"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.text"
cmp "$work/mini.text" "$work/gnu.text"
objcopy -j .data -O binary "$work/gnu.o" "$work/gnu.data"
objcopy -j .data -O binary "$work/mini.o" "$work/mini.data"
cmp "$work/mini.data" "$work/gnu.data"

grep -qx 'minias-source-reparse:pass=1' "$work/mini.trace"
grep -Eq '^minias-source-graph:file-reads=1 sites=[0-9]+ expansions=[0-9]+ replay-passes=1 site-drifts=0 conditional-changes=0$' "$work/mini.trace"

python3 - "$work/mini.o" "$work/gnu.o" "$work/mini.text" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

names = {'source_context_entry', 'source_context_span', 'source_context_far'}

def symbols(path):
    rows = []
    output = subprocess.check_output(['readelf', '-sW', path], text=True)
    for line in output.splitlines():
        columns = line.split()
        if len(columns) < 8 or not re.fullmatch(r'[0-9]+:', columns[0]):
            continue
        if columns[7] in names:
            rows.append((columns[7], columns[1], columns[3],
                         columns[4], columns[6]))
        if columns[7] == 'source_context_unreachable':
            raise SystemExit(f'.end failed to discard trailing label: {path}')
    if {row[0] for row in rows} != names:
        raise SystemExit(f'missing source-context symbols: {path}: {rows}')
    # The ELF writer owns local symbol-table ordering. Compare source-visible
    # symbol meaning, not incidental symtab ordinals.
    return sorted(rows)

mini, gnu, text = sys.argv[1:]
mini_symbols, gnu_symbols = symbols(mini), symbols(gnu)
if mini_symbols != gnu_symbols:
    raise SystemExit(f'SourceGraph symbol mismatch: MiniAS={mini_symbols} GNU={gnu_symbols}')
if len(Path(text).read_bytes()) != 263:
    raise SystemExit('source replay changed JE/REPT/.end resulting text size')
span = next(row for row in mini_symbols if row[0] == 'source_context_span')
if int(span[1], 16) != 6:
    raise SystemExit(f'lexical .set did not follow grown branch: {span}')
PY

echo 'MINIAS_SOURCE_CONTEXT_COMPOSITE=PASS GNU_text_data=exact GNU_symbols=exact altmacro_percent=1 macro_rept=1 replay=1 physical_reads=1 site_drifts=0 end=truncated'
