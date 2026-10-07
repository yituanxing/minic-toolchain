#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

anchor = "static bool minic_specialize_inline_symbolic_calls(\n"
if text.count(anchor) != 1:
    raise SystemExit(f"expected one symbolic specialization function, found {text.count(anchor)}")

helper = r'''
static bool minic_inline_symbolic_index_parameter_local(
    const MinicC0Program *program,
    MinicExpressionId expression_id,
    MinicLocalId *local_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (program == NULL || local_id == NULL || depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return minic_inline_symbolic_index_parameter_local(
            program, expression->value.unary.operand, local_id, depth + 1U);
    }
    if (expression->kind != MINIC_EXPRESSION_LOCAL) {
        return false;
    }
    *local_id = expression->value.local_id;
    return true;
}

static bool minic_inline_symbolic_build_call_index(
    const MinicC0Program *program,
    size_t indexed_function_count,
    size_t **heads_out,
    size_t **next_out) {
    MinicFunctionId *parameter_owners = NULL;
    size_t *heads = NULL;
    size_t *next = NULL;
    size_t function_index;
    size_t expression_index;
    bool ok = false;

    if (program == NULL || heads_out == NULL || next_out == NULL ||
        indexed_function_count > program->function_count ||
        indexed_function_count > SIZE_MAX / sizeof(*heads) ||
        program->expression_count > SIZE_MAX / sizeof(*next) ||
        program->local_count > SIZE_MAX / sizeof(*parameter_owners)) {
        return false;
    }
    *heads_out = NULL;
    *next_out = NULL;
    heads = indexed_function_count == 0U
                ? NULL
                : (size_t *)malloc(indexed_function_count * sizeof(*heads));
    next = program->expression_count == 0U
               ? NULL
               : (size_t *)malloc(program->expression_count * sizeof(*next));
    parameter_owners = program->local_count == 0U
                           ? NULL
                           : (MinicFunctionId *)malloc(
                                 program->local_count * sizeof(*parameter_owners));
    if ((indexed_function_count != 0U && heads == NULL) ||
        (program->expression_count != 0U && next == NULL) ||
        (program->local_count != 0U && parameter_owners == NULL)) {
        goto done;
    }

    for (function_index = 0U; function_index < indexed_function_count; ++function_index) {
        heads[function_index] = SIZE_MAX;
    }
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        next[expression_index] = SIZE_MAX;
    }
    for (expression_index = 0U; expression_index < program->local_count;
         ++expression_index) {
        parameter_owners[expression_index] = MINIC_FUNCTION_INVALID;
    }
    for (function_index = 0U; function_index < indexed_function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        size_t parameter_index;

        if (!function->is_defined ||
            function->local_begin > program->local_count ||
            function->parameter_count > program->local_count - function->local_begin) {
            continue;
        }
        for (parameter_index = 0U; parameter_index < function->parameter_count;
             ++parameter_index) {
            parameter_owners[function->local_begin + parameter_index] =
                (MinicFunctionId)function_index;
        }
    }
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *call = &program->expressions[expression_index];
        MinicFunctionId owner = MINIC_FUNCTION_INVALID;
        bool ambiguous = false;
        size_t argument_index;

        if (call->kind != MINIC_EXPRESSION_CALL) {
            continue;
        }
        for (argument_index = 0U;
             argument_index < call->value.call.argument_count;
             ++argument_index) {
            MinicLocalId local_id;
            MinicFunctionId argument_owner;

            if (!minic_inline_symbolic_index_parameter_local(
                    program,
                    call->value.call.arguments[argument_index],
                    &local_id,
                    0U) ||
                local_id >= program->local_count) {
                continue;
            }
            argument_owner = parameter_owners[local_id];
            if (argument_owner == MINIC_FUNCTION_INVALID) {
                continue;
            }
            if (owner == MINIC_FUNCTION_INVALID) {
                owner = argument_owner;
            } else if (owner != argument_owner) {
                ambiguous = true;
                break;
            }
        }
        if (!ambiguous && owner != MINIC_FUNCTION_INVALID &&
            (size_t)owner < indexed_function_count) {
            next[expression_index] = heads[owner];
            heads[owner] = expression_index;
        }
    }

    *heads_out = heads;
    *next_out = next;
    heads = NULL;
    next = NULL;
    ok = true;

done:
    free(parameter_owners);
    free(next);
    free(heads);
    return ok;
}

'''
text = text.replace(anchor, helper + anchor, 1)

old = '''    {
        size_t caller_index;
        for (caller_index = original_function_count;
'''
new = '''    {
        size_t caller_index;
        size_t *symbolic_call_heads = NULL;
        size_t *symbolic_call_next = NULL;

        if (!minic_inline_symbolic_build_call_index(
                program,
                original_function_count,
                &symbolic_call_heads,
                &symbolic_call_next)) {
            return false;
        }
        for (caller_index = original_function_count;
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one symbolic transitive loop prologue, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;
                 ++nested_expression_index) {
'''
new = '''            for (nested_expression_index =
                     symbolic_call_heads[caller.specialization_source];
                 nested_expression_index != SIZE_MAX;
                 nested_expression_index =
                     symbolic_call_next[nested_expression_index]) {
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one symbolic full expression rescan, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''                if (variant_id == MINIC_FUNCTION_INVALID) {
                    if (!minic_add_inline_symbolic_specialization(
                            program,
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            &variant_id)) {
                        return false;
                    }
                    transitive_clone_count += 1U;
                }
'''
new = '''                if (variant_id == MINIC_FUNCTION_INVALID) {
                    if (!minic_add_inline_symbolic_specialization(
                            program,
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            &variant_id)) {
                        free(symbolic_call_next);
                        free(symbolic_call_heads);
                        return false;
                    }
                    transitive_clone_count += 1U;
                }
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one symbolic transitive add block, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''            }
        }
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
'''
new = '''            }
        }
        free(symbolic_call_next);
        free(symbolic_call_heads);
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
'''
if text.count(old) != 1:
    raise SystemExit(f"expected one symbolic transitive loop epilogue, found {text.count(old)}")
text = text.replace(old, new, 1)

p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_CALL_INDEX_V0=APPLIED")
