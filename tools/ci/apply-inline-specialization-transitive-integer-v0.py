#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:180]!r}")
    p.write_text(text.replace(old, new, 1))


compiler_helpers = r'''

static bool minic_inline_specialization_argument_value(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicType target_type,
    MinicConstValue *value,
    bool *used_caller_fact) {
    const MinicExpression *expression;
    MinicConstValue operand_value;
    MinicConstValue constant_value;

    if (program == NULL || target == NULL || caller == NULL || value == NULL ||
        used_caller_fact == NULL || !caller->is_integer_specialization ||
        caller->specialization_source >= program->function_count ||
        !minic_type_is_integer(target_type)) {
        return false;
    }
    *used_caller_fact = false;
    if (minic_const_eval_integer(program, target, expression_id, &constant_value) &&
        minic_const_value_convert_integer(
            program, target, &constant_value, target_type, value)) {
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        bool operand_used_caller_fact = false;
        if (!minic_inline_specialization_argument_value(
                program,
                target,
                caller,
                expression->value.unary.operand,
                expression->type,
                &operand_value,
                &operand_used_caller_fact) ||
            !minic_const_value_convert_integer(
                program, target, &operand_value, target_type, value)) {
            return false;
        }
        *used_caller_fact = operand_used_caller_fact;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicFunction *source;
        MinicLocalId local_id;
        size_t parameter_index;

        source = &program->functions[caller->specialization_source];
        local_id = expression->value.local_id;
        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_integer_known[parameter_index] ||
            !minic_type_is_integer(source->parameter_types[parameter_index])) {
            return false;
        }
        constant_value.type = source->parameter_types[parameter_index];
        constant_value.bits = caller->specialization_integer_bits[parameter_index];
        if (!minic_const_value_convert_integer(
                program, target, &constant_value, target_type, value)) {
            return false;
        }
        *used_caller_fact = true;
        return true;
    }
    return false;
}
'''

replace_once(
    "src/compiler/compiler.c",
    "static bool minic_specialize_inline_integer_calls(MinicC0Program *program,\n",
    compiler_helpers + "\nstatic bool minic_specialize_inline_integer_calls(MinicC0Program *program,\n",
)

refinement = r'''
    {
        size_t caller_index;
        size_t refinement_count = 0U;

        for (caller_index = original_function_count;
             caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            size_t nested_expression_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= original_function_count) {
                continue;
            }
            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;
                 ++nested_expression_index) {
                const MinicExpression *nested = &program->expressions[nested_expression_index];
                MinicFunctionId current_callee_id;
                MinicFunctionId nested_source_id;
                MinicFunctionId refined_id = MINIC_FUNCTION_INVALID;
                MinicFunction current_callee;
                MinicFunction nested_source;
                bool nested_known[MINIC_MAX_FUNCTION_PARAMETERS];
                uint64_t nested_bits[MINIC_MAX_FUNCTION_PARAMETERS];
                bool used_caller_fact = false;
                bool adds_fact = false;
                size_t parameter_index;
                size_t nested_candidate_index;

                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                current_callee_id = nested->value.call.function_id;
                if (current_callee_id >= program->function_count) {
                    continue;
                }
                current_callee = program->functions[current_callee_id];
                if (!current_callee.is_integer_specialization ||
                    current_callee.specialization_source >= original_function_count) {
                    continue;
                }
                nested_source_id = current_callee.specialization_source;
                nested_source = program->functions[nested_source_id];
                if (!nested_source.is_defined || !nested_source.is_internal ||
                    !nested_source.is_inline || nested_source.is_variadic ||
                    nested_source.alias_target != MINIC_FUNCTION_INVALID ||
                    nested_source.parameter_count == 0U ||
                    nested_source.parameter_count != nested->value.call.argument_count) {
                    continue;
                }
                (void)memset(nested_known, 0, sizeof(nested_known));
                (void)memset(nested_bits, 0, sizeof(nested_bits));
                for (parameter_index = 0U;
                     parameter_index < nested_source.parameter_count;
                     ++parameter_index) {
                    MinicConstValue argument_value;
                    bool argument_used_caller_fact = false;

                    if (!minic_type_is_integer(nested_source.parameter_types[parameter_index]) ||
                        !minic_inline_specialization_argument_value(
                            program,
                            target,
                            &caller,
                            nested->value.call.arguments[parameter_index],
                            nested_source.parameter_types[parameter_index],
                            &argument_value,
                            &argument_used_caller_fact)) {
                        continue;
                    }
                    nested_known[parameter_index] = true;
                    nested_bits[parameter_index] = argument_value.bits;
                    used_caller_fact = used_caller_fact || argument_used_caller_fact;
                    if (!current_callee.specialization_integer_known[parameter_index] ||
                        current_callee.specialization_integer_bits[parameter_index] !=
                            argument_value.bits) {
                        adds_fact = true;
                    }
                }
                if (!used_caller_fact || !adds_fact) {
                    continue;
                }
                for (nested_candidate_index = original_function_count;
                     nested_candidate_index < program->function_count;
                     ++nested_candidate_index) {
                    if (minic_inline_integer_specialization_matches(
                            &program->functions[nested_candidate_index],
                            nested_source_id,
                            nested_known,
                            nested_bits,
                            nested_source.parameter_count)) {
                        refined_id = nested_candidate_index;
                        break;
                    }
                }
                if (refined_id == MINIC_FUNCTION_INVALID) {
                    if (specialization_count >= 1024U ||
                        !minic_add_inline_integer_specialization(
                            program,
                            nested_source_id,
                            nested_known,
                            nested_bits,
                            specialization_count,
                            &refined_id)) {
                        return false;
                    }
                    specialization_count += 1U;
                    refinement_count += 1U;
                }
            }
        }
        if (refinement_count != 0U) {
            (void)fprintf(stderr,
                          "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_V0 refined=%zu clones=%zu functions=%zu\n",
                          refinement_count,
                          specialization_count,
                          program->function_count);
        }
    }
'''

replace_once(
    "src/compiler/compiler.c",
    "        program->expressions[expression_index].value.call.function_id = specialized_id;\n"
    "    }\n"
    "    if (specialization_count != 0U) {\n",
    "        program->expressions[expression_index].value.call.function_id = specialized_id;\n"
    "    }\n" + refinement +
    "    if (specialization_count != 0U) {\n",
)

core_selector = r'''

static const MinicFunction *core_select_transitive_integer_specialization(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    const MinicFunction *current_callee) {
    const MinicC0Program *program;
    MinicFunctionId source_id;
    const MinicFunction *source;
    const MinicFunction *best;
    size_t best_known_count;
    bool known[MINIC_MAX_FUNCTION_PARAMETERS];
    uint64_t bits[MINIC_MAX_FUNCTION_PARAMETERS];
    size_t parameter_index;
    size_t candidate_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
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
    if (source->parameter_count != expression->value.call.argument_count ||
        source->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
        return current_callee;
    }
    (void)memset(known, 0, sizeof(known));
    (void)memset(bits, 0, sizeof(bits));
    for (parameter_index = 0U; parameter_index < source->parameter_count; ++parameter_index) {
        MinicConstValue argument_value;
        MinicConstValue converted_value;
        if (!minic_type_is_integer(source->parameter_types[parameter_index]) ||
            !core_const_eval_integer_with_locals(
                context,
                expression->value.call.arguments[parameter_index],
                &argument_value) ||
            !minic_const_value_convert_integer(
                program,
                context->target,
                &argument_value,
                source->parameter_types[parameter_index],
                &converted_value)) {
            continue;
        }
        known[parameter_index] = true;
        bits[parameter_index] = converted_value.bits;
    }
    best = current_callee;
    best_known_count = 0U;
    for (parameter_index = 0U; parameter_index < source->parameter_count; ++parameter_index) {
        if (current_callee->specialization_integer_known[parameter_index]) {
            best_known_count += 1U;
        }
    }
    for (candidate_index = 0U; candidate_index < program->function_count; ++candidate_index) {
        const MinicFunction *candidate = &program->functions[candidate_index];
        size_t candidate_known_count = 0U;
        bool compatible = true;

        if (!candidate->is_integer_specialization ||
            candidate->specialization_source != source_id ||
            candidate->parameter_count != source->parameter_count) {
            continue;
        }
        for (parameter_index = 0U; parameter_index < source->parameter_count; ++parameter_index) {
            if (!candidate->specialization_integer_known[parameter_index]) {
                continue;
            }
            candidate_known_count += 1U;
            if (!known[parameter_index] ||
                candidate->specialization_integer_bits[parameter_index] != bits[parameter_index]) {
                compatible = false;
                break;
            }
        }
        if (compatible && candidate_known_count > best_known_count) {
            best = candidate;
            best_known_count = candidate_known_count;
        }
    }
    if (best != current_callee && getenv("MINIC_INLINE_SPEC_TRANSITIVE_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "INLINE_SPEC_TRANSITIVE_SELECT caller=%s from=%s to=%s known=%zu\n",
                      context->source_function != NULL && context->source_function->name != NULL
                          ? context->source_function->name
                          : "?",
                      current_callee->name != NULL ? current_callee->name : "?",
                      best->name != NULL ? best->name : "?",
                      best_known_count);
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
    "    callee = minic_c0_program_function(context->body->program, expression->value.call.function_id);\n"
    "    if (callee == NULL || callee->name == NULL || callee->name_length == 0U) {\n",
    "    callee = minic_c0_program_function(context->body->program, expression->value.call.function_id);\n"
    "    callee = core_select_transitive_integer_specialization(context, expression, callee);\n"
    "    if (callee == NULL || callee->name == NULL || callee->name_length == 0U) {\n",
)

print("MINIC_INLINE_SPECIALIZATION_TRANSITIVE_INTEGER_V0=APPLIED")
