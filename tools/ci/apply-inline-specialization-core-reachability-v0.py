#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

anchor = '''static bool minic_validate_core_functions(const char *input_path,
'''
helper = r'''
static MinicFunctionId minic_core_named_function_id(
    const MinicC0Program *program, const char *name, size_t name_length) {
    size_t function_index;

    if (program == NULL || name == NULL || name_length == 0U) {
        return MINIC_FUNCTION_INVALID;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        const char *symbol_name = minic_c0_function_symbol_name(function);
        size_t symbol_name_length;

        if (symbol_name == NULL) {
            continue;
        }
        symbol_name_length = function->assembler_name != NULL
                                 ? function->assembler_name_length
                                 : function->name_length;
        if (symbol_name_length == name_length &&
            memcmp(symbol_name, name, name_length) == 0) {
            return function_index;
        }
    }
    return MINIC_FUNCTION_INVALID;
}

static bool minic_prune_inline_specializations_from_core(
    MinicC0Program *program, const MinicCoreFunctionSet *set) {
    bool *reachable = NULL;
    bool *processed = NULL;
    MinicFunctionId *queue = NULL;
    size_t queue_count = 0U;
    size_t queue_cursor = 0U;
    size_t function_index;
    size_t kept = 0U;
    size_t pruned = 0U;
    bool success = false;

    if (program == NULL || set == NULL || set->function_count != program->function_count ||
        (set->function_count != 0U &&
         (set->functions == NULL || set->statuses == NULL))) {
        return false;
    }
    reachable = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                               sizeof(*reachable));
    processed = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                               sizeof(*processed));
    queue = (MinicFunctionId *)malloc(
        (program->function_count == 0U ? 1U : program->function_count) * sizeof(*queue));
    if (reachable == NULL || processed == NULL || queue == NULL) {
        goto done;
    }

    /* The pre-specialization AST reachability result remains the root policy for
     * ordinary functions. Specialization clones are deliberately not roots: a
     * clone survives only when an actually-lowered Core function references it. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization || !function->is_defined ||
            (function->is_internal && !function->is_referenced)) {
            continue;
        }
        reachable[function_index] = true;
        queue[queue_count++] = function_index;
    }

    while (queue_cursor < queue_count) {
        MinicFunctionId caller_id = queue[queue_cursor++];
        const MinicCoreFunction *core;
        size_t callee_index;
        size_t symbol_index;

        if (caller_id >= set->function_count || processed[caller_id]) {
            continue;
        }
        processed[caller_id] = true;
        if (set->statuses[caller_id] != MINIC_CORE_LOWER_OK) {
            continue;
        }
        core = &set->functions[caller_id];
        for (callee_index = 0U; callee_index < core->callee_count; ++callee_index) {
            const MinicCoreCallee *callee = &core->callees[callee_index];
            MinicFunctionId target = minic_core_named_function_id(
                program, callee->name, callee->name_length);
            if (target == MINIC_FUNCTION_INVALID || target >= program->function_count ||
                !program->functions[target].is_defined || reachable[target]) {
                continue;
            }
            if (queue_count >= program->function_count) {
                goto done;
            }
            reachable[target] = true;
            queue[queue_count++] = target;
        }
        for (symbol_index = 0U; symbol_index < core->function_symbol_count; ++symbol_index) {
            const MinicCoreFunctionSymbol *symbol = &core->function_symbols[symbol_index];
            MinicFunctionId target = minic_core_named_function_id(
                program, symbol->name, symbol->name_length);
            if (target == MINIC_FUNCTION_INVALID || target >= program->function_count ||
                !program->functions[target].is_defined || reachable[target]) {
                continue;
            }
            if (queue_count >= program->function_count) {
                goto done;
            }
            reachable[target] = true;
            queue[queue_count++] = target;
        }
    }

    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (!function->is_integer_specialization) {
            continue;
        }
        function->is_referenced = reachable[function_index];
        if (function->is_referenced) {
            kept += 1U;
        } else {
            pruned += 1U;
        }
    }
    if (kept != 0U || pruned != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_SPECIALIZATION_CORE_REACHABILITY_V0 kept=%zu pruned=%zu\n",
                      kept,
                      pruned);
    }
    success = true;

done:
    free(queue);
    free(processed);
    free(reachable);
    return success;
}

'''
count = text.count(anchor)
if count != 1:
    raise SystemExit(f"expected one core validation anchor, found {count}")
text = text.replace(anchor, helper + anchor, 1)

old = '''    if (minic_bootstrap_trace_enabled()) {
        minic_bootstrap_trace_stage(input_path,
                                    "core-set",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "core-validate", "begin", program.function_count);
'''
new = '''    if (minic_bootstrap_trace_enabled()) {
        minic_bootstrap_trace_stage(input_path,
                                    "core-set",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success && !minic_prune_inline_specializations_from_core(&program, &core_set)) {
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "cannot prune inline specialization Core reachability");
        success = false;
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "core-validate", "begin", program.function_count);
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one core-set/validate anchor, found {count}")
text = text.replace(old, new, 1)
p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_CORE_REACHABILITY_V0=APPLIED")
