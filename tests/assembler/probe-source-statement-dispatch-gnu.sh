#!/usr/bin/env bash
# One Source-owned lexical dispatch must preserve GNU label timing, skipped
# conditionals, macro/repeat recursion and the SourceSite event identity.
set -Eeuo pipefail
: "${MINIAS:?point MINIAS at the exact-head minic-as executable}"
for tool in as objcopy cmp readelf python3 timeout; do
    command -v "$tool" >/dev/null
done

repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source="$repo/tests/assembler/fixtures/source-statement-dispatch-gnu.S"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

as --64 -o "$work/gnu.o" "$source"
MINIAS_TRACE_RELAYOUT=1 timeout 20s "$MINIAS" --target=x86_64 \
    -o "$work/mini.o" "$source" 2>"$work/mini.trace"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.text"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.text"
cmp "$work/gnu.text" "$work/mini.text"
grep -Eq '^minias-source-graph:file-reads=1 sites=[0-9]+ expansions=[0-9]+ replay-passes=0 site-drifts=0 conditional-changes=0$' "$work/mini.trace"

python3 - "$work/mini.o" "$work/gnu.o" "$work/mini.text" <<'PY'
from pathlib import Path
import re
import subprocess
import sys

names = {
    'source_dispatch_entry', 'first_label', 'second_label',
    'spaced_label',
    'if_label_a', 'if_label_b', 'rept_a', 'rept_b',
    'irp_a', 'irp_b', 'string_anchor', 'alt_anchor',
    'noalt_anchor', 'purge_anchor', 'end_anchor',
}
hidden = {'hidden_a', 'hidden_b', 'hidden_after_end'}

def symbols(path):
    rows = []
    for line in subprocess.check_output(['readelf', '-sW', path], text=True).splitlines():
        columns = line.split()
        if len(columns) < 8 or not re.fullmatch(r'[0-9]+:', columns[0]):
            continue
        if columns[7] in hidden:
            raise SystemExit(f'inactive/.end label leaked: {path}: {columns[7]}')
        if columns[7] in names:
            rows.append((columns[7], columns[1], columns[3],
                         columns[4], columns[6]))
    if {row[0] for row in rows} != names:
        raise SystemExit(f'missing source-label bindings in {path}: {rows}')
    return sorted(rows)

mini, gnu, text = sys.argv[1:]
if symbols(mini) != symbols(gnu):
    raise SystemExit('source dispatch label semantics diverged from GNU')
payload = Path(text).read_bytes()
if not payload.startswith(bytes.fromhex('11 12 00 22 23 33 33 44 55')):
    raise SystemExit('label/macro/.if/.rept/.irp byte ordering diverged')
if b'target:colon;is quoted\x00' not in payload:
    raise SystemExit('quoted target colon/semicolon was split as a label/statement')
if not payload.endswith(b'\x66'):
    raise SystemExit('.altmacro expansion or .end trailing source changed')
PY

echo 'MINIAS_SOURCE_STATEMENT_DISPATCH=PASS GNU_text=exact GNU_labels=exact active_inactive=1 numeric_and_spaced_labels=1 multi_label=1 macro_rept_irp=1 quoted_colon_semicolon=1 source_end=1'
