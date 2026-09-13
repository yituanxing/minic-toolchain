#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:180]!r}")
    p.write_text(text.replace(old, new, 1))


# The transitive pass already knows how to refine an existing specialization.
# Generalize the same source/fact machinery to a nested call that still points
# at the original internal inline function. Shared AST calls are deliberately
# not rewritten here: per-caller Core lowering selects the compatible clone.
replace_once(
    "src/compiler/compiler.c",
    '''                current_callee = program->functions[current_callee_id];
                if (!current_callee.is_integer_specialization ||
                    current_callee.specialization_source >= original_function_count) {
                    continue;
                }
                nested_source_id = current_callee.specialization_source;
                nested_source = program->functions[nested_source_id];
''',
    '''                current_callee = program->functions[current_callee_id];
                if (current_callee.is_integer_specialization) {
                    if (current_callee.specialization_source >= original_function_count) {
                        continue;
                    }
                    nested_source_id = current_callee.specialization_source;
                } else {
                    if (current_callee_id >= original_function_count) {
                        continue;
                    }
                    nested_source_id = current_callee_id;
                }
                nested_source = program->functions[nested_source_id];
''',
)
replace_once(
    "src/compiler/compiler.c",
    '''                    if (!current_callee.specialization_integer_known[parameter_index] ||
                        current_callee.specialization_integer_bits[parameter_index] !=
                            argument_value.bits) {
                        adds_fact = true;
                    }
''',
    '''                    if (!current_callee.is_integer_specialization ||
                        !current_callee.specialization_integer_known[parameter_index] ||
                        current_callee.specialization_integer_bits[parameter_index] !=
                            argument_value.bits) {
                        adds_fact = true;
                    }
''',
)

# Core lowering is the safe place to redirect a shared base CALL: the current
# caller specialization/local-fact state is known here. Start from zero facts
# for a base inline callee, then reuse the existing most-specific compatible
# specialization search.
replace_once(
    "src/core/core_lower.c",
    '''    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression == NULL || current_callee == NULL ||
        !current_callee->is_integer_specialization ||
        expression->kind != MINIC_EXPRESSION_CALL) {
        return current_callee;
    }
    program = context->body->program;
    source_id = current_callee->specialization_source;
    if (source_id >= program->function_count) {
        return current_callee;
    }
''',
    '''    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression == NULL || current_callee == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL) {
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
    best_known_count = 0U;
    for (parameter_index = 0U; parameter_index < source->parameter_count; ++parameter_index) {
        if (current_callee->specialization_integer_known[parameter_index]) {
            best_known_count += 1U;
        }
    }
''',
    '''    best = current_callee;
    best_known_count = 0U;
    if (current_callee->is_integer_specialization) {
        for (parameter_index = 0U; parameter_index < source->parameter_count;
             ++parameter_index) {
            if (current_callee->specialization_integer_known[parameter_index]) {
                best_known_count += 1U;
            }
        }
    }
''',
)

print("MINIC_INLINE_SPECIALIZATION_BASE_CALLEE_INTEGER_V0=APPLIED")
