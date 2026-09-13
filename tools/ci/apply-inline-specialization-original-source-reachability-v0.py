#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:180]!r}")
    p.write_text(text.replace(old, new, 1))


# A caller specialization can reveal a constant argument to a nested call even
# when that nested call still names the original inline source. The transitive
# pass previously considered only nested callees that were already
# specializations, so it could never create the first variant for chains such
# as outer(ext=34) -> riscv_has_extension_unlikely(ext).
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
                    if (current_callee_id >= original_function_count ||
                        !current_callee.is_defined || !current_callee.is_internal ||
                        !current_callee.is_inline || current_callee.is_variadic ||
                        current_callee.alias_target != MINIC_FUNCTION_INVALID) {
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

# Do not globally rewrite a shared source AST expression using facts from one
# specialization caller. Instead, permit an original inline callee to select a
# generated variant from the current Core-lowering context's proven constants.
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
    source = &program->functions[source_id];
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
    }
    if (source_id >= program->function_count) {
        return current_callee;
    }
    source = &program->functions[source_id];
    if (!current_callee->is_integer_specialization &&
        (!source->is_defined || !source->is_internal || !source->is_inline ||
         source->is_variadic || source->alias_target != MINIC_FUNCTION_INVALID)) {
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

# A specialization source that has only ordinary call references is not a true
# emission root. Keep address/alias/global-relocation/force-emit uses as roots,
# then let the already-lowered Core call graph decide whether the generic source
# body is actually reachable. This removes dead always-inline originals without
# dropping a source that still has a real fallback call.
helper_anchor = '''static bool minic_prune_inline_specializations_from_core(
'''
helper = r'''static bool minic_inline_source_has_specialization(
    const MinicC0Program *program, MinicFunctionId source_id) {
    size_t function_index;

    if (program == NULL || source_id >= program->function_count) {
        return false;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *candidate = &program->functions[function_index];
        if (candidate->is_integer_specialization &&
            candidate->specialization_source == source_id) {
            return true;
        }
    }
    return false;
}

static bool minic_inline_source_has_noncall_reference(
    const MinicC0Program *program, MinicFunctionId source_id) {
    size_t expression_index;
    size_t function_index;
    size_t object_index;

    if (program == NULL || source_id >= program->function_count) {
        return true;
    }
    if (program->entry_function == source_id || program->functions[source_id].force_emit) {
        return true;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        if (program->functions[function_index].alias_target == source_id) {
            return true;
        }
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                (MinicFunctionId)relocation->target_id == source_id) {
                return true;
            }
        }
    }
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *expression = &program->expressions[expression_index];
        if (expression->kind == MINIC_EXPRESSION_FUNCTION &&
            expression->value.function_id == source_id) {
            return true;
        }
    }
    return false;
}

static bool minic_inline_source_is_core_conditional_root(
    const MinicC0Program *program, MinicFunctionId function_id) {
    const MinicFunction *function;

    if (program == NULL || function_id >= program->function_count) {
        return false;
    }
    function = &program->functions[function_id];
    return !function->is_integer_specialization && function->is_defined &&
           function->is_internal && function->is_inline && function->is_referenced &&
           minic_inline_source_has_specialization(program, function_id) &&
           !minic_inline_source_has_noncall_reference(program, function_id);
}

'''
replace_once(
    "src/compiler/compiler.c",
    helper_anchor,
    helper + helper_anchor,
)
replace_once(
    "src/compiler/compiler.c",
    '''        if (function->is_integer_specialization || !function->is_defined ||
            (function->is_internal && !function->is_referenced)) {
            continue;
        }
''',
    '''        if (function->is_integer_specialization || !function->is_defined ||
            (function->is_internal && !function->is_referenced) ||
            minic_inline_source_is_core_conditional_root(program, function_index)) {
            continue;
        }
''',
)
replace_once(
    "src/compiler/compiler.c",
    '''        if (!function->is_integer_specialization) {
            continue;
        }
        function->is_referenced = reachable[function_index];
''',
    '''        if (!function->is_integer_specialization &&
            !minic_inline_source_is_core_conditional_root(program, function_index)) {
            continue;
        }
        function->is_referenced = reachable[function_index];
''',
)

print("MINIC_INLINE_SPECIALIZATION_ORIGINAL_SOURCE_REACHABILITY_V0=APPLIED")
