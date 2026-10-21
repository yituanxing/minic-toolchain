#include "compiler/inline_specialize.h"
#include "frontend/const_eval.h"
#include "frontend/function_body.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
/* AST semantic specialization and reference finalization are intentionally
 * kept separate from Core CFG reachability and machine ISA legalization.
 * This is the unchanged validated optimization body, not a new pass order. */

#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 2048U

static bool minic_inline_integer_specialization_matches(
    const MinicFunction *candidate,
    MinicFunctionId source_id,
    const bool *known,
    const uint64_t *bits,
    size_t parameter_count) {
    size_t i;
    if (candidate == NULL || !candidate->is_integer_specialization ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count) {
        return false;
    }
    for (i = 0U; i < parameter_count; ++i) {
        if (candidate->specialization_integer_known[i] != known[i] ||
            (known[i] && candidate->specialization_integer_bits[i] != bits[i])) {
            return false;
        }
    }
    return true;
}

static bool minic_add_inline_integer_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const bool *known,
    const uint64_t *bits,
    size_t ordinal,
    MinicFunctionId *specialized_id) {
    MinicFunction source;
    MinicFunction *specialized;
    char name[96];
    int name_length;
    size_t i;

    if (program == NULL || source_id >= program->function_count ||
        specialized_id == NULL) {
        return false;
    }
    source = program->functions[source_id];
    if (!source.is_defined || !source.is_internal || !source.is_inline ||
        source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID) {
        return false;
    }
    name_length = snprintf(name, sizeof(name), "__minic_inline_spec_%zu_%zu",
                           (size_t)source_id, ordinal);
    if (name_length <= 0 || (size_t)name_length >= sizeof(name) ||
        !minic_c0_program_add_function(program, name, (size_t)name_length,
                                       source.local_begin, source.local_count,
                                       source.body_block, specialized_id) ||
        !minic_c0_program_set_function_signature(program, *specialized_id,
                                                 source.return_type,
                                                 source.parameter_types,
                                                 source.parameter_count) ||
        !minic_c0_program_set_function_internal(program, *specialized_id, true) ||
        !minic_c0_program_set_function_inline(program, *specialized_id, true) ||
        (source.is_noreturn &&
         !minic_c0_program_set_function_noreturn(program, *specialized_id, true)) ||
        (source.section_name != NULL &&
         !minic_c0_program_set_function_section(program, *specialized_id,
                                                source.section_name,
                                                source.section_name_length))) {
        return false;
    }
    specialized = &program->functions[*specialized_id];
    specialized->visibility = source.visibility;
    specialized->specialization_source = source_id;
    specialized->is_integer_specialization = true;
    for (i = 0U; i < source.parameter_count; ++i) {
        specialized->specialization_integer_known[i] = known[i];
        specialized->specialization_integer_bits[i] = bits[i];
    }
    return true;
}



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

bool minic_specialize_inline_integer_calls(MinicC0Program *program,
                                                  const MinicTargetInfo *target) {
    size_t expression_index;
    size_t original_function_count;
    size_t specialization_count = 0U;

    if (program == NULL || target == NULL) {
        return false;
    }
    original_function_count = program->function_count;
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        MinicExpression *expression = &program->expressions[expression_index];
        MinicFunctionId source_id;
        MinicFunctionId specialized_id = MINIC_FUNCTION_INVALID;
        MinicFunction source;
        bool known[MINIC_MAX_FUNCTION_PARAMETERS];
        uint64_t bits[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t i;
        size_t candidate_index;
        bool has_integer_constant = false;

        if (expression->kind != MINIC_EXPRESSION_CALL ||
            expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            continue;
        }
        source_id = expression->value.call.function_id;
        if (source_id >= original_function_count) {
            continue;
        }
        source = program->functions[source_id];
        if (!source.is_defined || !source.is_internal || !source.is_inline ||
            !source.is_referenced || source.is_variadic ||
            source.alias_target != MINIC_FUNCTION_INVALID || source.parameter_count == 0U ||
            source.parameter_count != expression->value.call.argument_count) {
            continue;
        }
        (void)memset(known, 0, sizeof(known));
        (void)memset(bits, 0, sizeof(bits));
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicConstValue argument_value;
            MinicConstValue converted_value;
            if (!minic_type_is_integer(source.parameter_types[i]) ||
                !minic_const_eval_integer(program, target,
                                          minic_c0_call_argument(expression, i),
                                          &argument_value) ||
                !minic_const_value_convert_integer(program, target, &argument_value,
                                                   source.parameter_types[i],
                                                   &converted_value)) {
                continue;
            }
            known[i] = true;
            bits[i] = converted_value.bits;
            has_integer_constant = true;
        }
        if (!has_integer_constant) {
            continue;
        }
        for (candidate_index = original_function_count;
             candidate_index < program->function_count; ++candidate_index) {
            if (minic_inline_integer_specialization_matches(
                    &program->functions[candidate_index], source_id, known, bits,
                    source.parameter_count)) {
                specialized_id = candidate_index;
                break;
            }
        }
        if (specialized_id == MINIC_FUNCTION_INVALID) {
            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT) {
                (void)fprintf(stderr,
                              "MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT_HIT count=%zu limit=%u functions=%zu source=%zu\n",
                              specialization_count,
                              (unsigned int)MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT,
                              program->function_count,
                              (size_t)source_id);
                return false;
            }
            if (!minic_add_inline_integer_specialization(program, source_id, known, bits,
                                                         specialization_count,
                                                         &specialized_id)) {
                (void)fprintf(stderr,
                              "MINIC_INLINE_INTEGER_SPECIALIZATION_ADD_FAILED count=%zu functions=%zu source=%zu\n",
                              specialization_count,
                              program->function_count,
                              (size_t)source_id);
                return false;
            }
            specialization_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = specialized_id;
    }

    {
        size_t caller_index;
        size_t refinement_count = 0U;
        size_t *calls = NULL, call_count = 0U, pos;
        const size_t expression_limit = program->expression_count;
        /* Reuse the canonical source-FunctionBody ownership index.  Only
         * uniquely-owned calls from another source may be skipped.  Shared,
         * ambiguous, and unowned expressions retain the original path.
         * Allocation/validation failure deliberately falls back to all calls. */
        MinicFunctionId *integer_expression_owners = NULL;
        bool integer_owner_index_ready = false;
        size_t integer_owner_skipped = 0U;
        size_t integer_call_candidates = 0U;
        if (expression_limit != 0U &&
            expression_limit <= SIZE_MAX / sizeof(*integer_expression_owners)) {
            integer_expression_owners =
                (MinicFunctionId *)malloc(expression_limit *
                                          sizeof(*integer_expression_owners));
            if (integer_expression_owners != NULL) {
                integer_owner_index_ready =
                    minic_perf_program_build_expression_source_owners(
                        program, original_function_count,
                        integer_expression_owners, expression_limit);
                if (!integer_owner_index_ready) {
                    free(integer_expression_owners);
                    integer_expression_owners = NULL;
                }
            }
        }
        if (expression_limit > 0U &&
            expression_limit <= SIZE_MAX / sizeof(*calls)) {
            calls = malloc(expression_limit * sizeof(*calls));
            if (calls != NULL) {
                for (pos = 0U; pos < expression_limit; ++pos)
                    if (program->expressions[pos].kind == MINIC_EXPRESSION_CALL)
                        calls[call_count++] = pos;
            }
        }

        for (caller_index = original_function_count;
             caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            size_t nested_scan_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= original_function_count) {
                continue;
            }
            for (nested_scan_index = 0U;
                 nested_scan_index < ((calls != NULL &&
                    program->expression_count == expression_limit)
                        ? call_count : program->expression_count);
                 ++nested_scan_index) {
                size_t nested_expression_index =
                    (calls != NULL && program->expression_count == expression_limit)
                    ? calls[nested_scan_index] : nested_scan_index;
                const MinicExpression *nested = &program->expressions[nested_expression_index];
                MinicFunctionId current_callee_id;

                integer_call_candidates += 1U;
                if (integer_owner_index_ready &&
                    nested_expression_index < expression_limit) {
                    MinicFunctionId expression_owner =
                        integer_expression_owners[nested_expression_index];
                    if (expression_owner != MINIC_FUNCTION_INVALID &&
                        expression_owner < original_function_count &&
                        expression_owner != caller.specialization_source) {
                        integer_owner_skipped += 1U;
                        continue;
                    }
                }
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
                            minic_c0_call_argument(nested, parameter_index),
                            nested_source.parameter_types[parameter_index],
                            &argument_value,
                            &argument_used_caller_fact)) {
                        continue;
                    }
                    nested_known[parameter_index] = true;
                    nested_bits[parameter_index] = argument_value.bits;
                    used_caller_fact = used_caller_fact || argument_used_caller_fact;
                    if (!current_callee.is_integer_specialization ||
                        !current_callee.specialization_integer_known[parameter_index] ||
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
                    if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT) {
                        (void)fprintf(stderr,
                                      "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_LIMIT_HIT count=%zu limit=%u functions=%zu source=%zu\n",
                                      specialization_count,
                                      (unsigned int)MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT,
                                      program->function_count,
                                      (size_t)nested_source_id);
                        free(integer_expression_owners);
                        free(calls);
                        return false;
                    }
                    if (!minic_add_inline_integer_specialization(
                            program,
                            nested_source_id,
                            nested_known,
                            nested_bits,
                            specialization_count,
                            &refined_id)) {
                        (void)fprintf(stderr,
                                      "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_ADD_FAILED count=%zu functions=%zu source=%zu\n",
                                      specialization_count,
                                      program->function_count,
                                      (size_t)nested_source_id);
                        free(integer_expression_owners);
                        free(calls);
                        return false;
                    }
                    specialization_count += 1U;
                    refinement_count += 1U;
                }
            }
        }
        free(calls);
        free(integer_expression_owners);
        if (getenv("MINIC_INTEGER_OWNER_PERF") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_INTEGER_OWNER_FILTER ready=%u "
                          "candidates=%zu skipped_other_owner=%zu "
                          "fallback=%u\n",
                          (unsigned int)integer_owner_index_ready,
                          integer_call_candidates,
                          integer_owner_skipped,
                          (unsigned int)!integer_owner_index_ready);
        }
        if (refinement_count != 0U) {
            (void)fprintf(stderr,
                          "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_V0 refined=%zu clones=%zu functions=%zu\n",
                          refinement_count,
                          specialization_count,
                          program->function_count);
        }
    }
    if (specialization_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_INTEGER_SPECIALIZATION_V0 clones=%zu functions=%zu\n",
                      specialization_count, program->function_count);
    }
    return true;
}



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
    /* A nested constant array subscript is still a static lvalue rooted in the
     * same global object.  This matters for addresses such as
     * &global_matrix[CONST_A][CONST_B]: ADDRESS_OF sees the outer SUBSCRIPT,
     * then recursion must be able to walk the inner array-valued SUBSCRIPT. */
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT &&
        minic_type_is_array(expression->type)) {
        MinicConstValue index_value;
        return minic_inline_symbolic_static_expression_depth(
                   program, target, expression->value.subscript.base, depth + 1U) &&
               minic_const_eval_integer(
                   program, target, expression->value.subscript.index, &index_value);
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

static bool minic_inline_close_symbolic_expression_depth(
    MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicExpressionId *closed_id,
    bool *used_caller_integer_fact,
    unsigned int depth) {
    const MinicExpression *expression;
    const MinicFunction *caller_source;
    MinicExpression closed;
    MinicExpressionId left_id;
    MinicExpressionId right_id;
    bool left_used = false;
    bool right_used = false;

    if (program == NULL || target == NULL || caller == NULL || closed_id == NULL ||
        used_caller_integer_fact == NULL || depth > 32U ||
        !caller->is_integer_specialization ||
        caller->specialization_source >= program->function_count) {
        return false;
    }
    *used_caller_integer_fact = false;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    caller_source = &program->functions[caller->specialization_source];

    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;
        size_t parameter_index;
        uint64_t bits;

        if (local_id < caller_source->local_begin) {
            return false;
        }
        parameter_index = local_id - caller_source->local_begin;
        if (parameter_index >= caller_source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_integer_known[parameter_index] ||
            !minic_type_is_integer(caller_source->parameter_types[parameter_index])) {
            return false;
        }
        (void)memset(&closed, 0, sizeof(closed));
        closed.kind = MINIC_EXPRESSION_INTEGER;
        closed.span = expression->span;
        closed.type = caller_source->parameter_types[parameter_index];
        closed.value_category = MINIC_VALUE_RVALUE;
        bits = caller->specialization_integer_bits[parameter_index];
        (void)memcpy(&closed.value.integer_value, &bits, sizeof(bits));
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        if (operand != NULL && operand->kind == MINIC_EXPRESSION_LOCAL) {
            if (!minic_inline_close_symbolic_expression_depth(
                    program,
                    target,
                    caller,
                    expression->value.unary.operand,
                    &left_id,
                    &left_used,
                    depth + 1U) ||
                !left_used) {
                return false;
            }
            *closed_id = left_id;
            *used_caller_integer_fact = true;
            return true;
        }
    }

    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ ||
        expression->kind == MINIC_EXPRESSION_ADDRESS_OF ||
        expression->kind == MINIC_EXPRESSION_DEREFERENCE) {
        if (!minic_inline_close_symbolic_expression_depth(
                program,
                target,
                caller,
                expression->value.unary.operand,
                &left_id,
                &left_used,
                depth + 1U)) {
            return false;
        }
        if (!left_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.unary.operand = left_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        bool base_static = minic_inline_symbolic_static_expression(
            program, target, expression->value.subscript.base);
        if (base_static) {
            left_id = expression->value.subscript.base;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.subscript.base,
                       &left_id,
                       &left_used,
                       depth + 1U)) {
            return false;
        }
        if (minic_const_eval_integer(
                program, target, expression->value.subscript.index, &(MinicConstValue){0})) {
            right_id = expression->value.subscript.index;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.subscript.index,
                       &right_id,
                       &right_used,
                       depth + 1U)) {
            return false;
        }
        if (!left_used && !right_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.subscript.base = left_id;
        closed.value.subscript.index = right_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = left_used || right_used;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        if (minic_inline_symbolic_static_expression(
                program, target, expression->value.member.base)) {
            *closed_id = expression_id;
            return true;
        }
        if (!minic_inline_close_symbolic_expression_depth(
                program,
                target,
                caller,
                expression->value.member.base,
                &left_id,
                &left_used,
                depth + 1U)) {
            return false;
        }
        if (!left_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.member.base = left_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        if (minic_inline_symbolic_static_expression(
                program, target, expression->value.binary.left)) {
            left_id = expression->value.binary.left;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.binary.left,
                       &left_id,
                       &left_used,
                       depth + 1U)) {
            return false;
        }
        if (minic_const_eval_integer(
                program, target, expression->value.binary.right, &(MinicConstValue){0})) {
            right_id = expression->value.binary.right;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       expression->value.binary.right,
                       &right_id,
                       &right_used,
                       depth + 1U)) {
            return false;
        }
        if (!left_used && !right_used) {
            *closed_id = expression_id;
            return true;
        }
        closed = *expression;
        closed.value.binary.left = left_id;
        closed.value.binary.right = right_id;
        if (!minic_c0_program_add_expression(program, &closed, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = left_used || right_used;
        return true;
    }

    return false;
}

static bool minic_inline_symbolic_argument_expression(
    MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicExpressionId *symbolic_expression,
    bool *used_caller_fact,
    unsigned int depth) {
    const MinicExpression *expression;
    MinicExpressionId closed_id;
    bool used_integer_fact = false;

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

    if (minic_inline_close_symbolic_expression_depth(
            program,
            target,
            caller,
            expression_id,
            &closed_id,
            &used_integer_fact,
            depth + 1U) &&
        used_integer_fact &&
        minic_inline_symbolic_static_expression(program, target, closed_id)) {
        *symbolic_expression = closed_id;
        *used_caller_fact = true;
        return true;
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

static bool minic_inline_symbolic_integer_inputs_are_closed(
    const MinicFunction *source,
    const bool *integer_known) {
    size_t i;

    if (source == NULL || integer_known == NULL) {
        return false;
    }
    for (i = 0U; i < source->parameter_count; ++i) {
        if (minic_type_is_integer(source->parameter_types[i]) && !integer_known[i]) {
            return false;
        }
    }
    return true;
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

bool minic_specialize_inline_symbolic_calls(
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
        /* Do not manufacture a half-specialized variant for a helper whose
         * integer parameters are still runtime values.  GNU immediate-only
         * asm inside helpers such as arch_static_branch requires both the
         * symbolic key and the branch integer to be closed before Core lower. */
        if (!minic_inline_symbolic_integer_inputs_are_closed(&source, integer_known)) {
            continue;
        }
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicExpressionId argument_id = minic_c0_call_argument(call, i);
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
        size_t symbolic_owned_expression_count = program->expression_count;
        MinicFunctionId *symbolic_expression_owners =
            symbolic_owned_expression_count == 0U
                ? NULL
                : (MinicFunctionId *)malloc(
                      symbolic_owned_expression_count *
                      sizeof(*symbolic_expression_owners));

        /* Optimization only: never fail an otherwise valid compiler input
         * just because an optional ownership index cannot be allocated or
         * constructed. Revert to the original unfiltered transitive walk. */
        bool symbolic_owner_index_ready =
            (symbolic_owned_expression_count == 0U ||
             symbolic_expression_owners != NULL) &&
            minic_perf_program_build_expression_source_owners(
                program,
                original_function_count,
                symbolic_expression_owners,
                symbolic_owned_expression_count);
        if (!symbolic_owner_index_ready) {
            free(symbolic_expression_owners);
            symbolic_expression_owners = NULL;
        }
        /* Indexed CALLs are grouped by their *canonical* source owner.
         * Shared, ambiguous and orphan CALLs are retained in the common bucket.
         * Retain ascending ExpressionId order using a two-way merge.
         * All storage is optional; any OOM falls back to the original scan.
         * Late appends are scanned separately using the live expression_count. */
        size_t *symbolic_source_bucket_offsets = NULL;
        size_t *symbolic_source_bucket_calls = NULL;
        size_t *symbolic_source_bucket_writes = NULL;
        bool symbolic_source_buckets_ready = false;
        if (symbolic_owner_index_ready &&
            symbolic_owned_expression_count != 0U &&
            original_function_count <= SIZE_MAX / sizeof(size_t) - 2U &&
            symbolic_owned_expression_count <= SIZE_MAX / sizeof(size_t)) {
            size_t n = original_function_count;
            size_t idx;
            symbolic_source_bucket_offsets = (size_t *)calloc(
                n + 2U, sizeof(*symbolic_source_bucket_offsets));
            symbolic_source_bucket_calls = (size_t *)malloc(
                symbolic_owned_expression_count * sizeof(*symbolic_source_bucket_calls));
            symbolic_source_bucket_writes = (size_t *)malloc(
                (n + 1U) * sizeof(*symbolic_source_bucket_writes));
            if (symbolic_source_bucket_offsets != NULL &&
                symbolic_source_bucket_calls != NULL &&
                symbolic_source_bucket_writes != NULL) {
                for (idx = 0U; idx < symbolic_owned_expression_count; ++idx) {
                    if (program->expressions[idx].kind == MINIC_EXPRESSION_CALL) {
                        MinicFunctionId owner = symbolic_expression_owners[idx];
                        size_t bucket = (owner != MINIC_FUNCTION_INVALID && owner < n)
                                            ? (size_t)owner : n;
                        ++symbolic_source_bucket_offsets[bucket + 1U];
                    }
                }
                for (idx = 0U; idx < n + 1U; ++idx) {
                    symbolic_source_bucket_offsets[idx + 1U] +=
                        symbolic_source_bucket_offsets[idx];
                }
                (void)memcpy(symbolic_source_bucket_writes,
                             symbolic_source_bucket_offsets,
                             (n + 1U) * sizeof(*symbolic_source_bucket_writes));
                for (idx = 0U; idx < symbolic_owned_expression_count; ++idx) {
                    if (program->expressions[idx].kind == MINIC_EXPRESSION_CALL) {
                        MinicFunctionId owner = symbolic_expression_owners[idx];
                        size_t bucket = (owner != MINIC_FUNCTION_INVALID && owner < n)
                                            ? (size_t)owner : n;
                        symbolic_source_bucket_calls[
                            symbolic_source_bucket_writes[bucket]++] = idx;
                    }
                }
                symbolic_source_buckets_ready = true;
            }
        }
        free(symbolic_source_bucket_writes);
        if (!symbolic_source_buckets_ready) {
            free(symbolic_source_bucket_offsets);
            free(symbolic_source_bucket_calls);
            symbolic_source_bucket_offsets = NULL;
            symbolic_source_bucket_calls = NULL;
        }
        (void)original_function_count;
        for (caller_index = 0U;
             caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            size_t symbolic_full_cursor = 0U;
            size_t symbolic_own_cursor = 0U;
            size_t symbolic_own_end = 0U;
            size_t symbolic_common_cursor = 0U;
            size_t symbolic_common_end = 0U;
            size_t symbolic_late_cursor = symbolic_owned_expression_count;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            if (symbolic_source_buckets_ready) {
                size_t n = original_function_count;
                if (caller.specialization_source < n) {
                    symbolic_own_cursor =
                        symbolic_source_bucket_offsets[caller.specialization_source];
                    symbolic_own_end =
                        symbolic_source_bucket_offsets[caller.specialization_source + 1U];
                }
                symbolic_common_cursor = symbolic_source_bucket_offsets[n];
                symbolic_common_end = symbolic_source_bucket_offsets[n + 1U];
            }
            for (;;) {
                size_t nested_expression_index;
                if (!symbolic_source_buckets_ready) {
                    if (symbolic_full_cursor >= program->expression_count) {
                        break;
                    }
                    nested_expression_index = symbolic_full_cursor++;
                } else if (symbolic_own_cursor < symbolic_own_end ||
                           symbolic_common_cursor < symbolic_common_end) {
                    if (symbolic_common_cursor == symbolic_common_end ||
                        (symbolic_own_cursor < symbolic_own_end &&
                         symbolic_source_bucket_calls[symbolic_own_cursor] <
                             symbolic_source_bucket_calls[symbolic_common_cursor])) {
                        nested_expression_index =
                            symbolic_source_bucket_calls[symbolic_own_cursor++];
                    } else {
                        nested_expression_index =
                            symbolic_source_bucket_calls[symbolic_common_cursor++];
                    }
                } else if (symbolic_late_cursor < program->expression_count) {
                    /* The old loop observes append-only expressions, including
                     * ones created while processing this same caller. */
                    nested_expression_index = symbolic_late_cursor++;
                } else {
                    break;
                }
                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];
                MinicFunctionId current_id;

                if (symbolic_owner_index_ready &&
                    nested_expression_index < symbolic_owned_expression_count) {
                    MinicFunctionId nested_owner =
                        symbolic_expression_owners[nested_expression_index];
                    if (nested_owner != MINIC_FUNCTION_INVALID &&
                        nested_owner < original_function_count &&
                        nested_owner != caller.specialization_source) {
                        continue;
                    }
                }
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
                if (!minic_inline_symbolic_integer_inputs_are_closed(&source, integer_known)) {
                    continue;
                }
                for (i = 0U; i < source.parameter_count; ++i) {
                    MinicExpressionId resolved_expression;
                    bool argument_used_caller_fact = false;
                    if (!minic_type_is_pointer(source.parameter_types[i]) ||
                        !minic_inline_symbolic_argument_expression(
                            program,
                            target,
                            &caller,
                            minic_c0_call_argument(nested, i),
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
                        free(symbolic_source_bucket_offsets);
                        free(symbolic_source_bucket_calls);
                        free(symbolic_expression_owners);
                        return false;
                    }
                    transitive_clone_count += 1U;
                }
                /* The transitive symbolic pass used to create the right clone
                   but leave the source call pointing at the base callee.  That
                   made the specialization dead and preserved deferred asm
                   immediates.  Rewrite the actual nested call by stable index. */
                program->expressions[nested_expression_index].value.call.function_id = variant_id;
            }
        }
        free(symbolic_source_bucket_offsets);
        free(symbolic_source_bucket_calls);
        free(symbolic_expression_owners);
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




static bool minic_inline_boolean_domain_value(
    const MinicC0Program *program,
    MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;

    if (program == NULL || depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (minic_type_is_bool_integer(expression->type)) {
        return true;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        return expression->value.integer_value == 0 ||
               expression->value.integer_value == 1;
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_LVALUE_READ:
        return minic_inline_boolean_domain_value(
            program, expression->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_UNARY:
        if (expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
            return true;
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_PLUS) {
            return minic_inline_boolean_domain_value(
                program, expression->value.unary.operand, depth + 1U);
        }
        return false;
    case MINIC_EXPRESSION_BINARY:
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_EQUAL:
        case MINIC_BINARY_NOT_EQUAL:
        case MINIC_BINARY_LESS:
        case MINIC_BINARY_LESS_EQUAL:
        case MINIC_BINARY_GREATER:
        case MINIC_BINARY_GREATER_EQUAL:
        case MINIC_BINARY_LOGICAL_AND:
        case MINIC_BINARY_LOGICAL_OR:
            return true;
        case MINIC_BINARY_COMMA:
            return minic_inline_boolean_domain_value(
                program, expression->value.binary.right, depth + 1U);
        default:
            return false;
        }
    case MINIC_EXPRESSION_CONDITIONAL:
        return minic_inline_boolean_domain_value(
                   program, expression->value.conditional.when_true, depth + 1U) &&
               minic_inline_boolean_domain_value(
                   program, expression->value.conditional.when_false, depth + 1U);
    case MINIC_EXPRESSION_ASSIGNMENT:
        return minic_inline_boolean_domain_value(
            program, expression->value.binary.right, depth + 1U);
    default:
        return false;
    }
}

static bool minic_inline_direct_local_target(
    const MinicC0Program *program,
    MinicExpressionId expression_id,
    MinicLocalId *local_id) {
    const MinicExpression *expression;

    if (program == NULL || local_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL) {
        return false;
    }
    *local_id = expression->value.local_id;
    return true;
}

static bool minic_inline_local_is_parameter(
    const MinicC0Program *program,
    MinicLocalId local_id) {
    size_t function_index;

    if (program == NULL) {
        return false;
    }
    for (function_index = 0U; function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        if (!function->is_defined || function->parameter_count == 0U) {
            continue;
        }
        if (local_id >= function->local_begin &&
            local_id - function->local_begin < function->parameter_count) {
            return true;
        }
    }
    return false;
}

/*
 * Conservatively recognize ordinary integer locals whose complete direct-write
 * set is confined to {0,1}.  This covers Linux patterns such as an unsigned
 * int flag initialized to 0 and conditionally assigned 1 before being passed
 * through always-inline helpers.  Reject address escapes, compound updates,
 * increments/decrements and asm outputs so the fact is never inferred across
 * an unknown write.
 */
static bool minic_inline_local_boolean_domain_legacy(
    const MinicC0Program *program,
    MinicLocalId local_id) {
    bool saw_assignment = false;
    size_t statement_index;
    size_t expression_index;
    size_t asm_index;

    if (program == NULL || local_id >= program->local_count ||
        !minic_type_is_integer(program->locals[local_id].type) ||
        minic_inline_local_is_parameter(program, local_id)) {
        return false;
    }

    for (statement_index = 0U; statement_index < program->statement_count;
         ++statement_index) {
        const MinicStatement *statement = &program->statements[statement_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;
        if (!minic_inline_direct_local_target(
                program, statement->target_expression, &target_local) ||
            target_local != local_id) {
            continue;
        }
        if (statement->kind != MINIC_STATEMENT_ASSIGN ||
            statement->expression == MINIC_EXPRESSION_INVALID ||
            !minic_inline_boolean_domain_value(
                program, statement->expression, 0U)) {
            return false;
        }
        saw_assignment = true;
    }

    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *expression = &program->expressions[expression_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;

        if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_UNARY &&
            (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return false;
        }
        if ((expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
             expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.binary.left, &target_local) &&
            target_local == local_id) {
            if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT ||
                !minic_inline_boolean_domain_value(
                    program, expression->value.binary.right, 0U)) {
                return false;
            }
            saw_assignment = true;
        }
    }

    for (asm_index = 0U; asm_index < program->inline_asm_count; ++asm_index) {
        const MinicInlineAsm *inline_asm = &program->inline_asms[asm_index];
        size_t output_index;
        for (output_index = 0U; output_index < inline_asm->output_count;
             ++output_index) {
            MinicLocalId output_local = MINIC_LOCAL_INVALID;
            if (minic_inline_direct_local_target(
                    program,
                    inline_asm->outputs[output_index].expression,
                    &output_local) &&
                output_local == local_id) {
                return false;
            }
        }
    }

    return saw_assignment;
}



typedef struct MinicInlineLocalBooleanDomainCache {
    const MinicC0Program *program;
    bool *values;
    size_t local_count;
    size_t statement_count;
    size_t expression_count;
    size_t inline_asm_count;
} MinicInlineLocalBooleanDomainCache;

static MinicInlineLocalBooleanDomainCache minic_inline_local_boolean_domain_cache;
static size_t minic_perf_boolean_domain_queries;
static size_t minic_perf_boolean_domain_builds;
static size_t minic_perf_boolean_domain_fallbacks;

void minic_inline_local_boolean_domain_cache_clear(void) {
    free(minic_inline_local_boolean_domain_cache.values);
    (void)memset(&minic_inline_local_boolean_domain_cache, 0,
                 sizeof(minic_inline_local_boolean_domain_cache));
    minic_perf_boolean_domain_queries = 0U;
    minic_perf_boolean_domain_builds = 0U;
    minic_perf_boolean_domain_fallbacks = 0U;
}

static bool minic_inline_build_local_boolean_domain_cache(
    const MinicC0Program *program) {
    bool *values = NULL;
    bool *parameter = NULL;
    bool *saw_assignment = NULL;
    bool *invalid = NULL;
    size_t function_index;
    size_t statement_index;
    size_t expression_index;
    size_t asm_index;
    size_t local_index;

    (void)minic_inline_local_is_parameter;
    if (program == NULL ||
        program->local_count > SIZE_MAX / sizeof(bool)) {
        return false;
    }
    values = (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                            sizeof(*values));
    parameter = (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                               sizeof(*parameter));
    saw_assignment =
        (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                       sizeof(*saw_assignment));
    invalid = (bool *)calloc(program->local_count == 0U ? 1U : program->local_count,
                             sizeof(*invalid));
    if (values == NULL || parameter == NULL || saw_assignment == NULL ||
        invalid == NULL) {
        free(values);
        free(parameter);
        free(saw_assignment);
        free(invalid);
        return false;
    }

    for (function_index = 0U; function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        size_t parameter_index;
        if (!function->is_defined || function->local_begin > program->local_count ||
            function->parameter_count > program->local_count - function->local_begin) {
            continue;
        }
        for (parameter_index = 0U; parameter_index < function->parameter_count;
             ++parameter_index) {
            parameter[function->local_begin + parameter_index] = true;
        }
    }

    for (statement_index = 0U; statement_index < program->statement_count;
         ++statement_index) {
        const MinicStatement *statement = &program->statements[statement_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;
        if (!minic_inline_direct_local_target(
                program, statement->target_expression, &target_local) ||
            target_local >= program->local_count) {
            continue;
        }
        if (statement->kind != MINIC_STATEMENT_ASSIGN ||
            statement->expression == MINIC_EXPRESSION_INVALID ||
            !minic_inline_boolean_domain_value(
                program, statement->expression, 0U)) {
            invalid[target_local] = true;
        } else {
            saw_assignment[target_local] = true;
        }
    }

    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        const MinicExpression *expression = &program->expressions[expression_index];
        MinicLocalId target_local = MINIC_LOCAL_INVALID;

        if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local < program->local_count) {
            invalid[target_local] = true;
            continue;
        }
        if (expression->kind == MINIC_EXPRESSION_UNARY &&
            (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.unary.operand, &target_local) &&
            target_local < program->local_count) {
            invalid[target_local] = true;
            continue;
        }
        if ((expression->kind == MINIC_EXPRESSION_ASSIGNMENT ||
             expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) &&
            minic_inline_direct_local_target(
                program, expression->value.binary.left, &target_local) &&
            target_local < program->local_count) {
            if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT ||
                !minic_inline_boolean_domain_value(
                    program, expression->value.binary.right, 0U)) {
                invalid[target_local] = true;
            } else {
                saw_assignment[target_local] = true;
            }
        }
    }

    for (asm_index = 0U; asm_index < program->inline_asm_count; ++asm_index) {
        const MinicInlineAsm *inline_asm = &program->inline_asms[asm_index];
        size_t output_index;
        for (output_index = 0U; output_index < inline_asm->output_count;
             ++output_index) {
            MinicLocalId output_local = MINIC_LOCAL_INVALID;
            if (minic_inline_direct_local_target(
                    program,
                    inline_asm->outputs[output_index].expression,
                    &output_local) &&
                output_local < program->local_count) {
                invalid[output_local] = true;
            }
        }
    }

    for (local_index = 0U; local_index < program->local_count; ++local_index) {
        values[local_index] =
            minic_type_is_integer(program->locals[local_index].type) &&
            !parameter[local_index] &&
            saw_assignment[local_index] &&
            !invalid[local_index];
    }

    free(parameter);
    free(saw_assignment);
    free(invalid);
    free(minic_inline_local_boolean_domain_cache.values);
    minic_inline_local_boolean_domain_cache.program = program;
    minic_inline_local_boolean_domain_cache.values = values;
    minic_inline_local_boolean_domain_cache.local_count = program->local_count;
    minic_inline_local_boolean_domain_cache.statement_count = program->statement_count;
    minic_inline_local_boolean_domain_cache.expression_count = program->expression_count;
    minic_inline_local_boolean_domain_cache.inline_asm_count = program->inline_asm_count;
    minic_perf_boolean_domain_builds += 1U;
    return true;
}


/* MINIC_BOOLEAN_DOMAIN_SCOPED_NATIVE_V1: certified on 500 real Linux inputs. */
static bool minic_inline_local_boolean_domain(
    const MinicC0Program *program,
    MinicLocalId local_id) {
    if (program == NULL || local_id >= program->local_count) {
        return false;
    }
    minic_perf_boolean_domain_queries += 1U;
    /* An appended clone may grow the underlying arenas without changing
     * the Program address. All arenas referenced by write scanning matter. */
    if (minic_inline_local_boolean_domain_cache.program != program ||
        minic_inline_local_boolean_domain_cache.local_count != program->local_count ||
        minic_inline_local_boolean_domain_cache.statement_count != program->statement_count ||
        minic_inline_local_boolean_domain_cache.expression_count != program->expression_count ||
        minic_inline_local_boolean_domain_cache.inline_asm_count != program->inline_asm_count ||
        minic_inline_local_boolean_domain_cache.values == NULL) {
        if (!minic_inline_build_local_boolean_domain_cache(program)) {
            minic_perf_boolean_domain_fallbacks += 1U;
            return minic_inline_local_boolean_domain_legacy(program, local_id);
        }
    }
    return minic_inline_local_boolean_domain_cache.values[local_id];
}

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
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        MinicLocalId local_id = expression->value.local_id;

        if (minic_inline_local_boolean_domain(program, local_id)) {
            return true;
        }
        if (caller != NULL && caller->is_integer_specialization &&
            caller->specialization_source < program->function_count) {
            const MinicFunction *source =
                &program->functions[caller->specialization_source];
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

bool minic_specialize_inline_boolean_range_calls(
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
                program, minic_c0_call_argument(call, i));
            if (argument == NULL ||
                !minic_inline_boolean_range_argument(
                    program,
                    NULL,
                    minic_c0_call_argument(call, i),
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
        /* All clones in this pass reuse canonical source FunctionBodies.
         * Build the original-expression ownership oracle and CALL-only
         * iteration list lazily: most TUs have no Boolean-range callers.
         * On OOM or invalid ownership, the original full walk is preserved.
         * Calls remain sorted by expression ID, maintaining clone order. */
        const size_t boolean_expression_limit = program->expression_count;
        const size_t boolean_function_limit = program->function_count;
        MinicFunctionId *boolean_owners = NULL;
        size_t *boolean_calls = NULL;
        size_t boolean_call_count = 0U;
        size_t boolean_index_builds = 0U;
        size_t boolean_caller_scans = 0U;
        size_t boolean_call_candidates = 0U;
        size_t boolean_skipped_other_owner = 0U;
        bool boolean_owner_ready = false;
        bool boolean_index_attempted = false;
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
            boolean_caller_scans += 1U;
            if (!boolean_index_attempted) {
                boolean_index_attempted = true;
                boolean_index_builds += 1U;
                if (boolean_expression_limit != 0U &&
                    boolean_expression_limit <= SIZE_MAX / sizeof(*boolean_calls)) {
                    size_t call_id;
                    boolean_calls = (size_t *)malloc(
                        boolean_expression_limit * sizeof(*boolean_calls));
                    if (boolean_calls != NULL) {
                        for (call_id = 0U; call_id < boolean_expression_limit;
                             ++call_id) {
                            if (program->expressions[call_id].kind ==
                                MINIC_EXPRESSION_CALL) {
                                boolean_calls[boolean_call_count++] = call_id;
                            }
                        }
                    }
                }
                if (boolean_expression_limit != 0U &&
                    boolean_expression_limit <= SIZE_MAX / sizeof(*boolean_owners)) {
                    boolean_owners = (MinicFunctionId *)malloc(
                        boolean_expression_limit * sizeof(*boolean_owners));
                    if (boolean_owners != NULL) {
                        boolean_owner_ready =
                            minic_perf_program_build_expression_source_owners(
                                program, boolean_function_limit,
                                boolean_owners, boolean_expression_limit);
                        if (!boolean_owner_ready) {
                            free(boolean_owners);
                            boolean_owners = NULL;
                        }
                    }
                }
            }
            {
                const bool call_index_ready =
                    boolean_calls != NULL &&
                    program->expression_count == boolean_expression_limit;
                const size_t scan_limit = call_index_ready
                    ? boolean_call_count : program->expression_count;
                size_t scan_position;
                for (scan_position = 0U; scan_position < scan_limit; ++scan_position) {
                    const MinicExpression *nested;
                    nested_expression_index = call_index_ready
                        ? boolean_calls[scan_position] : scan_position;
                    nested = &program->expressions[nested_expression_index];
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
                boolean_call_candidates += 1U;
                if (boolean_owner_ready &&
                    nested_expression_index < boolean_expression_limit) {
                    const MinicFunctionId owner =
                        boolean_owners[nested_expression_index];
                    if (owner != MINIC_FUNCTION_INVALID &&
                        owner < boolean_function_limit &&
                        owner != caller.specialization_source) {
                        boolean_skipped_other_owner += 1U;
                        continue;
                    }
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
                            minic_c0_call_argument(nested, parameter_index),
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
                        free(boolean_calls);
                        free(boolean_owners);
                        return false;
                    }
                    transitive_clone_count += 1U;
                }
                }
            }
        }
        if (getenv("MINIC_BOOLEAN_TRANSITIVE_PERF") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_BOOLEAN_TRANSITIVE_INDEX callers=%zu "
                          "index_builds=%zu call_entries=%zu candidates=%zu "
                          "skipped_other_owner=%zu calls_ready=%u "
                          "owners_ready=%u\n",
                          boolean_caller_scans, boolean_index_builds,
                          boolean_call_count, boolean_call_candidates,
                          boolean_skipped_other_owner,
                          (unsigned int)(boolean_calls != NULL),
                          (unsigned int)boolean_owner_ready);
        }
        free(boolean_calls);
        free(boolean_owners);
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

static bool minic_inline_integer_source_has_residual_reference(
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
        if ((expression->kind == MINIC_EXPRESSION_FUNCTION &&
             expression->value.function_id == source_id) ||
            (expression->kind == MINIC_EXPRESSION_CALL &&
             expression->value.call.function_id == source_id)) {
            return true;
        }
    }
    return false;
}

bool minic_finalize_inline_integer_specialization_references(MinicC0Program *program) {
    unsigned char *source_residual_cache = NULL;
    bool cache_allocation_failed = false;
    size_t function_index;

    if (program == NULL) {
        return false;
    }
    /* Generic reachability ran before cloning, while FunctionBody ownership was still
     * one-to-one. Preserve that result for clones first, then retire a source only when
     * specialization rewrote every syntactic/function-address reference to it. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization) {
            MinicFunctionId source_id = function->specialization_source;
            if (source_id >= program->function_count ||
                program->functions[source_id].is_integer_specialization) {
                return false;
            }
            function->is_referenced = program->functions[source_id].is_referenced;
        }
    }
    /* Calls and function-address references are no longer rewritten after
     * specialization. All clones of one source therefore ask the same question.
     * Cache only within this finalization pass, after validating source IDs;
     * on allocation failure, retain the original source-wide scan. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization) {
            MinicFunctionId source_id = function->specialization_source;
            MinicFunction *source = &program->functions[source_id];
            if (source->is_internal && source->is_inline && source->is_referenced) {
                bool has_residual_reference;
                if (source_residual_cache == NULL && !cache_allocation_failed) {
                    source_residual_cache = (unsigned char *)calloc(
                        program->function_count == 0U ? 1U : program->function_count,
                        sizeof(*source_residual_cache));
                    cache_allocation_failed = source_residual_cache == NULL;
                }
                if (source_residual_cache != NULL &&
                    source_residual_cache[source_id] != 0U) {
                    has_residual_reference = source_residual_cache[source_id] == 1U;
                } else {
                    has_residual_reference =
                        minic_inline_integer_source_has_residual_reference(program, source_id);
                    if (source_residual_cache != NULL) {
                        source_residual_cache[source_id] =
                            has_residual_reference ? 1U : 2U;
                    }
                }
                if (!has_residual_reference) {
                    source->is_referenced = false;
                }
            }
        }
    }
    free(source_residual_cache);
    return true;
}


