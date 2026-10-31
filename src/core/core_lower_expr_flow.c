#include "core/core_lower_internal.h"
#include "core/local_facts.h"
#include "core/cfg_constant_calls.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Expression control-flow owner. Every helper maintains source evaluation
 * order and Core CFG semantics, independent from target ABI or machine IR. */
typedef struct CoreExpressionFactFork {
    MinicCoreLocalIntegerConstant *entry;
    MinicCoreLocalIntegerConstant *first_path;
} CoreExpressionFactFork;

static void core_expression_fact_fork_dispose(CoreExpressionFactFork *fork) {
    if (fork == NULL) return;
    free(fork->first_path);
    free(fork->entry);
    fork->first_path = NULL;
    fork->entry = NULL;
}

static bool core_expression_fact_snapshot(
    MinicCoreLowerContext *context,
    MinicCoreLocalIntegerConstant **out) {
    if (context == NULL || context->source_function == NULL || out == NULL)
        return false;
    *out = minic_core_local_facts_snapshot(context);
    return context->source_function->local_count == 0U || *out != NULL;
}

static bool core_expression_fact_fork_begin(
    MinicCoreLowerContext *context, CoreExpressionFactFork *fork) {
    if (fork == NULL) return false;
    fork->entry = NULL;
    fork->first_path = NULL;
    return core_expression_fact_snapshot(context, &fork->entry);
}

/* The first arm was built; restore the branch-entry state before lowering
 * its mutually exclusive sibling, regardless of source spelling. */
static bool core_expression_fact_fork_second(
    MinicCoreLowerContext *context, CoreExpressionFactFork *fork) {
    if (fork == NULL ||
        !core_expression_fact_snapshot(context, &fork->first_path)) return false;
    minic_core_local_facts_restore(context, fork->entry);
    return true;
}

static bool core_expression_fact_fork_join(
    MinicCoreLowerContext *context, const CoreExpressionFactFork *fork) {
    return fork != NULL && minic_core_local_facts_meet(
        context, fork->first_path, context->local_integer_constants);
}

MinicCoreLowerStatus minic_core_lower_try_conditional(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    *handled = false;
    /* M57_CONSTANT_CONDITIONAL_PRUNING: if the frontend can prove
       the condition, lower only the selected arm. Besides being smaller CFG,
       this is semantically important for GNU compile-time choice idioms: the
       dead arm may contain target builtins that are never evaluated. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value &&
        expression->value.conditional.when_true != MINIC_EXPRESSION_INVALID &&
        expression->value.conditional.when_false != MINIC_EXPRESSION_INVALID &&
        context->target != NULL) {
        MinicConstValue condition_value;
        MinicExpressionId selected_expression;
        bool condition_is_zero;

        if (minic_const_eval_integer(context->body->program,
                                     context->target,
                                     expression->value.conditional.condition,
                                     &condition_value) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &condition_value,
                                      &condition_is_zero)) {
            selected_expression = condition_is_zero
                                      ? expression->value.conditional.when_false
                                      : expression->value.conditional.when_true;
            if (minic_type_is_void(expression->type)) {
                MinicCoreLowerStatus status;
                MinicCoreValueId discarded_value;

                status = lower_expression(context, selected_expression, &discarded_value);
                if (status != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return status; }
                }
                /* GNU mixed-void conditional arms may produce a scalar
                 * which is intentionally discarded. Both the scalar and
                 * void expression paths have already executed their effects. */
                *value_id = MINIC_CORE_VALUE_INVALID;
                { *handled = true; return MINIC_CORE_LOWER_OK; }
            }
            if (!core_memory_scalar_type(expression->type)) {
                { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
            }
            { *handled = true; return minic_core_lower_internal_lower_scalar_assignment_value(
                context, expression->type, selected_expression, value_id); }
        }
    }
    /* M53_VOID_CONDITIONAL_EXPRESSION: ISO C permits two void arms; GNU C
       additionally permits one void arm and one scalar-valued arm, whose
       resulting value is discarded. Model both as effect-only CFG, without
       a synthetic scalar result or spill object. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value &&
        expression->value.conditional.when_true != MINIC_EXPRESSION_INVALID &&
        expression->value.conditional.when_false != MINIC_EXPRESSION_INVALID &&
        minic_type_is_void(expression->type)) {
        const MinicExpression *false_expression;
        const MinicExpression *true_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;
        CoreExpressionFactFork facts;

        true_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_true);
        false_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_false);
        if (true_expression == NULL || false_expression == NULL) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (!minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        status = minic_core_lower_internal_lower_condition_branch(context,
                                        expression->value.conditional.condition,
                                        expression->span,
                                        true_block,
                                        false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        if (!core_expression_fact_fork_begin(context, &facts)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        context->block_id = true_block;
        status = lower_expression(
            context, expression->value.conditional.when_true, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            goto void_conditional_done;
        }
        status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            goto void_conditional_done;
        }
        if (!core_expression_fact_fork_second(context, &facts)) {
            status = MINIC_CORE_LOWER_ERROR;
            goto void_conditional_done;
        }

        context->block_id = false_block;
        status = lower_expression(
            context, expression->value.conditional.when_false, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            goto void_conditional_done;
        }
        status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            goto void_conditional_done;
        }

        context->block_id = merge_block;
        if (!core_expression_fact_fork_join(context, &facts)) {
            status = MINIC_CORE_LOWER_ERROR;
            goto void_conditional_done;
        }
        *value_id = MINIC_CORE_VALUE_INVALID;
void_conditional_done:
        core_expression_fact_fork_dispose(&facts);
        *handled = true;
        return status;
    }
    /* BATCH_N_GNU_OMITTED_MIDDLE_CONDITIONAL: GNU `a ?: b` reuses the
       already-evaluated condition value on the true path.  Materialize that
       converted value before branching so `a` is evaluated exactly once; the
       false path alone evaluates and overwrites with `b`. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        expression->value.conditional.uses_condition_value) {
        const MinicExpression *condition_expression;
        const MinicExpression *false_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreBlockId branch_true;
        MinicCoreBlockId branch_false;
        MinicCoreObjectId result_object;
        MinicCoreValueId condition_value;
        MinicCoreValueId true_value;
        MinicCoreValueId false_value;
        MinicCoreValueId branch_value;
        MinicCoreLowerStatus status;
        MinicCoreInstruction zero_test;
        MinicCoreTerminator terminator;
        MinicType assignment_type;
        MinicType result_type;
        CoreExpressionFactFork facts;

        if (expression->value.conditional.condition == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_true != expression->value.conditional.condition ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !core_scalar_expression_value_type(context->body, expression, &result_type) ||
            !core_memory_scalar_type(result_type)) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        condition_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.condition);
        false_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_false);
        if (condition_expression == NULL || false_expression == NULL ||
            (!minic_type_is_integer(condition_expression->type) &&
             !minic_type_is_pointer(condition_expression->type)) ||
            !minic_c0_assignment_compatible(context->body->program,
                                            result_type,
                                            expression->value.conditional.condition) ||
            !minic_c0_assignment_compatible(context->body->program,
                                            result_type,
                                            expression->value.conditional.when_false)) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, result_type, &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        status = lower_expression(
            context, expression->value.conditional.condition, &condition_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        if (condition_value >= context->function->value_count) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (minic_type_is_integer(result_type) &&
            minic_type_is_integer(condition_expression->type)) {
            if (!minic_c0_integer_assignment_value_type(
                    context->body->program,
                    result_type,
                    expression->value.conditional.condition,
                    &assignment_type)) {
                { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
            }
            status = minic_core_lower_internal_append_integer_conversion(context,
                                               condition_expression->span,
                                               assignment_type,
                                               condition_value,
                                               &true_value);
        } else if (minic_type_is_pointer(result_type) &&
                   (minic_type_is_pointer(condition_expression->type) ||
                    minic_c0_expression_is_null_pointer_constant_v0(
                        context->body->program,
                        expression->value.conditional.condition))) {
            status = append_scalar_bitcast(context,
                                           condition_expression->span,
                                           result_type,
                                           condition_value,
                                           &true_value);
        } else {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        status = minic_core_lower_internal_store_scalar_value(context,
                                    condition_expression->span,
                                    result_type,
                                    result_object,
                                    true_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }

        branch_value = condition_value;
        branch_true = true_block;
        branch_false = false_block;
        if (minic_type_is_pointer(condition_expression->type)) {
            MinicCoreBlockId original_true;

            if (!minic_type_is_pointer(context->function->values[branch_value].type)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            (void)memset(&zero_test, 0, sizeof(zero_test));
            zero_test.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
            zero_test.span = expression->span;
            zero_test.type = minic_type_int();
            zero_test.result = MINIC_CORE_VALUE_INVALID;
            zero_test.value.operand = branch_value;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &zero_test, &branch_value)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            original_true = branch_true;
            branch_true = branch_false;
            branch_false = original_true;
        } else if (!minic_type_is_integer(context->function->values[branch_value].type)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&terminator, 0, sizeof(terminator));
        terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
        terminator.span = expression->span;
        terminator.return_value = MINIC_CORE_VALUE_INVALID;
        terminator.conditional.condition = branch_value;
        terminator.conditional.when_true = branch_true;
        terminator.conditional.when_false = branch_false;
        if (!minic_core_function_set_terminator(
                context->function, context->block_id, &terminator)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (!core_expression_fact_fork_begin(context, &facts)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        context->block_id = true_block;
        status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            goto omitted_conditional_done;
        }
        if (!core_expression_fact_fork_second(context, &facts)) {
            status = MINIC_CORE_LOWER_ERROR;
            goto omitted_conditional_done;
        }

        context->block_id = false_block;
        status = minic_core_lower_internal_lower_scalar_assignment_value(context,
                                               result_type,
                                               expression->value.conditional.when_false,
                                               &false_value);
        if (status != MINIC_CORE_LOWER_OK) {
            goto omitted_conditional_done;
        }
        status = minic_core_lower_internal_store_scalar_value(context,
                                    false_expression->span,
                                    result_type,
                                    result_object,
                                    false_value);
        if (status != MINIC_CORE_LOWER_OK) {
            goto omitted_conditional_done;
        }
        status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            goto omitted_conditional_done;
        }

        context->block_id = merge_block;
        status = core_expression_fact_fork_join(context, &facts)
            ? minic_core_lower_internal_reload_scalar_value(
                context, expression->span, result_type, result_object, value_id)
            : MINIC_CORE_LOWER_ERROR;
omitted_conditional_done:
        core_expression_fact_fork_dispose(&facts);
        *handled = true;
        return status;
    }
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        const MinicExpression *false_expression;
        const MinicExpression *true_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreObjectId result_object;
        MinicCoreValueId arm_value;
        MinicCoreLowerStatus status;
        MinicType false_type;
        MinicType result_type;
        MinicType true_type;
        CoreExpressionFactFork facts;

        /* M60_POINTER_CONDITIONAL_VALUE: C conditional values may be pointer
           scalars as well as integers. The existing arm conversion, spill and
           reload machinery is already scalar-generic, so keep the semantic
           restriction at the Core scalar boundary rather than at integer-only. */
        if (expression->value.conditional.uses_condition_value ||
            expression->value.conditional.when_true == MINIC_EXPRESSION_INVALID ||
            expression->value.conditional.when_false == MINIC_EXPRESSION_INVALID ||
            !core_scalar_expression_value_type(context->body, expression, &result_type) ||
            !core_memory_scalar_type(result_type)) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        true_expression = minic_c0_program_expression(context->body->program,
                                                      expression->value.conditional.when_true);
        false_expression = minic_c0_program_expression(context->body->program,
                                                       expression->value.conditional.when_false);
        if (true_expression == NULL || false_expression == NULL) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        /* M55_SCALAR_CONDITIONAL_ARM_CONVERSION: the frontend owns the
           conditional result type. The selected arm undergoes the same scalar
           conversion as assignment to that type; its source type need not
           already be identical. */
        if (!core_scalar_expression_value_type(context->body, true_expression, &true_type) ||
            !core_scalar_expression_value_type(context->body, false_expression, &false_type)) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, result_type, &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        status = minic_core_lower_internal_lower_condition_branch(context,
                                        expression->value.conditional.condition,
                                        expression->span,
                                        true_block,
                                        false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        if (!core_expression_fact_fork_begin(context, &facts)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        context->block_id = true_block;
        status = minic_core_lower_internal_lower_scalar_assignment_value(context,
                                               result_type,
                                               expression->value.conditional.when_true,
                                               &arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            goto scalar_conditional_done;
        }
        status = minic_core_lower_internal_store_scalar_value(
            context, true_expression->span, result_type, result_object, arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            goto scalar_conditional_done;
        }
        status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            goto scalar_conditional_done;
        }
        if (!core_expression_fact_fork_second(context, &facts)) {
            status = MINIC_CORE_LOWER_ERROR;
            goto scalar_conditional_done;
        }

        context->block_id = false_block;
        status = minic_core_lower_internal_lower_scalar_assignment_value(context,
                                               result_type,
                                               expression->value.conditional.when_false,
                                               &arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            goto scalar_conditional_done;
        }
        status = minic_core_lower_internal_store_scalar_value(
            context, false_expression->span, result_type, result_object, arm_value);
        if (status != MINIC_CORE_LOWER_OK) {
            goto scalar_conditional_done;
        }
        status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            goto scalar_conditional_done;
        }

        context->block_id = merge_block;
        status = core_expression_fact_fork_join(context, &facts)
            ? minic_core_lower_internal_reload_scalar_value(
                context, expression->span, result_type, result_object, value_id)
            : MINIC_CORE_LOWER_ERROR;
scalar_conditional_done:
        core_expression_fact_fork_dispose(&facts);
        *handled = true;
        return status;
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_try_sequenced(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, MinicExpressionId expression_id, bool *handled) {
    MinicCoreInstruction instruction;
    *handled = false;
    /* M73_COMMA_EXPRESSION_VALUE: the left operand is sequenced for
       side effects and its scalar value is discarded; the right operand
       supplies the value of the whole comma expression. Unsupported left
       operand forms remain fail-closed through lower_expression(). */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
        const MinicExpression *discarded_expression;
        MinicStatement discarded_statement;
        MinicCoreLowerStatus status;

        discarded_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (discarded_expression == NULL) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&discarded_statement, 0, sizeof(discarded_statement));
        discarded_statement.kind = MINIC_STATEMENT_EXPRESSION;
        discarded_statement.span = discarded_expression->span;
        discarded_statement.expression = expression->value.binary.left;
        status = minic_core_lower_internal_lower_expression_statement(context, &discarded_statement);
        if (status != MINIC_CORE_LOWER_OK) {
            if (getenv("CORE_FAST_TRACE") != NULL) {
                const MinicExpression *right_expression =
                    minic_c0_program_expression(context->body->program,
                                                expression->value.binary.right);
                (void)fprintf(stderr,
                              "CORE_COMMA_TRACE function=%s stage=left status=%d left_kind=%d right_kind=%d\n",
                              context->source_function != NULL ? context->source_function->name : "?",
                              (int)status,
                              discarded_expression != NULL ? (int)discarded_expression->kind : -1,
                              right_expression != NULL ? (int)right_expression->kind : -1);
            }
            { *handled = true; return status; }
        }
        status = lower_expression(context, expression->value.binary.right, value_id);
        if (status != MINIC_CORE_LOWER_OK && getenv("CORE_FAST_TRACE") != NULL) {
            const MinicExpression *right_expression =
                minic_c0_program_expression(context->body->program,
                                            expression->value.binary.right);
            (void)fprintf(stderr,
                          "CORE_COMMA_TRACE function=%s stage=right status=%d left_kind=%d right_kind=%d right_base=%d right_ptr=%u\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          (int)status,
                          discarded_expression != NULL ? (int)discarded_expression->kind : -1,
                          right_expression != NULL ? (int)right_expression->kind : -1,
                          right_expression != NULL ? (int)right_expression->type.base_kind : -1,
                          right_expression != NULL ? right_expression->type.pointer_depth : 0U);
        }
        { *handled = true; return status; }
    }

    /* M58_LOGICAL_OR_VALUE: minic_core_lower_internal_lower_condition_branch already owns the
       short-circuit semantics for both && and ||. Their value materialization
       is identical: branch to true/false, store 1/0, then reload. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreObjectId result_object;
        MinicCoreValueId address_value;
        MinicCoreValueId constant_value;
        MinicCoreLowerStatus status;
        MinicType result_pointer_type;

        if (!minic_type_equal(expression->type, minic_type_int()) ||
            !minic_type_pointer_to(minic_type_int(), &result_pointer_type)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (!minic_core_function_add_object(
                context->function, expression->span, minic_type_int(), &result_object) ||
            !minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        status = minic_core_lower_internal_lower_condition_branch(
            context, expression_id, expression->span, true_block, false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }

        context->block_id = false_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 0;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant_value)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address_value;
        instruction.value.store.stored_value = constant_value;
        instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }

        context->block_id = true_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant_value)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address_value;
        instruction.value.store.stored_value = constant_value;
        instruction.value.store.is_volatile = false;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        status = minic_core_lower_internal_set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }

        context->block_id = merge_block;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.type = result_pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = result_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &address_value)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address_value;
        instruction.value.load.is_volatile = false;
        { *handled = true; return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }
    return MINIC_CORE_LOWER_OK;
}

/* GNU statement expression: AST block + optional tail expression -> Core
 * effects and an optional result. Perform all tail/type admission before
 * mutating Core blocks; lower the shared block exactly once. */
MinicCoreLowerStatus minic_core_lower_try_statement_expression(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    const MinicBlock *statement_block;
    const MinicExpression *statement_result;
    MinicExpressionId result_id;
    MinicCoreValueId result_value;
    MinicCoreLowerStatus status;
    MinicType result_type;
    bool result_is_void;
    bool terminated;

    if (handled == NULL || context == NULL || expression == NULL || value_id == NULL ||
        context->body == NULL || context->body->program == NULL ||
        context->function == NULL)
        return MINIC_CORE_LOWER_ERROR;
    *handled = false;
    if (expression->kind != MINIC_EXPRESSION_STATEMENT)
        return MINIC_CORE_LOWER_OK;
    *handled = true;
    result_id = expression->value.statement_expression.result;

    statement_block = minic_c0_program_block(
        context->body->program, expression->value.statement_expression.block);
    if (statement_block == NULL)
        return MINIC_CORE_LOWER_ERROR;

    result_is_void = minic_type_is_void(expression->type);
    if (result_id == MINIC_EXPRESSION_INVALID) {
        if (!result_is_void)
            return MINIC_CORE_LOWER_ERROR;
    } else {
        /* Preserve the original scalar type admission before tail lookup. */
        if (!result_is_void &&
            !core_scalar_expression_value_type(context->body, expression, &result_type))
            return MINIC_CORE_LOWER_UNSUPPORTED;
        statement_result = minic_c0_program_expression(
            context->body->program, result_id);
        if (statement_result == NULL)
            return MINIC_CORE_LOWER_ERROR;
        if (result_is_void && !minic_type_is_void(statement_result->type))
            return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = minic_core_lower_internal_lower_block(context, statement_block, &terminated);
    if (status != MINIC_CORE_LOWER_OK)
        return status;
    if (terminated)
        return MINIC_CORE_LOWER_UNSUPPORTED;

    if (result_id == MINIC_EXPRESSION_INVALID) {
        *value_id = MINIC_CORE_VALUE_INVALID;
        return MINIC_CORE_LOWER_OK;
    }
    status = lower_expression(context, result_id, &result_value);
    if (status != MINIC_CORE_LOWER_OK)
        return status;
    if (result_is_void) {
        if (result_value != MINIC_CORE_VALUE_INVALID)
            return MINIC_CORE_LOWER_ERROR;
        *value_id = MINIC_CORE_VALUE_INVALID;
        return MINIC_CORE_LOWER_OK;
    }
    if (result_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[result_value].type, result_type))
        return MINIC_CORE_LOWER_ERROR;
    *value_id = result_value;
    return MINIC_CORE_LOWER_OK;
}

