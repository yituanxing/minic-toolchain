#include "compiler/inline_specialize.h"
#include "compiler/inline_specialize_internal.h"
#include "compiler/inline_specialize_common.h"
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
    const MinicTargetInvocation *target,
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
    const MinicTargetInvocation *target,
    MinicExpressionId expression_id) {
    return minic_inline_symbolic_static_expression_depth(
        program, target, expression_id, 0U);
}

/* Intern only synthetic pure closure expressions, not source AST nodes.
 * Reusing child IDs bottom-up makes this shallow equality canonical for all
 * builders below. Clone facts must not acquire a fresh ExpressionId every
 * time another caller traverses the same shared FunctionBody. */
static bool minic_inline_symbolic_add_closed_expression(
    MinicC0Program *program, const MinicExpression *closed,
    size_t synthetic_begin, MinicExpressionId *expression_id) {
    size_t i;
    if (program == NULL || closed == NULL || expression_id == NULL ||
        synthetic_begin > program->expression_count) return false;
    for (i = synthetic_begin; i < program->expression_count; ++i) {
        const MinicExpression *existing = &program->expressions[i];
        if (existing->kind != closed->kind ||
            !minic_type_equal(existing->type, closed->type) ||
            existing->value_category != closed->value_category) continue;
        switch (closed->kind) {
            case MINIC_EXPRESSION_INTEGER:
                if (existing->value.integer_value != closed->value.integer_value)
                    continue;
                break;
            case MINIC_EXPRESSION_CAST:
            case MINIC_EXPRESSION_BITCAST:
            case MINIC_EXPRESSION_CONVERSION:
            case MINIC_EXPRESSION_LVALUE_READ:
            case MINIC_EXPRESSION_ADDRESS_OF:
            case MINIC_EXPRESSION_DEREFERENCE:
                if (existing->value.unary.operand != closed->value.unary.operand)
                    continue;
                break;
            case MINIC_EXPRESSION_SUBSCRIPT:
                if (existing->value.subscript.base != closed->value.subscript.base ||
                    existing->value.subscript.index != closed->value.subscript.index)
                    continue;
                break;
            case MINIC_EXPRESSION_MEMBER:
                if (existing->value.member.base != closed->value.member.base ||
                    existing->value.member.record_id != closed->value.member.record_id ||
                    existing->value.member.field_index != closed->value.member.field_index)
                    continue;
                break;
            case MINIC_EXPRESSION_BINARY:
                if (existing->value.binary.operator_kind != closed->value.binary.operator_kind ||
                    existing->value.binary.left != closed->value.binary.left ||
                    existing->value.binary.right != closed->value.binary.right)
                    continue;
                break;
            default: return false;
        }
        *expression_id = i;
        return true;
    }
    return minic_c0_program_add_expression(program, closed, expression_id);
}

static bool minic_inline_close_symbolic_expression_depth(
    MinicC0Program *program,
    const MinicTargetInvocation *target,
    const MinicFunction *caller,
    const bool *stable_locals,
    size_t stable_local_count,
    size_t synthetic_begin,
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

    /* All child IDs survive recursive expression appends; the borrowed
     * Expression pointer does not.  Reacquire it before copying a parent. */
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
            stable_locals == NULL || local_id >= stable_local_count ||
            !stable_locals[local_id]) {
            return false;
        }
        if (minic_type_is_pointer(caller_source->parameter_types[parameter_index]) &&
            caller->specialization_symbolic_known[parameter_index] &&
            caller->specialization_symbolic_expression[parameter_index] !=
                MINIC_EXPRESSION_INVALID) {
            *closed_id = caller->specialization_symbolic_expression[parameter_index];
            *used_caller_integer_fact = true;
            return true;
        }
        if (!caller->specialization_integer_known[parameter_index] ||
            !minic_type_is_integer(caller_source->parameter_types[parameter_index]))
            return false;
        (void)memset(&closed, 0, sizeof(closed));
        closed.kind = MINIC_EXPRESSION_INTEGER;
        closed.span = expression->span;
        closed.type = caller_source->parameter_types[parameter_index];
        closed.value_category = MINIC_VALUE_RVALUE;
        bits = caller->specialization_integer_bits[parameter_index];
        (void)memcpy(&closed.value.integer_value, &bits, sizeof(bits));
        if (!minic_inline_symbolic_add_closed_expression(
                program, &closed, synthetic_begin, closed_id)) {
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
                    stable_locals,
                    stable_local_count,
                    synthetic_begin,
                    expression->value.unary.operand,
                    &left_id,
                    &left_used,
                    depth + 1U) ||
                !left_used) {
                return false;
            }
            /* A symbolic pointer argument may itself originate from a global
             * array. Preserve the source parameter's pointer type at this
             * read: substituting the bare GLOBAL_OBJECT (array type) into
             * `pointer + 1` would lose its pointee stride. */
            if (minic_type_is_pointer(expression->type)) {
                MinicExpression cast = *expression;
                cast.kind = MINIC_EXPRESSION_CAST;
                cast.value.unary.operand = left_id;
                if (!minic_inline_symbolic_add_closed_expression(
                        program, &cast, synthetic_begin, closed_id))
                    return false;
            } else {
                *closed_id = left_id;
            }
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
        MinicExpressionId operand_id = expression->value.unary.operand;
        if (!minic_inline_close_symbolic_expression_depth(
                program,
                target,
                caller,
                stable_locals,
                stable_local_count,
                synthetic_begin,
                operand_id,
                &left_id,
                &left_used,
                depth + 1U)) {
            return false;
        }
        if (!left_used) {
            *closed_id = expression_id;
            return true;
        }
        expression = minic_c0_program_expression(program, expression_id);
        if (expression == NULL) {
            return false;
        }
        closed = *expression;
        closed.value.unary.operand = left_id;
        if (!minic_inline_symbolic_add_closed_expression(
                program, &closed, synthetic_begin, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        MinicExpressionId base_id = expression->value.subscript.base;
        MinicExpressionId index_id = expression->value.subscript.index;
        bool base_static = minic_inline_symbolic_static_expression(
            program, target, base_id);
        if (base_static) {
            left_id = base_id;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       stable_locals,
                       stable_local_count,
                       synthetic_begin,
                       base_id,
                       &left_id,
                       &left_used,
                       depth + 1U)) {
            return false;
        }
        if (minic_const_eval_integer(
                program, target, index_id, &(MinicConstValue){0})) {
            right_id = index_id;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       stable_locals,
                       stable_local_count,
                       synthetic_begin,
                       index_id,
                       &right_id,
                       &right_used,
                       depth + 1U)) {
            return false;
        }
        if (!left_used && !right_used) {
            *closed_id = expression_id;
            return true;
        }
        expression = minic_c0_program_expression(program, expression_id);
        if (expression == NULL) {
            return false;
        }
        closed = *expression;
        closed.value.subscript.base = left_id;
        closed.value.subscript.index = right_id;
        if (!minic_inline_symbolic_add_closed_expression(
                program, &closed, synthetic_begin, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = left_used || right_used;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        MinicExpressionId base_id = expression->value.member.base;
        if (minic_inline_symbolic_static_expression(
                program, target, base_id)) {
            *closed_id = expression_id;
            return true;
        }
        if (!minic_inline_close_symbolic_expression_depth(
                program,
                target,
                caller,
                stable_locals,
                stable_local_count,
                synthetic_begin,
                base_id,
                &left_id,
                &left_used,
                depth + 1U)) {
            return false;
        }
        if (!left_used) {
            *closed_id = expression_id;
            return true;
        }
        expression = minic_c0_program_expression(program, expression_id);
        if (expression == NULL) {
            return false;
        }
        closed = *expression;
        closed.value.member.base = left_id;
        if (!minic_inline_symbolic_add_closed_expression(
                program, &closed, synthetic_begin, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = true;
        return true;
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        MinicExpressionId operand_left = expression->value.binary.left;
        MinicExpressionId operand_right = expression->value.binary.right;
        MinicConstValue ignored;
        if (minic_inline_symbolic_static_expression(
                program, target, operand_left) ||
            minic_const_eval_integer(program, target, operand_left, &ignored)) {
            left_id = operand_left;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       stable_locals,
                       stable_local_count,
                       synthetic_begin,
                       operand_left,
                       &left_id,
                       &left_used,
                       depth + 1U)) {
            return false;
        }
        if (minic_const_eval_integer(
                program, target, operand_right, &ignored) ||
            minic_inline_symbolic_static_expression(
                program, target, operand_right)) {
            right_id = operand_right;
        } else if (!minic_inline_close_symbolic_expression_depth(
                       program,
                       target,
                       caller,
                       stable_locals,
                       stable_local_count,
                       synthetic_begin,
                       operand_right,
                       &right_id,
                       &right_used,
                       depth + 1U)) {
            return false;
        }
        if (!left_used && !right_used) {
            *closed_id = expression_id;
            return true;
        }
        expression = minic_c0_program_expression(program, expression_id);
        if (expression == NULL) {
            return false;
        }
        closed = *expression;
        closed.value.binary.left = left_id;
        closed.value.binary.right = right_id;
        if (!minic_inline_symbolic_add_closed_expression(
                program, &closed, synthetic_begin, closed_id)) {
            return false;
        }
        *used_caller_integer_fact = left_used || right_used;
        return true;
    }

    return false;
}

static bool minic_inline_symbolic_argument_expression(
    MinicC0Program *program,
    const MinicTargetInvocation *target,
    const MinicFunction *caller,
    const bool *stable_locals,
    size_t stable_local_count,
    size_t synthetic_begin,
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
            stable_locals,
            stable_local_count,
            synthetic_begin,
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
            stable_locals,
            stable_local_count,
            synthetic_begin,
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
            stable_locals == NULL || local_id >= stable_local_count ||
            !stable_locals[local_id] ||
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
    const MinicTargetInvocation *target) {
    size_t expression_index;
    size_t initial_clone_count = 0U;
    size_t transitive_clone_count = 0U;
    size_t original_function_count;
    MinicInlineInitialOwners initial_owners = {0};
    bool *stable_locals = NULL;
    size_t stable_local_count;

    if (program == NULL || target == NULL) {
        return false;
    }
    original_function_count = program->function_count;
    stable_local_count = program->local_count;
    if (stable_local_count != 0U &&
        stable_local_count <= SIZE_MAX / sizeof(*stable_locals)) {
        stable_locals = malloc(stable_local_count * sizeof(*stable_locals));
        if (stable_locals != NULL && !minic_c0_program_stable_parameter_locals(
                program, stable_locals, stable_local_count)) {
            free(stable_locals);
            stable_locals = NULL;
        }
    }
    /* Failure to establish local stability must remove transitive ingress
     * facts, never let a modified pointer or integer parameter masquerade
     * as the original call value. */
    if (!minic_inline_initial_owners_build(
            program, original_function_count, &initial_owners)) {
        (void)fprintf(stderr, "MINIC_INLINE_SECTION_OWNER_INDEX=FAIL symbolic\n");
        free(stable_locals);
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
        MinicInlineSpecializationKey key;
        bool adds_symbol = false;
        size_t i;

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
        {
            MinicFunctionId owner = minic_inline_initial_owners_get(
                &initial_owners, expression_index);
            if (owner < original_function_count)
                caller = &program->functions[owner];
        }
        if (caller == NULL && current.is_integer_specialization)
            caller = &program->functions[current_id];
        if (!source.is_defined || !source.is_internal || !source.is_inline ||
            source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID ||
            source.parameter_count == 0U ||
            source.parameter_count != call->value.call.argument_count) {
            continue;
        }
        if (!minic_inline_spec_key_from_function(
                &key, &current, source.parameter_count)) {
            minic_inline_initial_owners_destroy(&initial_owners);
            free(stable_locals);
            return false;
        }
        /* Do not manufacture a half-specialized variant for a helper whose
         * integer parameters are still runtime values.  GNU immediate-only
         * asm inside helpers such as arch_static_branch requires both the
         * symbolic key and the branch integer to be closed before Core lower. */
        if (!minic_inline_symbolic_integer_inputs_are_closed(
                &source, key.integer_known)) {
            continue;
        }
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicExpressionId argument_id = minic_c0_call_argument(call, i);
            if (!minic_type_is_pointer(source.parameter_types[i]) ||
                minic_type_is_volatile(source.parameter_types[i]) ||
                !minic_inline_symbolic_static_expression(
                    program, target, argument_id)) {
                continue;
            }
            if (!key.symbolic_known[i] ||
                key.symbolic_expression[i] != argument_id) {
                key.symbolic_known[i] = true;
                key.symbolic_expression[i] = argument_id;
                adds_symbol = true;
            }
        }
        if (!adds_symbol) {
            continue;
        }
        variant_id = minic_inline_spec_find_variant(
            program, &source, caller, source_id, &key,
            MINIC_INLINE_SPEC_FACT_INTEGER | MINIC_INLINE_SPEC_FACT_SYMBOLIC, 0U);
        if (variant_id == MINIC_FUNCTION_INVALID) {
            if (program->function_count >= SIZE_MAX - 1U ||
                !minic_add_inline_symbolic_specialization(
                    program,
                    source_id,
                    caller,
                    key.integer_known,
                    key.integer_bits,
                    key.symbolic_known,
                    key.symbolic_expression,
                    &variant_id)) {
                minic_inline_initial_owners_destroy(&initial_owners);
                free(stable_locals);
                return false;
            }
            initial_clone_count += 1U;
        }
        program->expressions[expression_index].value.call.function_id = variant_id;
    }
    minic_inline_initial_owners_destroy(&initial_owners);

    {
        size_t caller_index;
        size_t synthetic_begin = program->expression_count;
        MinicInlineCallWorklist worklist = {0};
        /* Canonical source-owned CALL bucket + ambiguous/unowned bucket.
         * Common retains append-only expression IDs and an OOM full-scan
         * fallback, preserving the historical shared FunctionBody closure. */
        if (!minic_inline_call_worklist_build(
                program, original_function_count, &worklist)) {
            free(stable_locals);
            return false;
        }
        for (caller_index = 0U;
             caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            MinicInlineCallCursor cursor;
            MinicExpressionId nested_expression_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= program->function_count) {
                continue;
            }
            minic_inline_call_cursor_begin(
                &worklist, caller.specialization_source, &cursor);
            while (minic_inline_call_cursor_next(
                       &cursor, program, &nested_expression_index)) {
                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];
                MinicFunctionId current_id;
                MinicFunctionId source_id;
                MinicFunctionId variant_id = MINIC_FUNCTION_INVALID;
                MinicFunction current;
                MinicFunction source;
                MinicInlineSpecializationKey key;
                bool used_caller_fact = false;
                bool adds_symbol = false;
                bool needs_section_clone;
                size_t i;

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
                needs_section_clone = current.is_integer_specialization &&
                    !minic_inline_spec_section_matches(&current, &source, &caller);
                if (!minic_inline_spec_key_from_function(
                        &key, &current, source.parameter_count)) {
                    minic_inline_call_worklist_destroy(&worklist);
                    free(stable_locals);
                    return false;
                }
                if (!needs_section_clone &&
                    !minic_inline_symbolic_integer_inputs_are_closed(
                        &source, key.integer_known)) {
                    continue;
                }
                for (i = 0U; i < source.parameter_count; ++i) {
                    MinicExpressionId resolved_expression;
                    bool argument_used_caller_fact = false;
                    if (!minic_type_is_pointer(source.parameter_types[i]) ||
                        minic_type_is_volatile(source.parameter_types[i]) ||
                        !minic_inline_symbolic_argument_expression(
                            program,
                            target,
                            &caller,
                            stable_locals,
                            stable_local_count,
                            synthetic_begin,
                            /* Earlier arguments may have grown this arena. */
                            minic_c0_call_argument(
                                &program->expressions[nested_expression_index], i),
                            &resolved_expression,
                            &argument_used_caller_fact,
                            0U)) {
                        continue;
                    }
                    used_caller_fact =
                        used_caller_fact || argument_used_caller_fact;
                    if (!key.symbolic_known[i] ||
                        key.symbolic_expression[i] != resolved_expression) {
                        key.symbolic_known[i] = true;
                        key.symbolic_expression[i] = resolved_expression;
                        adds_symbol = true;
                    }
                }
                if ((!used_caller_fact || !adds_symbol) && !needs_section_clone) {
                    continue;
                }
                variant_id = minic_inline_spec_find_variant(
                    program, &source, &caller, source_id, &key,
                    MINIC_INLINE_SPEC_FACT_INTEGER |
                        MINIC_INLINE_SPEC_FACT_SYMBOLIC,
                    0U);
                if (variant_id == MINIC_FUNCTION_INVALID) {
                    if (!minic_add_inline_symbolic_specialization(
                            program,
                            source_id,
                            &caller,
                            key.integer_known,
                            key.integer_bits,
                            key.symbolic_known,
                            key.symbolic_expression,
                            &variant_id)) {
                        minic_inline_call_worklist_destroy(&worklist);
                        free(stable_locals);
                        return false;
                    }
                    transitive_clone_count += 1U;
                }
                /* A nested call is shared by every specialized caller that
                 * borrows this source FunctionBody. Never stamp a caller's
                 * chosen clone into its source CALL; Core resolves the
                 * concrete clone from the active caller specialization. */
            }
        }
        minic_inline_call_worklist_destroy(&worklist);
    }

    free(stable_locals);

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_SYMBOLIC_SPECIALIZATION_V0 direct=%zu transitive=%zu functions=%zu\n",
                      initial_clone_count,
                      transitive_clone_count,
                      program->function_count);
    }
    return true;
}




