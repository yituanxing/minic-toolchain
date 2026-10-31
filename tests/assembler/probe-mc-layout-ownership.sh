#!/usr/bin/env bash
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
driver=Path('assembler/src/assembler.c').read_text()
layout=Path('assembler/src/mc_layout.c').read_text()
expr=Path('assembler/src/mc_expr.c').read_text()
internal=Path('assembler/src/minias_internal.h').read_text()
source=Path('assembler/src/source_graph_internal.h').read_text()
expansion=Path('assembler/src/source_expansion.c').read_text()
mk=Path('Makefile').read_text()
for fragment in ('INSTRUCTION','ALIGNMENT','ORIGIN','FIXED_DATA',
                 'FILL','SPACE','BYTES'):
    if 'MINIAS_FRAGMENT_'+fragment not in internal:
        raise SystemExit('Common Fragment discriminator missing '+fragment)
if 'fragment_kind' not in driver or 'fragment_kind' not in layout:
    raise SystemExit('layout/emission still ignores persistent FragmentKind')
for legacy in ('strcmp(stmt->op', 'strncmp(stmt->op'):
    if legacy in layout:
        raise SystemExit('MCLayout selects semantic fragment by source text: '+legacy)
if ('typedef struct MiniAsSourceFrame' not in source or
    'MiniAsSourceFrame physical;' not in source or
    expansion.count('bool minias_source_frame_process(') != 1 or
    'minias_source_frame_process(as,&graph->physical)' not in driver or
    expansion.count('minias_source_frame_process(as,&frame)') < 3 or
    'assembler/src/source_graph.c' not in mk or
    'assembler/src/source_expansion.c' not in mk):
    raise SystemExit('physical file and expansion inputs lack shared SourceFrame')
for old_reader in ('read_macro_block(', 'read_repeat_block('):
    if old_reader in driver:
        raise SystemExit('obsolete file-only source collector: '+old_reader)
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
    if any(n+'(' in owner for owner in (expr,driver,internal)):
        raise SystemExit('obsolete variable DATA eager text parser survived: '+n)
for n in ('minias_compile_deferred_expr','minias_evaluate_deferred_expr',
          'minias_deferred_expr_destroy','minias_compile_variable_exprs',
          'minias_resolve_variable_exprs'):
    if n+'(' not in expr:raise SystemExit('persistent MCExpr owner missing: '+n)
if 'minias_resolve_variable_exprs(' not in layout:
    raise SystemExit('MCLayout failed to consume persistent variable expressions')
if driver.count('minias_compile_variable_exprs(as,stmt)') != 1 or \
   'minias_resolve_variable_exprs(as,stmt' not in driver:
    raise SystemExit('variable DATA first pass must use one retained MCExpr AST')
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
print('MINIAS_MCLAYOUT_OWNER=PASS persistent=1 typed_fragments=1 branch_size=target_callback')
PY
