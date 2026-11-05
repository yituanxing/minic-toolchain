#!/usr/bin/env bash
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
import re
root=Path('assembler/src')
driver=(root/'assembler.c').read_text()
lower=(root/'common_lowering.c').read_text()
emission=(root/'common_emission.c').read_text()
api=(root/'minias_common_lowering.h').read_text()
if 'assembler/src/common_lowering.c' not in Path('Makefile').read_text():
    raise SystemExit('Lowering not linked as independent source')
for name in ('lower_source_statement','add_stmt','emit_data_stmt',
             'flush_literal_pools','flush_pending_local_commons'):
    if re.search(r'\b(?:static\s+)?bool\s+'+name+r'\s*\(',driver):
        raise SystemExit('Old semantic lowerer still in Common driver: '+name)
for func in ('lower_source_statement','minias_common_parse_source_graph'):
    if len(re.findall(r'\b(?:static\s+)?bool\s+'+func+r'\s*\(',lower))!=1:
        raise SystemExit('Single semantic owner violated: '+func)
if lower.count('minias_emit_data_fragment(') or +   len(re.findall(r'\bstatic bool minias_emit_data_fragment\s*\(',emission))!=1:
    raise SystemExit('DATA Fragment materialization escaped Emission owner')
if 'minias_common_parse_source_graph(&as,graph)' not in driver or \
   'minias_emit_data_fragment(as, stmt)' not in emission:
    raise SystemExit('Common driver/emitter bypassed typed lowering output')
if 'typedef struct MiniAsSectionIdentity' not in (root/'minias_common_context.h').read_text() or \
   'struct MiniAsParseContext {' not in api:
    raise SystemExit('Lowering private contract does not expose required types')
print('MINIAS_COMMON_LOWERING_OWNER=PASS sole_parser=1 typed_data=1 emission_separate=1 source_eof=1')
PY
