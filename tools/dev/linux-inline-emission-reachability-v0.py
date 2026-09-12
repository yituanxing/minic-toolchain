#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"{path}: expected one anchor, found {text.count(old)}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/function_body.h",
    "bool minic_c0_program_validate_function_body_ownership(const MinicC0Program *program);\n",
    "bool minic_c0_program_validate_function_body_ownership(const MinicC0Program *program);\n"
    "\n"
    "/* Recompute static-inline emission from semantic reachability. Parser-time\n"
    " * references are lexical and may originate in dead header inline bodies;\n"
    " * only references reachable from functions that are emitted independently,\n"
    " * global function-pointer initializers, aliases, or the entry function should\n"
    " * cause an internal inline definition to be emitted. */\n"
    "bool minic_c0_program_recompute_inline_emission_references(MinicC0Program *program);\n",
)

append = r'''

static bool enqueue_emission_function(const MinicC0Program *program,
                                      MinicFunctionId function_id,
                                      bool *reachable,
                                      MinicFunctionId *queue,
                                      size_t *queue_count) {
    const MinicFunction *function;

    if (program == NULL || reachable == NULL || queue == NULL || queue_count == NULL ||
        function_id >= program->function_count) {
        return false;
    }
    function = &program->functions[function_id];
    if (!function->is_defined || reachable[function_id]) {
        return true;
    }
    if (*queue_count >= program->function_count) {
        return false;
    }
    reachable[function_id] = true;
    queue[(*queue_count)++] = function_id;
    return true;
}

bool minic_c0_program_recompute_inline_emission_references(MinicC0Program *program) {
    MinicFunctionBodyValidation validation;
    bool *reachable = NULL;
    bool *processed = NULL;
    MinicFunctionId *queue = NULL;
    size_t queue_count = 0U;
    size_t queue_cursor = 0U;
    size_t function_index;
    size_t object_index;
    bool success = false;

    if (program == NULL || !initialize_validation(program, &validation) ||
        !assign_local_owners(&validation)) {
        return false;
    }
    reachable = calloc(program->function_count == 0U ? 1U : program->function_count,
                       sizeof(*reachable));
    processed = calloc(program->function_count == 0U ? 1U : program->function_count,
                       sizeof(*processed));
    queue = malloc((program->function_count == 0U ? 1U : program->function_count) *
                   sizeof(*queue));
    if (reachable == NULL || processed == NULL || queue == NULL) {
        goto done;
    }

    /* Preserve the compiler's established emission policy for every definition
     * except internal inline functions. Those are the only entities for which
     * parser-time lexical references were previously used as an emission gate. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        if (function->is_defined && !(function->is_internal && function->is_inline) &&
            !enqueue_emission_function(
                program, function_index, reachable, queue, &queue_count)) {
            goto done;
        }
        if (function->alias_target != MINIC_FUNCTION_INVALID &&
            !enqueue_emission_function(
                program, function->alias_target, reachable, queue, &queue_count)) {
            goto done;
        }
    }
    if (program->entry_function != MINIC_FUNCTION_INVALID &&
        !enqueue_emission_function(
            program, program->entry_function, reachable, queue, &queue_count)) {
        goto done;
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                !enqueue_emission_function(program,
                                           (MinicFunctionId)relocation->target_id,
                                           reachable,
                                           queue,
                                           &queue_count)) {
                goto done;
            }
        }
    }

    while (queue_cursor < queue_count) {
        MinicFunctionId function_id = queue[queue_cursor++];
        size_t expression_index;

        if (processed[function_id]) {
            continue;
        }
        processed[function_id] = true;
        if (!validate_one_function(&validation, function_id)) {
            goto done;
        }
        for (expression_index = 0U; expression_index < validation.expression_work_count;
             ++expression_index) {
            const MinicExpression *expression = minic_c0_program_expression(
                program, validation.expression_work[expression_index]);
            MinicFunctionId target = MINIC_FUNCTION_INVALID;

            if (expression == NULL) {
                goto done;
            }
            if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
                target = expression->value.function_id;
            } else if (expression->kind == MINIC_EXPRESSION_CALL &&
                       expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
                target = expression->value.call.function_id;
            }
            if (target != MINIC_FUNCTION_INVALID &&
                !enqueue_emission_function(program, target, reachable, queue, &queue_count)) {
                goto done;
            }
        }
    }

    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (function->is_defined && function->is_internal && function->is_inline) {
            function->is_referenced = reachable[function_index];
        }
    }
    success = true;

done:
    free(queue);
    free(processed);
    free(reachable);
    destroy_validation(&validation);
    return success;
}
'''

p = Path("src/frontend/function_body.c")
text = p.read_text()
if "minic_c0_program_recompute_inline_emission_references" in text:
    raise SystemExit("function_body.c: reachability implementation already present")
p.write_text(text + append)

replace_once(
    "src/compiler/compiler.c",
    "    if (success && !minic_c0_program_validate_function_body_ownership(&program)) {\n"
    "        minic_set_diagnostic(\n"
    "            diagnostic, input_path, 1U, 1U, \"normalized FunctionBody ownership is invalid\");\n"
    "        success = false;\n"
    "    }\n"
    "    if (success) {\n"
    "        minic_bootstrap_trace_stage(input_path, \"core-set\", \"begin\", program.function_count);\n"
    "    }\n",
    "    if (success && !minic_c0_program_validate_function_body_ownership(&program)) {\n"
    "        minic_set_diagnostic(\n"
    "            diagnostic, input_path, 1U, 1U, \"normalized FunctionBody ownership is invalid\");\n"
    "        success = false;\n"
    "    }\n"
    "    if (success && !minic_c0_program_recompute_inline_emission_references(&program)) {\n"
    "        minic_set_diagnostic(diagnostic,\n"
    "                             input_path,\n"
    "                             1U,\n"
    "                             1U,\n"
    "                             \"cannot compute inline emission reachability\");\n"
    "        success = false;\n"
    "    }\n"
    "    if (success) {\n"
    "        minic_bootstrap_trace_stage(input_path, \"core-set\", \"begin\", program.function_count);\n"
    "    }\n",
)

print("LINUX_INLINE_EMISSION_PATCH=APPLIED")
