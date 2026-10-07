#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:180]!r}")
    p.write_text(text.replace(old, new, 1))


# Carry a conservative source-AST symbolic address fact beside integer facts.
# The expression id always points at an immutable parsed expression rooted in a
# global object/function.  Shared FunctionBody ownership therefore stays intact.
replace_once(
    "src/frontend/ast.h",
    "    uint64_t specialization_integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool specialization_integer_known[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool is_integer_specialization;\n",
    "    uint64_t specialization_integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool specialization_integer_known[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    MinicExpressionId specialization_symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool specialization_symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool is_integer_specialization;\n",
)

compiler_helpers = r'''

static bool minic_inline_symbolic_static_expression_depth(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (program == NULL || target == NULL || depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT ||
        expression->kind == MINIC_EXPRESSION_FUNCTION) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return minic_inline_symbolic_static_expression_depth(
            program, target, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        return operand != NULL && operand->kind == MINIC_EXPRESSION_LOCAL && false;
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        if (operand == NULL) {
            return false;
        }
        if (operand->kind == MINIC_EXPRESSION_GLOBAL_OBJECT ||
            operand->kind == MINIC_EXPRESSION_FUNCTION) {
            return true;
        }
        if (operand->kind == MINIC_EXPRESSION_DEREFERENCE) {
            return minic_inline_symbolic_static_expression_depth(
                program, target, operand->value.unary.operand, depth + 1U);
        }
        if (operand->kind == MINIC_EXPRESSION_MEMBER) {
            return minic_inline_symbolic_static_expression_depth(
                program, target, operand->value.member.base, depth + 1U);
        }
        if (operand->kind == MINIC_EXPRESSION_SUBSCRIPT) {
            MinicConstValue index_value;
            return minic_inline_symbolic_static_expression_depth(
                       program, target, operand->value.subscript.base, depth + 1U) &&
                   minic_const_eval_integer(
                       program, target, operand->value.subscript.index, &index_value);
        }
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        MinicConstValue integer_value;
        if (minic_inline_symbolic_static_expression_depth(
                program, target, expression->value.binary.left, depth + 1U) &&
            minic_const_eval_integer(
                program, target, expression->value.binary.right, &integer_value)) {
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
            minic_const_eval_integer(
                program, target, expression->value.binary.left, &integer_value) &&
            minic_inline_symbolic_static_expression_depth(
                program, target, expression->value.binary.right, depth + 1U)) {
            return true;
        }
    }
    return false;
}

static bool minic_inline_symbolic_static_expression(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    MinicExpressionId expression_id) {
    return minic_inline_symbolic_static_expression_depth(
        program, target, expression_id, 0U);
}

static bool minic_inline_symbolic_argument_expression(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicExpressionId *symbolic_expression,
    bool *used_caller_fact,
    unsigned int depth) {
    const MinicExpression *expression;

    if (program == NULL || target == NULL || symbolic_expression == NULL ||
        used_caller_fact == NULL || depth > 32U) {
        return false;
    }
    *used_caller_fact = false;
    if (minic_inline_symbolic_static_expression(program, target, expression_id)) {
        *symbolic_expression = expression_id;
        return true;
    }
    if (caller == NULL || !caller->is_integer_specialization ||
        caller->specialization_source >= program->function_count) {
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
        return minic_inline_symbolic_argument_expression(
            program,
            target,
            caller,
            expression->value.unary.operand,
            symbolic_expression,
            used_caller_fact,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicFunction *source = &program->functions[caller->specialization_source];
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;

        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_symbolic_known[parameter_index]) {
            return false;
        }
        *symbolic_expression =
            caller->specialization_symbolic_expression[parameter_index];
        *used_caller_fact = true;
        return *symbolic_expression != MINIC_EXPRESSION_INVALID;
    }
    return false;
}

static bool minic_inline_symbolic_variant_matches(
    const MinicFunction *candidate,
    MinicFunctionId source_id,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    size_t parameter_count) {
    size_t i;

    if (candidate == NULL || !candidate->is_integer_specialization ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count) {
        return false;
    }
    for (i = 0U; i < parameter_count; ++i) {
        if (candidate->specialization_integer_known[i] != integer_known[i] ||
            (integer_known[i] &&
             candidate->specialization_integer_bits[i] != integer_bits[i]) ||
            candidate->specialization_symbolic_known[i] != symbolic_known[i] ||
            (symbolic_known[i] &&
             candidate->specialization_symbolic_expression[i] != symbolic_expression[i])) {
            return false;
        }
    }
    return true;
}

static bool minic_add_inline_symbolic_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    MinicFunctionId *specialized_id) {
    MinicFunction *specialized;
    size_t i;
    size_t ordinal;

    if (program == NULL || source_id >= program->function_count ||
        specialized_id == NULL) {
        return false;
    }
    ordinal = program->function_count;
    if (!minic_add_inline_integer_specialization(
            program,
            source_id,
            integer_known,
            integer_bits,
            ordinal,
            specialized_id)) {
        return false;
    }
    specialized = &program->functions[*specialized_id];
    for (i = 0U; i < specialized->parameter_count; ++i) {
        specialized->specialization_symbolic_known[i] = symbolic_known[i];
        specialized->specialization_symbolic_expression[i] =
            symbolic_known[i] ? symbolic_expression[i] : MINIC_EXPRESSION_INVALID;
    }
    return true;
}

static bool minic_specialize_inline_symbolic_calls(
    MinicC0Program *program,
    const MinicTargetInfo *target) {
    size_t expression_index;
    size_t initial_clone_count = 0U;
    size_t transitive_clone_count = 0U;
    size_t original_function_count;

    if (program == NULL || target == NULL) {
        return false;
    }
    original_function_count = program->function_count;

    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        MinicExpression *call = &program->expressions[expression_index];
        MinicFunctionId current_id;
        MinicFunctionId source_id;
        MinicFunctionId variant_id = MINIC_FUNCTION_INVALID;
        MinicFunction current;
        MinicFunction source;
        bool integer_known[MINIC_MAX_FUNCTION_PARAMETERS];
        uint64_t integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];
        bool symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicExpressionId symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];
        bool adds_symbol = false;
        size_t i;
        size_t candidate_index;

        if (call->kind != MINIC_EXPRESSION_CALL ||
            call->value.call.function_id == MINIC_FUNCTION_INVALID) {
            continue;
        }
        current_id = call->value.call.function_id;
        if (current_id >= program->function_count) {
            continue;
        }
        current = program->functions[current_id];
        source_id = current.is_integer_specialization
                        ? current.specialization_source
                        : current_id;
        if (source_id >= program->function_count) {
            continue;
        }
        source = program->functions[source_id];
        if (!source.is_defined || !source.is_internal || !source.is_inline ||
            source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID ||
            source.parameter_count == 0U ||
            source.parameter_count != call->value.call.argument_count) {
            continue;
        }
        (void)memset(integer_known, 0, sizeof(integer_known));
        (void)memset(integer_bits, 0, sizeof(integer_bits));
        (void)memset(symbolic_known, 0, sizeof(symbolic_known));
        for (i = 0U; i < MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
            symbolic_expression[i] = MINIC_EXPRESSION_INVALID;
        }
        if (current.is_integer_specialization) {
            for (i = 0U; i < source.parameter_count; ++i) {
                integer_known[i] = current.specialization_integer_known[i];
                integer_bits[i] = current.specialization_integer_bits[i];
                symbolic_known[i] = current.specialization_symbolic_known[i];
                symbolic_expression[i] =
                    current.specialization_symbolic_expression[i];
            }
        }
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicExpressionId argument_id = call->value.call.arguments[i];
            if (!minic_type_is_pointer(source.parameter_types[i]) ||
                !minic_inline_symbolic_static_expression(
                    program, target, argument_id)) {
                continue;
            }
            if (!symbolic_known[i] || symbolic_expression[i] != argument_id) {
                symbolic_known[i] = true;
                symbolic_expression[i] = argument_id;
                adds_symbol = true;
            }
        }
        if (!adds_symbol) {
            continue;
        }
        for (candidate_index = 0U; candidate_index < program->function_count;
             ++candidate_index) {
            if (minic_inline_symbolic_variant_matches(
                    &program->functions[candidate_index],
                    source_id,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    source.parameter_count)) {
                variant_id = candidate_index;
                break;
            }
        }
        if (variant_id == MINIC_FUNCTION_INVALID) {
            if (program->function_count >= SIZE_MAX - 1U ||
                !minic_add_inline_symbolic_specialization(
                    program,
                    source_id,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    &variant_id)) {
                return false;
            }
            initial_clone_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = variant_id;
    }

    {
        size_t caller_index;
        for (caller_index = original_function_count;
             caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            size_t nested_expression_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;
                 ++nested_expression_index) {
                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];
                MinicFunctionId current_id;
                MinicFunctionId source_id;
                MinicFunctionId variant_id = MINIC_FUNCTION_INVALID;
                MinicFunction current;
                MinicFunction source;
                bool integer_known[MINIC_MAX_FUNCTION_PARAMETERS];
                uint64_t integer_bits[MINIC_MAX_FUNCTION_PARAMETERS];
                bool symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];
                MinicExpressionId symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];
                bool used_caller_fact = false;
                bool adds_symbol = false;
                size_t i;
                size_t candidate_index;

                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                current_id = nested->value.call.function_id;
                if (current_id >= program->function_count) {
                    continue;
                }
                current = program->functions[current_id];
                source_id = current.is_integer_specialization
                                ? current.specialization_source
                                : current_id;
                if (source_id >= program->function_count) {
                    continue;
                }
                source = program->functions[source_id];
                if (!source.is_defined || !source.is_internal || !source.is_inline ||
                    source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID ||
                    source.parameter_count == 0U ||
                    source.parameter_count != nested->value.call.argument_count) {
                    continue;
                }
                (void)memset(integer_known, 0, sizeof(integer_known));
                (void)memset(integer_bits, 0, sizeof(integer_bits));
                (void)memset(symbolic_known, 0, sizeof(symbolic_known));
                for (i = 0U; i < MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
                    symbolic_expression[i] = MINIC_EXPRESSION_INVALID;
                }
                if (current.is_integer_specialization) {
                    for (i = 0U; i < source.parameter_count; ++i) {
                        integer_known[i] = current.specialization_integer_known[i];
                        integer_bits[i] = current.specialization_integer_bits[i];
                        symbolic_known[i] = current.specialization_symbolic_known[i];
                        symbolic_expression[i] =
                            current.specialization_symbolic_expression[i];
                    }
                }
                for (i = 0U; i < source.parameter_count; ++i) {
                    MinicExpressionId resolved_expression;
                    bool argument_used_caller_fact = false;
                    if (!minic_type_is_pointer(source.parameter_types[i]) ||
                        !minic_inline_symbolic_argument_expression(
                            program,
                            target,
                            &caller,
                            nested->value.call.arguments[i],
                            &resolved_expression,
                            &argument_used_caller_fact,
                            0U)) {
                        continue;
                    }
                    used_caller_fact =
                        used_caller_fact || argument_used_caller_fact;
                    if (!symbolic_known[i] ||
                        symbolic_expression[i] != resolved_expression) {
                        symbolic_known[i] = true;
                        symbolic_expression[i] = resolved_expression;
                        adds_symbol = true;
                    }
                }
                if (!used_caller_fact || !adds_symbol) {
                    continue;
                }
                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {
                    if (minic_inline_symbolic_variant_matches(
                            &program->functions[candidate_index],
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            source.parameter_count)) {
                        variant_id = candidate_index;
                        break;
                    }
                }
                if (variant_id == MINIC_FUNCTION_INVALID) {
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
            }
        }
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_SYMBOLIC_SPECIALIZATION_V0 direct=%zu transitive=%zu functions=%zu\n",
                      initial_clone_count,
                      transitive_clone_count,
                      program->function_count);
    }
    return true;
}
'''

replace_once(
    "src/compiler/compiler.c",
    "static bool minic_inline_integer_source_has_residual_reference(\n",
    compiler_helpers + "\nstatic bool minic_inline_integer_source_has_residual_reference(\n",
)

replace_once(
    "src/compiler/compiler.c",
    "    if (success && !minic_finalize_inline_integer_specialization_references(&program)) {\n",
    "    if (success && !minic_specialize_inline_symbolic_calls(&program, target_info)) {\n"
    "        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,\n"
    "                             \"cannot specialize inline symbolic address call sites\");\n"
    "        success = false;\n"
    "    }\n"
    "    if (success && !minic_finalize_inline_integer_specialization_references(&program)) {\n",
)

core_selector = r'''

static bool core_inline_symbolic_argument_expression(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicExpressionId *symbolic_expression,
    unsigned int depth) {
    const MinicExpression *expression;
    const MinicFunction *caller;
    const MinicFunction *source;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || symbolic_expression == NULL || depth > 32U) {
        return false;
    }
    caller = context->source_function;
    if (!caller->is_integer_specialization ||
        caller->specialization_source >= context->body->program->function_count) {
        return false;
    }
    source = &context->body->program->functions[caller->specialization_source];
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_inline_symbolic_argument_expression(
            context,
            expression->value.unary.operand,
            symbolic_expression,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;
        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_symbolic_known[parameter_index]) {
            return false;
        }
        *symbolic_expression =
            caller->specialization_symbolic_expression[parameter_index];
        return *symbolic_expression != MINIC_EXPRESSION_INVALID;
    }
    return false;
}

static const MinicFunction *core_select_transitive_symbolic_specialization(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    const MinicFunction *current_callee) {
    const MinicC0Program *program;
    MinicFunctionId source_id;
    const MinicFunction *source;
    const MinicFunction *best;
    size_t best_symbol_count;
    MinicExpressionId actual_symbol[MINIC_MAX_FUNCTION_PARAMETERS];
    bool actual_known[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t i;
    size_t candidate_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression == NULL || expression->kind != MINIC_EXPRESSION_CALL ||
        current_callee == NULL || !current_callee->is_integer_specialization) {
        return current_callee;
    }
    program = context->body->program;
    source_id = current_callee->specialization_source;
    if (source_id >= program->function_count) {
        return current_callee;
    }
    source = &program->functions[source_id];
    if (source->parameter_count != expression->value.call.argument_count ||
        source->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
        return current_callee;
    }
    (void)memset(actual_known, 0, sizeof(actual_known));
    for (i = 0U; i < MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
        actual_symbol[i] = MINIC_EXPRESSION_INVALID;
    }
    for (i = 0U; i < source->parameter_count; ++i) {
        if (!minic_type_is_pointer(source->parameter_types[i])) {
            continue;
        }
        actual_known[i] = core_inline_symbolic_argument_expression(
            context,
            expression->value.call.arguments[i],
            &actual_symbol[i],
            0U);
    }
    best = current_callee;
    best_symbol_count = 0U;
    for (i = 0U; i < source->parameter_count; ++i) {
        if (current_callee->specialization_symbolic_known[i]) {
            best_symbol_count += 1U;
        }
    }
    for (candidate_index = 0U; candidate_index < program->function_count;
         ++candidate_index) {
        const MinicFunction *candidate = &program->functions[candidate_index];
        size_t symbol_count = 0U;
        bool compatible = true;

        if (!candidate->is_integer_specialization ||
            candidate->specialization_source != source_id ||
            candidate->parameter_count != source->parameter_count) {
            continue;
        }
        for (i = 0U; i < source->parameter_count; ++i) {
            if (candidate->specialization_integer_known[i] !=
                    current_callee->specialization_integer_known[i] ||
                (candidate->specialization_integer_known[i] &&
                 candidate->specialization_integer_bits[i] !=
                    current_callee->specialization_integer_bits[i])) {
                compatible = false;
                break;
            }
            if (!candidate->specialization_symbolic_known[i]) {
                continue;
            }
            symbol_count += 1U;
            if (!actual_known[i] ||
                candidate->specialization_symbolic_expression[i] != actual_symbol[i]) {
                compatible = false;
                break;
            }
        }
        if (compatible && symbol_count > best_symbol_count) {
            best = candidate;
            best_symbol_count = symbol_count;
        }
    }
    if (best != current_callee && getenv("MINIC_INLINE_SPEC_SYMBOLIC_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "INLINE_SPEC_SYMBOLIC_SELECT caller=%s from=%s to=%s symbols=%zu\n",
                      context->source_function->name != NULL
                          ? context->source_function->name
                          : "?",
                      current_callee->name != NULL ? current_callee->name : "?",
                      best->name != NULL ? best->name : "?",
                      best_symbol_count);
    }
    return best;
}
'''

replace_once(
    "src/core/core_lower.c",
    "static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,\n",
    core_selector + "\nstatic MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,\n",
)

replace_once(
    "src/core/core_lower.c",
    "    callee = core_select_transitive_integer_specialization(context, expression, callee);\n",
    "    callee = core_select_transitive_integer_specialization(context, expression, callee);\n"
    "    callee = core_select_transitive_symbolic_specialization(context, expression, callee);\n",
)

# Resolve symbolic-address parameters at the asm immediate boundary.  This is
# intentionally target-text specialization, not a generic pointer constant fold.
replace_once(
    "src/core/core_lower_asm.c",
    '#include "frontend/expression_semantics.h"\n',
    '#include "frontend/expression_semantics.h"\n#include "target/data_layout.h"\n',
)

asm_helpers = r'''

static bool core_inline_asm_parameter_index(
    const MinicCoreLowerContext *context,
    MinicLocalId local_id,
    size_t *parameter_index) {
    const MinicFunction *source;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || parameter_index == NULL ||
        !context->source_function->is_integer_specialization ||
        context->source_function->specialization_source >=
            context->body->program->function_count) {
        return false;
    }
    source = &context->body->program->functions[
        context->source_function->specialization_source];
    if (local_id < source->local_begin) {
        return false;
    }
    *parameter_index = local_id - source->local_begin;
    return *parameter_index < source->parameter_count &&
           *parameter_index < MINIC_MAX_FUNCTION_PARAMETERS;
}

static bool core_inline_asm_specialized_integer(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    int64_t *value,
    unsigned int depth) {
    const MinicExpression *expression;
    MinicConstValue constant;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || value == NULL || depth > 32U) {
        return false;
    }
    if (minic_const_eval_integer(
            context->body->program, context->target, expression_id, &constant) &&
        minic_const_value_as_int64(
            context->body->program, context->target, &constant, value)) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_inline_asm_specialized_integer(
            context, expression->value.unary.operand, value, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        size_t parameter_index;
        const MinicFunction *function = context->source_function;
        const MinicFunction *source;
        MinicConstValue specialized;
        if (!core_inline_asm_parameter_index(
                context, expression->value.local_id, &parameter_index) ||
            !function->specialization_integer_known[parameter_index]) {
            return false;
        }
        source = &context->body->program->functions[function->specialization_source];
        specialized.type = source->parameter_types[parameter_index];
        specialized.bits = function->specialization_integer_bits[parameter_index];
        return minic_const_value_as_int64(
            context->body->program, context->target, &specialized, value);
    }
    return false;
}

static bool core_inline_asm_symbolic_address_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    const char **symbol,
    int64_t *addend,
    unsigned int depth);

static bool core_inline_asm_symbolic_lvalue_address(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    const char **symbol,
    int64_t *addend,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        symbol == NULL || addend == NULL || depth > 32U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object =
            minic_c0_program_global_object(program, expression->value.global_object_id);
        if (object == NULL || object->name == NULL || object->name_length == 0U) {
            return false;
        }
        *symbol = object->name;
        *addend = 0;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_DEREFERENCE) {
        return core_inline_asm_symbolic_address_depth(
            context,
            expression->value.unary.operand,
            symbol,
            addend,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *record =
            minic_c0_program_record(program, expression->value.member.record_id);
        const MinicExpression *base =
            minic_c0_program_expression(program, expression->value.member.base);
        size_t field_offset;
        int64_t base_addend;
        bool base_ok;

        if (record == NULL || base == NULL ||
            !minic_data_layout_record_field_offset(
                minic_target_info_data_layout(context->target),
                program,
                record,
                expression->value.member.field_index,
                &field_offset) || field_offset > (size_t)INT64_MAX) {
            return false;
        }
        if (minic_type_is_pointer(base->type)) {
            base_ok = core_inline_asm_symbolic_address_depth(
                context,
                expression->value.member.base,
                symbol,
                &base_addend,
                depth + 1U);
        } else {
            base_ok = core_inline_asm_symbolic_lvalue_address(
                context,
                expression->value.member.base,
                symbol,
                &base_addend,
                depth + 1U);
        }
        if (!base_ok || base_addend > INT64_MAX - (int64_t)field_offset) {
            return false;
        }
        *addend = base_addend + (int64_t)field_offset;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base =
            minic_c0_program_expression(program, expression->value.subscript.base);
        MinicType element_type;
        size_t element_size;
        size_t element_alignment;
        int64_t index;
        int64_t base_addend;
        int64_t scaled;

        if (base == NULL || !minic_type_is_pointer(base->type) ||
            !minic_type_pointee(base->type, &element_type) ||
            !minic_data_layout_type(
                minic_target_info_data_layout(context->target),
                program,
                element_type,
                &element_size,
                &element_alignment) ||
            element_size > (size_t)INT64_MAX ||
            !core_inline_asm_specialized_integer(
                context, expression->value.subscript.index, &index, depth + 1U) ||
            !core_inline_asm_symbolic_address_depth(
                context,
                expression->value.subscript.base,
                symbol,
                &base_addend,
                depth + 1U)) {
            return false;
        }
        if (index != 0 &&
            ((index > 0 && index > INT64_MAX / (int64_t)element_size) ||
             (index < 0 && index < INT64_MIN / (int64_t)element_size))) {
            return false;
        }
        scaled = index * (int64_t)element_size;
        if ((scaled > 0 && base_addend > INT64_MAX - scaled) ||
            (scaled < 0 && base_addend < INT64_MIN - scaled)) {
            return false;
        }
        *addend = base_addend + scaled;
        return true;
    }
    return false;
}

static bool core_inline_asm_symbolic_address_depth(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    const char **symbol,
    int64_t *addend,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || symbol == NULL || addend == NULL || depth > 32U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object =
            minic_c0_program_global_object(program, expression->value.global_object_id);
        if (object == NULL || object->name == NULL || object->name_length == 0U) {
            return false;
        }
        *symbol = object->name;
        *addend = 0;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        const MinicFunction *function =
            minic_c0_program_function(program, expression->value.function_id);
        if (function == NULL) {
            return false;
        }
        *symbol = function->assembler_name != NULL && function->assembler_name_length != 0U
                      ? function->assembler_name
                      : function->name;
        *addend = 0;
        return *symbol != NULL && **symbol != '\0';
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return core_inline_asm_symbolic_address_depth(
            context,
            expression->value.unary.operand,
            symbol,
            addend,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand =
            minic_c0_program_expression(program, expression->value.unary.operand);
        if (operand == NULL || operand->kind != MINIC_EXPRESSION_LOCAL) {
            return false;
        }
        return core_inline_asm_symbolic_address_depth(
            context,
            expression->value.unary.operand,
            symbol,
            addend,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        size_t parameter_index;
        MinicExpressionId specialized_expression;
        if (!core_inline_asm_parameter_index(
                context, expression->value.local_id, &parameter_index) ||
            !context->source_function->specialization_symbolic_known[parameter_index]) {
            return false;
        }
        specialized_expression =
            context->source_function->specialization_symbolic_expression[parameter_index];
        if (specialized_expression == MINIC_EXPRESSION_INVALID ||
            specialized_expression == expression_id) {
            return false;
        }
        return core_inline_asm_symbolic_address_depth(
            context, specialized_expression, symbol, addend, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        return core_inline_asm_symbolic_lvalue_address(
            context,
            expression->value.unary.operand,
            symbol,
            addend,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        int64_t integer;
        int64_t base_addend;
        if (core_inline_asm_symbolic_address_depth(
                context,
                expression->value.binary.left,
                symbol,
                &base_addend,
                depth + 1U) &&
            core_inline_asm_specialized_integer(
                context,
                expression->value.binary.right,
                &integer,
                depth + 1U)) {
            if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) {
                if (integer == INT64_MIN) {
                    return false;
                }
                integer = -integer;
            }
            if ((integer > 0 && base_addend > INT64_MAX - integer) ||
                (integer < 0 && base_addend < INT64_MIN - integer)) {
                return false;
            }
            *addend = base_addend + integer;
            return true;
        }
    }
    return false;
}

static bool core_inline_asm_specialized_symbolic_text(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    char *buffer,
    size_t capacity,
    const char **text_out,
    size_t *length_out) {
    const char *symbol;
    int64_t addend;
    int written;

    if (buffer == NULL || capacity == 0U || text_out == NULL || length_out == NULL ||
        !core_inline_asm_symbolic_address_depth(
            context, expression_id, &symbol, &addend, 0U)) {
        return false;
    }
    if (addend == 0) {
        *text_out = symbol;
        *length_out = strlen(symbol);
        return *length_out != 0U;
    }
    written = snprintf(buffer,
                       capacity,
                       addend > 0 ? "%s+%" PRId64 : "%s%" PRId64,
                       symbol,
                       addend);
    if (written < 0 || (size_t)written >= capacity) {
        return false;
    }
    *text_out = buffer;
    *length_out = (size_t)written;
    return true;
}
'''

replace_once(
    "src/core/core_lower_asm.c",
    "static bool core_inline_asm_immediate_text(\n",
    asm_helpers + "\nstatic bool core_inline_asm_immediate_text(\n",
)

replace_once(
    "src/core/core_lower_asm.c",
    "    symbol = core_inline_asm_symbolic_immediate_name(\n"
    "        context->body->program, context->target, operand->expression);\n"
    "    if (symbol == NULL) {\n"
    "        return false;\n"
    "    }\n"
    "    *text_out = symbol;\n"
    "    *length_out = strlen(symbol);\n"
    "    return *length_out != 0U;\n",
    "    if (core_inline_asm_specialized_symbolic_text(\n"
    "            context,\n"
    "            operand->expression,\n"
    "            integer_text,\n"
    "            integer_capacity,\n"
    "            text_out,\n"
    "            length_out)) {\n"
    "        return true;\n"
    "    }\n"
    "    symbol = core_inline_asm_symbolic_immediate_name(\n"
    "        context->body->program, context->target, operand->expression);\n"
    "    if (symbol == NULL) {\n"
    "        return false;\n"
    "    }\n"
    "    *text_out = symbol;\n"
    "    *length_out = strlen(symbol);\n"
    "    return *length_out != 0U;\n",
)

print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_ADDRESS_V0=APPLIED")
