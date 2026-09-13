#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:180]!r}")
    p.write_text(text.replace(old, new, 1))

# The symbolic selector previously only refined a call that already targeted an
# integer/symbolic specialization. Shared AST call nodes can still point at the
# original internal inline function even though caller-specific symbolic variants
# were created by the transitive pass. Generalize selection at Core lowering,
# exactly like the integer base-callee selector: a base inline callee starts with
# zero baked facts and may select a compatible symbolic variant using facts from
# the current caller specialization.
replace_once(
    "src/core/core_lower.c",
    '''    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression == NULL || expression->kind != MINIC_EXPRESSION_CALL ||
        current_callee == NULL || !current_callee->is_integer_specialization) {
        return current_callee;
    }
    program = context->body->program;
    source_id = current_callee->specialization_source;
    if (source_id >= program->function_count) {
        return current_callee;
    }
''',
    '''    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression == NULL || expression->kind != MINIC_EXPRESSION_CALL ||
        current_callee == NULL) {
        return current_callee;
    }
    program = context->body->program;
    if (current_callee->is_integer_specialization) {
        source_id = current_callee->specialization_source;
    } else {
        source_id = expression->value.call.function_id;
        if (source_id >= program->function_count ||
            current_callee != &program->functions[source_id] ||
            !current_callee->is_defined || !current_callee->is_internal ||
            !current_callee->is_inline || current_callee->is_variadic ||
            current_callee->alias_target != MINIC_FUNCTION_INVALID) {
            return current_callee;
        }
    }
    if (source_id >= program->function_count) {
        return current_callee;
    }
''',
)

replace_once(
    "src/core/core_lower.c",
    '''    best = current_callee;
    best_symbol_count = 0U;
    for (i = 0U; i < source->parameter_count; ++i) {
        if (current_callee->specialization_symbolic_known[i]) {
            best_symbol_count += 1U;
        }
    }
''',
    '''    best = current_callee;
    best_symbol_count = 0U;
    if (current_callee->is_integer_specialization) {
        for (i = 0U; i < source->parameter_count; ++i) {
            if (current_callee->specialization_symbolic_known[i]) {
                best_symbol_count += 1U;
            }
        }
    }
''',
)

replace_once(
    "src/core/core_lower.c",
    '''            if (candidate->specialization_integer_known[i] !=
                    current_callee->specialization_integer_known[i] ||
                (candidate->specialization_integer_known[i] &&
                 candidate->specialization_integer_bits[i] !=
                    current_callee->specialization_integer_bits[i])) {
                compatible = false;
                break;
            }
''',
    '''            if (current_callee->is_integer_specialization) {
                if (candidate->specialization_integer_known[i] !=
                        current_callee->specialization_integer_known[i] ||
                    (candidate->specialization_integer_known[i] &&
                     candidate->specialization_integer_bits[i] !=
                        current_callee->specialization_integer_bits[i])) {
                    compatible = false;
                    break;
                }
            } else if (candidate->specialization_integer_known[i]) {
                /* If an integer fact was available, the integer selector that
                 * runs immediately before this selector would already have
                 * redirected the base call. Do not guess one here. */
                compatible = false;
                break;
            }
''',
)

print("MINIC_INLINE_SPECIALIZATION_BASE_CALLEE_SYMBOLIC_V0=APPLIED")
