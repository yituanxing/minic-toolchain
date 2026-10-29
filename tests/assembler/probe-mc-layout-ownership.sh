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
    import re
    if re.search(r'(?:static\s+)?bool\s+'+n+r'\s*\(',driver):
        raise SystemExit('old driver layout: '+n)
for n in ('minias_parse_common_space','minias_parse_common_fill'):
    if n+'(' not in expr:raise SystemExit('DATA grammar missing in MCExpr: '+n)
for n in ('minias_compile_deferred_expr','minias_evaluate_deferred_expr',
          'minias_deferred_expr_destroy','minias_compile_variable_exprs',
          'minias_resolve_variable_exprs'):
    if n+'(' not in expr:raise SystemExit('persistent MCExpr owner missing: '+n)
if 'minias_resolve_variable_exprs(' not in layout:
    raise SystemExit('MCLayout failed to consume persistent variable expressions')
if 'minias_compile_variable_mc(' in expr or 'minias_resolve_variable_mc(' in layout:
    raise SystemExit('obsolete variable DATA text/legacy MC path survived')
emitter=driver[driver.index('static bool emit_data_stmt('):
               driver.index('static bool parse_numeric_label_id(')]
if 'minias_parse_common_fill(' in emitter or 'minias_parse_common_space(' in emitter:
    raise SystemExit('DATA re-parsed on final emission')
if 'variable_data_resolved' not in emitter:
    raise SystemExit('not emitting cached typed variable DATA')
if 'assembler/src/mc_layout.c' not in mk:raise SystemExit('missing MCLayout build')
if 'forced_long_branch_bitmap' not in layout or 'layout_anchor_graph' not in layout:
    raise SystemExit('Common layout still recomputes/linearly scans symbol anchors or forced branches')
if 'minias_validate_fragment_layout' not in layout:
    raise SystemExit('MCLayout must prove fragment placement before encoding')
if 'minias_solve_fragment_layout(' not in driver:
    raise SystemExit('missing Common layout fixed-point call')
print('MINIAS_MCLAYOUT_OWNER=PASS persistent=1 branch_size=target_callback')
PY
