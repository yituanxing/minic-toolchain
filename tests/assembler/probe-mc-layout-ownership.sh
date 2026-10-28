#!/usr/bin/env bash
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
driver=Path('assembler/src/assembler.c').read_text()
layout=Path('assembler/src/mc_layout.c').read_text()
expr=Path('assembler/src/mc_expr.c').read_text()
mk=Path('Makefile').read_text()
for n in ('minias_solve_fragment_layout','minias_relayout_fragments',
          'minias_resolve_deferred_data','collect_out_of_range_branches',
          'minias_finalize_subsection_layout'):
    if n+'(' not in layout:raise SystemExit('missing MCLayout '+n)
for n in ('minias_relayout_fragments','minias_resolve_deferred_data',
          'collect_out_of_range_branches','minias_finalize_subsection_layout'):
    if n+'(' in driver:raise SystemExit('old driver layout: '+n)
for n in ('minias_parse_common_space','minias_parse_common_fill'):
    if n+'(' not in expr:raise SystemExit('DATA grammar missing in MCExpr: '+n)
if 'assembler/src/mc_layout.c' not in mk:raise SystemExit('missing MCLayout build')
if 'minias_solve_fragment_layout(' not in driver:
    raise SystemExit('missing Common layout fixed-point call')
print('MINIAS_MCLAYOUT_OWNER=PASS persistent=1 branch_size=target_callback')
PY
