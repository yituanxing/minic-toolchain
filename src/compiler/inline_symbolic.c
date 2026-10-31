#include "compiler/inline_specialize.h"
#include "compiler/inline_specialize_internal.h"
#include "frontend/const_eval.h"
#include "frontend/function_body.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compiler AST pass: symbolic parameter analysis and clone generation.
 * Function bodies remain from the validated monolithic optimizer. */
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

    /* FIXME(MC-058): transitive closure carries direct symbolic operands and
     * integer facts, but not (known global pointer + constant element offset).
     * GNU "i" operand users therefore fail for a one-hop specialized caller. */

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
    const MinicFunction *source,
    const MinicFunction *caller,
    MinicFunctionId source_id,
    const bool *integer_known,
    const uint64_t *integer_bits,
    const bool *symbolic_known,
    const MinicExpressionId *symbolic_expression,
    size_t parameter_count) {
    size_t i;

    if (candidate == NULL || !candidate->is_integer_specialization ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count ||
        !minic_inline_specialization_section_matches(candidate, source, caller)) {
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

bool minic_add_inline_symbolic_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const MinicFunction *caller,
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
            caller,
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
    MinicFunctionId *initial_expression_owners = NULL;
    bool initial_owner_index_ready = false;

    if (program == NULL || target == NULL) {
        return false;
    }
    original_function_count = program->function_count;
    if (minic_inline_section_owner_index_required(program) &&
        program->expression_count != 0U &&
        program->expression_count <= SIZE_MAX / sizeof(*initial_expression_owners)) {
        initial_expression_owners = malloc(program->expression_count *
                                           sizeof(*initial_expression_owners));
        if (initial_expression_owners != NULL)
            initial_owner_index_ready = minic_perf_program_build_expression_source_owners(
                program, original_function_count, initial_expression_owners,
                program->expression_count);
    }
    if (minic_inline_section_owner_index_required(program) &&
        program->expression_count != 0U && !initial_owner_index_ready) {
        free(initial_expression_owners);
        (void)fprintf(stderr, "MINIC_INLINE_SECTION_OWNER_INDEX=FAIL symbolic\n");
        return false;
    }

    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        MinicExpression *call = &program->expressions[expression_index];
        MinicFunctionId current_id;
        MinicFunctionId source_id;
        MinicFunctionId variant_id = MINIC_FUNCTION_INVALID;
        MinicFunction current;
        MinicFunction source;
        const MinicFunction *caller = NULL;
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
        if (initial_owner_index_ready &&
            initial_expression_owners[expression_index] < original_function_count)
            caller = &program->functions[initial_expression_owners[expression_index]];
        else if (current.is_integer_specialization)
            caller = &program->functions[current_id];
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
                    &source, caller,
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
                    caller,
                    integer_known,
                    integer_bits,
                    symbolic_known,
                    symbolic_expression,
                    &variant_id)) {
                free(initial_expression_owners);
                return false;
            }
            initial_clone_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = variant_id;
    }
    free(initial_expression_owners);

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
                            &source, &caller,
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
                            &caller,
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




