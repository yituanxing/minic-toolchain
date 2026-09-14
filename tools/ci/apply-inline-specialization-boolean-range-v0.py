#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


# A range fact is weaker than an exact integer specialization: it records only
# that the incoming scalar is the result of a C _Bool value conversion, hence
# its value is in {0,1}.  Keep it on specialization clones beside exact and
# symbolic facts so it can flow through nested always-inline helpers.
replace_once(
    "src/frontend/ast.h",
    "    MinicExpressionId specialization_symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool specialization_symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool is_integer_specialization;\n",
    "    MinicExpressionId specialization_symbolic_expression[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool specialization_symbolic_known[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool specialization_boolean_range_known[MINIC_MAX_FUNCTION_PARAMETERS];\n"
    "    bool is_integer_specialization;\n",
    "boolean range metadata",
)

compiler_helpers = r'''

static bool minic_inline_boolean_range_argument(
    const MinicC0Program *program,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    bool *used_caller_fact,
    unsigned int depth) {
    const MinicExpression *expression;

    if (program == NULL || used_caller_fact == NULL || depth > 32U) {
        return false;
    }
    *used_caller_fact = false;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (minic_type_is_bool_integer(expression->type)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return minic_inline_boolean_range_argument(
            program,
            caller,
            expression->value.unary.operand,
            used_caller_fact,
            depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL && caller != NULL &&
        caller->is_integer_specialization &&
        caller->specialization_source < program->function_count) {
        const MinicFunction *source = &program->functions[caller->specialization_source];
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;

        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_boolean_range_known[parameter_index]) {
            return false;
        }
        *used_caller_fact = true;
        return true;
    }
    return false;
}

static void minic_inline_boolean_copy_existing_facts(
    const MinicFunction *current,
    size_t parameter_count,
    bool *integer_known,
    uint64_t *integer_bits,
    bool *symbolic_known,
    MinicExpressionId *symbolic_expression,
    bool *boolean_range_known) {
    size_t i;

    (void)memset(integer_known, 0,
                 MINIC_MAX_FUNCTION_PARAMETERS * sizeof(*integer_known));
    (void)memset(integer_bits, 0,
                 MINIC_MAX_FUNCTION_PARAMETERS * sizeof(*integer_bits));
    (void)memset(symbolic_known, 0,
                 MINIC_MAX_FUNCTION_PARAMETERS * sizeof(*symbolic_known));
    (void)memset(boolean_range_known, 0,
                 MINIC_MAX_FUNCTION_PARAMETERS * sizeof(*boolean_range_known));
    for (i = 0U; i < MINIC_MAX_FUNCTION_PARAMETERS; ++i) {
        symbolic_expression[i] = MINIC_EXPRESSION_INVALID;
    }
    if (current == NULL || !current->is_integer_specialization) {
        return;
    }
    for (i = 0U; i < parameter_count; ++i) {
        integer_known[i] = current->specialization_integer_known[i];
        integer_bits[i] = current->specialization_integer_bits[i];
        symbolic_known[i] = current->specialization_symbolic_known[i];
        symbolic_expression[i] = current->specialization_symbolic_expression[i];
        boolean_range_known[i] =
            current->specialization_boolean_range_known[i];
    }
}

static bool minic_inline_boolean_range_variant_matches(
    const MinicFunction *candidate,
    MinicFunctionId source_id,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    const bool *boolean_range_known,
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
             candidate->specialization_symbolic_expression[i] != symbolic_expression[i]) ||
            candidate->specialization_boolean_range_known[i] !=
                boolean_range_known[i]) {
            return false;
        }
    }
    return true;
}

static bool minic_add_inline_boolean_range_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    const bool *boolean_range_known,
    MinicFunctionId *specialized_id) {
    MinicFunction *specialized;
    size_t i;

    if (!minic_add_inline_symbolic_specialization(
            program,
            source_id,
            integer_known,
            integer_bits,
            symbolic_known,
            symbolic_expression,
            specialized_id)) {
        return false;
    }
    specialized = &program->functions[*specialized_id];
    for (i = 0U; i < specialized->parameter_count; ++i) {
        specialized->specialization_boolean_range_known[i] =
            boolean_range_known[i];
    }
    return true;
}

static bool minic_specialize_inline_boolean_range_calls(
    MinicC0Program *program,
    const MinicTargetInfo *target) {
    size_t expression_index;
    size_t direct_clone_count = 0U;
    size_t transitive_clone_count = 0U;

    if (program == NULL || target == NULL) {
        return false;
    }

    /* First, calls whose source expression itself has _Bool type are safe to
       rewrite globally: that {0,1} property does not depend on caller state. */
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
        bool boolean_range_known[MINIC_MAX_FUNCTION_PARAMETERS];
        bool adds_range = false;
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
        minic_inline_boolean_copy_existing_facts(
            &current,
            source.parameter_count,
            integer_known,
            integer_bits,
            symbolic_known,
            symbolic_expression,
            boolean_range_known);
        for (i = 0U; i < source.parameter_count; ++i) {
            const MinicExpression *argument;
            bool used_caller_fact = false;

            if (!minic_type_is_integer(source.parameter_types[i])) {
                continue;
            }
            argument = minic_c0_program_expression(
                program, call->value.call.arguments[i]);
            if (argument == NULL || !minic_type_is_bool_integer(argument->type) ||
                !minic_inline_boolean_range_argument(
                    program,
                    NULL,
                    call->value.call.arguments[i],
                    &used_caller_fact,
                    0U)) {
                continue;
            }
            if (!boolean_range_known[i]) {
                boolean_range_known[i] = true;
                adds_range = true;
            }
        }
        if (!adds_range) {
            continue;
        }
        for (candidate_index = 0U; candidate_index < program->function_count;
             ++candidate_index) {
            if (minic_inline_boolean_range_variant_matches(
                    &program->functions[candidate_index],
                    source_id,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    boolean_range_known,
                    source.parameter_count)) {
                variant_id = candidate_index;
                break;
            }
        }
        if (variant_id == MINIC_FUNCTION_INVALID) {
            if (direct_clone_count + transitive_clone_count >= 1024U ||
                !minic_add_inline_boolean_range_specialization(
                    program,
                    source_id,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    boolean_range_known,
                    &variant_id)) {
                return false;
            }
            direct_clone_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = variant_id;
    }

    /* Then close range facts through already-specialized callers.  Do not
       rewrite the shared source AST here; Core lowering selects the matching
       variant using the actual caller specialization. */
    {
        size_t caller_index;
        for (caller_index = 0U; caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            bool caller_has_range = false;
            size_t i;
            size_t nested_expression_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            for (i = 0U; i < caller.parameter_count; ++i) {
                caller_has_range = caller_has_range ||
                    caller.specialization_boolean_range_known[i];
            }
            if (!caller_has_range) {
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
                bool boolean_range_known[MINIC_MAX_FUNCTION_PARAMETERS];
                bool used_caller_fact = false;
                bool adds_range = false;
                size_t parameter_index;
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
                minic_inline_boolean_copy_existing_facts(
                    &current,
                    source.parameter_count,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    boolean_range_known);
                for (parameter_index = 0U;
                     parameter_index < source.parameter_count;
                     ++parameter_index) {
                    bool argument_used_caller_fact = false;
                    if (!minic_type_is_integer(source.parameter_types[parameter_index]) ||
                        !minic_inline_boolean_range_argument(
                            program,
                            &caller,
                            nested->value.call.arguments[parameter_index],
                            &argument_used_caller_fact,
                            0U)) {
                        continue;
                    }
                    used_caller_fact =
                        used_caller_fact || argument_used_caller_fact;
                    if (!boolean_range_known[parameter_index]) {
                        boolean_range_known[parameter_index] = true;
                        adds_range = true;
                    }
                }
                if (!used_caller_fact || !adds_range) {
                    continue;
                }
                for (candidate_index = 0U;
                     candidate_index < program->function_count;
                     ++candidate_index) {
                    if (minic_inline_boolean_range_variant_matches(
                            &program->functions[candidate_index],
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            boolean_range_known,
                            source.parameter_count)) {
                        variant_id = candidate_index;
                        break;
                    }
                }
                if (variant_id == MINIC_FUNCTION_INVALID) {
                    if (direct_clone_count + transitive_clone_count >= 1024U ||
                        !minic_add_inline_boolean_range_specialization(
                            program,
                            source_id,
                            integer_known,
                            integer_bits,
                            symbolic_known,
                            symbolic_expression,
                            boolean_range_known,
                            &variant_id)) {
                        return false;
                    }
                    transitive_clone_count += 1U;
                }
            }
        }
    }

    if (direct_clone_count != 0U || transitive_clone_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_BOOLEAN_RANGE_V0 direct=%zu transitive=%zu functions=%zu\n",
                      direct_clone_count,
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
    "boolean range compiler helpers",
)

replace_once(
    "src/compiler/compiler.c",
    "    if (success && !minic_finalize_inline_integer_specialization_references(&program)) {\n",
    "    if (success && !minic_specialize_inline_boolean_range_calls(&program, target_info)) {\n"
    "        minic_set_diagnostic(diagnostic, input_path, 1U, 1U,\n"
    "                             \"cannot specialize inline boolean range call sites\");\n"
    "        success = false;\n"
    "    }\n"
    "    if (success && !minic_finalize_inline_integer_specialization_references(&program)) {\n",
    "boolean range compiler pass",
)

core_helpers = r'''

static bool core_boolean_range_expression(
    const MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (minic_type_is_bool_integer(expression->type)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_boolean_range_expression(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL &&
        context->source_function != NULL &&
        context->source_function->is_integer_specialization &&
        context->source_function->specialization_source <
            context->body->program->function_count) {
        const MinicFunction *source = &context->body->program->functions[
            context->source_function->specialization_source];
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;

        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        return parameter_index < source->parameter_count &&
               parameter_index < MINIC_MAX_FUNCTION_PARAMETERS &&
               context->source_function->specialization_boolean_range_known[
                   parameter_index];
    }
    return false;
}

static bool core_boolean_compare_values(
    const MinicCoreLowerContext *context,
    MinicBinaryOperator operator_kind,
    MinicType common_type,
    const MinicConstValue *left,
    const MinicConstValue *right,
    bool *predicate) {
    if (context == NULL || left == NULL || right == NULL || predicate == NULL) {
        return false;
    }
    if (operator_kind == MINIC_BINARY_EQUAL ||
        operator_kind == MINIC_BINARY_NOT_EQUAL) {
        *predicate = left->bits == right->bits;
        if (operator_kind == MINIC_BINARY_NOT_EQUAL) {
            *predicate = !*predicate;
        }
        return true;
    }
    if (minic_type_is_signed_integer(common_type)) {
        int64_t left_signed;
        int64_t right_signed;
        if (!core_const_signed_value(context, left, &left_signed) ||
            !core_const_signed_value(context, right, &right_signed)) {
            return false;
        }
        switch (operator_kind) {
        case MINIC_BINARY_LESS: *predicate = left_signed < right_signed; return true;
        case MINIC_BINARY_LESS_EQUAL: *predicate = left_signed <= right_signed; return true;
        case MINIC_BINARY_GREATER: *predicate = left_signed > right_signed; return true;
        case MINIC_BINARY_GREATER_EQUAL: *predicate = left_signed >= right_signed; return true;
        default: return false;
        }
    }
    switch (operator_kind) {
    case MINIC_BINARY_LESS: *predicate = left->bits < right->bits; return true;
    case MINIC_BINARY_LESS_EQUAL: *predicate = left->bits <= right->bits; return true;
    case MINIC_BINARY_GREATER: *predicate = left->bits > right->bits; return true;
    case MINIC_BINARY_GREATER_EQUAL: *predicate = left->bits >= right->bits; return true;
    default: return false;
    }
}
'''

# core_const_signed_value is already present immediately before the local-aware
# evaluator, so insert the range helpers at the evaluator definition.
p = Path("src/core/core_lower.c")
text = p.read_text()
evaluator_anchor = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
if text.count(evaluator_anchor) != 1:
    raise SystemExit(f"boolean range evaluator anchor count={text.count(evaluator_anchor)}")
text = text.replace(evaluator_anchor, core_helpers + "\n" + evaluator_anchor, 1)
start = text.find(evaluator_anchor)
comparison_anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
'''
pos = text.find(comparison_anchor, start)
if pos < 0:
    raise SystemExit("boolean range comparison anchor missing")
range_comparison = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        const MinicExpression *right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        bool left_range = core_boolean_range_expression(
            context, expression->value.binary.left, 0U);
        bool right_range = core_boolean_range_expression(
            context, expression->value.binary.right, 0U);

        if (left_expression != NULL && right_expression != NULL &&
            left_range != right_range) {
            MinicExpressionId constant_id = left_range
                                                ? expression->value.binary.right
                                                : expression->value.binary.left;
            MinicConstValue constant_value;
            MinicConstValue constant_common;
            MinicConstValue range_zero;
            MinicConstValue range_one;
            MinicConstValue zero_common;
            MinicConstValue one_common;
            MinicType range_type = left_range ? left_expression->type
                                              : right_expression->type;
            MinicType constant_type = left_range ? right_expression->type
                                                 : left_expression->type;
            MinicType common_type;
            bool predicate_zero;
            bool predicate_one;

            range_zero.type = range_type;
            range_zero.bits = 0U;
            range_one.type = range_type;
            range_one.bits = 1U;
            if (core_const_eval_integer_with_locals(
                    context, constant_id, &constant_value) &&
                minic_target_info_integer_common_for_program(
                    context->target,
                    context->body->program,
                    range_type,
                    constant_type,
                    &common_type) &&
                minic_const_value_convert_integer(
                    context->body->program,
                    context->target,
                    &constant_value,
                    common_type,
                    &constant_common) &&
                minic_const_value_convert_integer(
                    context->body->program,
                    context->target,
                    &range_zero,
                    common_type,
                    &zero_common) &&
                minic_const_value_convert_integer(
                    context->body->program,
                    context->target,
                    &range_one,
                    common_type,
                    &one_common) &&
                core_boolean_compare_values(
                    context,
                    expression->value.binary.operator_kind,
                    common_type,
                    left_range ? &zero_common : &constant_common,
                    left_range ? &constant_common : &zero_common,
                    &predicate_zero) &&
                core_boolean_compare_values(
                    context,
                    expression->value.binary.operator_kind,
                    common_type,
                    left_range ? &one_common : &constant_common,
                    left_range ? &constant_common : &one_common,
                    &predicate_one) &&
                predicate_zero == predicate_one) {
                value->type = expression->type;
                value->bits = predicate_zero ? 1U : 0U;
                return true;
            }
        }
    }
'''
text = text[:pos] + range_comparison + text[pos:]
p.write_text(text)

core_selector = r'''

static bool core_boolean_range_specialization_same_base_facts(
    const MinicFunction *candidate,
    const MinicFunction *current,
    size_t parameter_count) {
    size_t i;

    if (candidate == NULL || !candidate->is_integer_specialization) {
        return false;
    }
    for (i = 0U; i < parameter_count; ++i) {
        bool current_integer = current != NULL && current->is_integer_specialization
                                   ? current->specialization_integer_known[i]
                                   : false;
        bool current_symbolic = current != NULL && current->is_integer_specialization
                                    ? current->specialization_symbolic_known[i]
                                    : false;
        if (candidate->specialization_integer_known[i] != current_integer ||
            (current_integer &&
             candidate->specialization_integer_bits[i] !=
                 current->specialization_integer_bits[i]) ||
            candidate->specialization_symbolic_known[i] != current_symbolic ||
            (current_symbolic &&
             candidate->specialization_symbolic_expression[i] !=
                 current->specialization_symbolic_expression[i])) {
            return false;
        }
    }
    return true;
}

static const MinicFunction *core_select_boolean_range_specialization(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    const MinicFunction *current_callee) {
    const MinicC0Program *program;
    MinicFunctionId source_id;
    const MinicFunction *source;
    const MinicFunction *best;
    bool actual_range[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t best_count = 0U;
    size_t i;
    size_t candidate_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression == NULL || expression->kind != MINIC_EXPRESSION_CALL ||
        current_callee == NULL) {
        return current_callee;
    }
    program = context->body->program;
    source_id = current_callee->is_integer_specialization
                    ? current_callee->specialization_source
                    : (MinicFunctionId)(current_callee - program->functions);
    if (source_id >= program->function_count) {
        return current_callee;
    }
    source = &program->functions[source_id];
    if (source->parameter_count != expression->value.call.argument_count ||
        source->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
        return current_callee;
    }
    (void)memset(actual_range, 0, sizeof(actual_range));
    for (i = 0U; i < source->parameter_count; ++i) {
        actual_range[i] = minic_type_is_integer(source->parameter_types[i]) &&
            core_boolean_range_expression(
                context, expression->value.call.arguments[i], 0U);
    }
    best = current_callee;
    if (current_callee->is_integer_specialization) {
        for (i = 0U; i < source->parameter_count; ++i) {
            if (current_callee->specialization_boolean_range_known[i]) {
                best_count += 1U;
            }
        }
    }
    for (candidate_index = 0U; candidate_index < program->function_count;
         ++candidate_index) {
        const MinicFunction *candidate = &program->functions[candidate_index];
        size_t range_count = 0U;
        bool compatible = true;

        if (!candidate->is_integer_specialization ||
            candidate->specialization_source != source_id ||
            candidate->parameter_count != source->parameter_count ||
            !core_boolean_range_specialization_same_base_facts(
                candidate, current_callee, source->parameter_count)) {
            continue;
        }
        for (i = 0U; i < source->parameter_count; ++i) {
            if (!candidate->specialization_boolean_range_known[i]) {
                continue;
            }
            range_count += 1U;
            if (!actual_range[i]) {
                compatible = false;
                break;
            }
        }
        if (compatible && range_count > best_count) {
            best = candidate;
            best_count = range_count;
        }
    }
    if (best != current_callee && getenv("MINIC_INLINE_SPEC_BOOLEAN_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "INLINE_SPEC_BOOLEAN_SELECT caller=%s from=%s to=%s ranges=%zu\n",
                      context->source_function != NULL &&
                              context->source_function->name != NULL
                          ? context->source_function->name
                          : "?",
                      current_callee->name != NULL ? current_callee->name : "?",
                      best->name != NULL ? best->name : "?",
                      best_count);
    }
    return best;
}
'''

p = Path("src/core/core_lower.c")
text = p.read_text()
selector_anchor = "static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,\n"
if text.count(selector_anchor) != 1:
    raise SystemExit(f"boolean selector anchor count={text.count(selector_anchor)}")
text = text.replace(selector_anchor, core_selector + "\n" + selector_anchor, 1)
call_anchor = (
    "    callee = core_select_transitive_symbolic_specialization(context, expression, callee);\n"
)
if text.count(call_anchor) != 1:
    raise SystemExit(f"boolean selector call anchor count={text.count(call_anchor)}")
text = text.replace(
    call_anchor,
    call_anchor +
    "    callee = core_select_boolean_range_specialization(context, expression, callee);\n",
    1,
)
p.write_text(text)

print("MINIC_INLINE_SPECIALIZATION_BOOLEAN_RANGE_V0=APPLIED")
