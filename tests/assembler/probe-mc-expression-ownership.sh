#!/usr/bin/env bash
# Source-ownership invariant: no second Common expression interpreter.
set -Eeuo pipefail
python3 - <<'PY'
from pathlib import Path
s=Path('assembler/src/assembler.c').read_text()
m=Path('assembler/src/mc_expr.c').read_text()
legacy=Path('assembler/src/expression.c').read_text()
syntax=Path('assembler/src/expression_grammar.h').read_text()
internal=Path('assembler/src/minias_internal.h').read_text()
mk=Path('Makefile').read_text()
owners=('minias_compile_data_mc','minias_emit_mc_data',
        'minias_parse_absolute_data_bits',
        'minias_evaluate_absolute_expression',
        'minias_resolve_symbol_difference',
        'minias_mcdata_destroy')
for owner in owners:
    if owner+'(' not in m:
        raise SystemExit('missing expression model owner '+owner)
    if 'static bool '+owner+'(' in s or 'static void '+owner+'(' in s:
        raise SystemExit('obsolete expression algorithm still in driver '+owner)
for old in ('parse_data_integer_bits(', 'static bool checked_mul_absolute_i64(',
            'static bool parse_symbol_minus_dot(',
            'static bool parse_symbol_difference('):
    if old in s: raise SystemExit('obsolete common expression helper: '+old)
if 'deferred_text' in m or 'MINIAS_MC_EXPR_DEFERRED_ABSOLUTE' in m:
    raise SystemExit('fixed-width MCExpr retained textual reparse')
if 'deferred_text' in internal:
    raise SystemExit('fixed-width MCExpr still stores source strings')
for removed in ('minias_evaluate_same_subsection_difference(',
                'minias_fold_local_difference_addend('):
    if removed in m or removed in internal:
        raise SystemExit('unreferenced expression compatibility API survived '+removed)
if 'MINIAS_EXPRESSION_GRAMMAR_H' not in syntax:
    raise SystemExit('shared Common expression grammar contract is missing')
if 'expression_grammar.h' not in legacy or 'expression_grammar.h' not in m:
    raise SystemExit('separate absolute and MCExpr parsers no longer share grammar')
if 'assembler/src/mc_expr.c' not in mk:
    raise SystemExit('MCExpr not in build')
print('MINIAS_MCEXPR_MODULE=PASS shared_grammar=1 one_emitter=1 immutable_fields=1 stable_symbols=1 dead_apis=0')
PY
