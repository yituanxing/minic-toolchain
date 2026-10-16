/* One unit owns the whole-source zero-only-boolean CFG proof. Preserve the
 * fail-closed checks on address escape, mutation and entry dominance.
 * A source-level refactor only: this file MUST NOT change the proof rules.
 * The current per-query program-wide scans are a separate measured
 * performance question; do not add unsound caching during extraction. */
#include "core/core_lower_internal.h"

#include "frontend/const_eval.h"

/*
 * CORE_ZERO_ONLY_BOOL_CFG_V1: one narrow, whole-source-function invariant.
 *
 * A nonvolatile _Bool local is identically zero only when a literal-zero
 * entry assignment dominates its defined reads, every syntactic write is
 * a literal zero, and there is no address-taking, RMW, or asm output.
 * Unlike speculative per-block facts this property survives joins/gotos.
 * On uncertainty this proof fails closed; ordinary CFG lowering remains.
 * This only prunes constant-if source branches when existing label-reentry
 * checks say the discarded block is not a legal jump target.
 */
static bool core_zero_bool_literal(const MinicCoreLowerContext *context,
                                   MinicExpressionId id) {
    MinicConstValue value;
    bool is_zero;
    return context != NULL && context->body != NULL &&
           context->body->program != NULL && context->target != NULL &&
           minic_const_eval_integer(context->body->program,
                                    context->target, id, &value) &&
           minic_const_value_is_zero(context->body->program,
                                     context->target, &value, &is_zero) &&
           is_zero;
}

static bool core_zero_bool_is_local(const MinicC0Program *program,
                                    MinicExpressionId id,
                                    MinicLocalId local_id) {
    const MinicExpression *expression =
        minic_c0_program_expression(program, id);
    return expression != NULL &&
           expression->kind == MINIC_EXPRESSION_LOCAL &&
           expression->value.local_id == local_id;
}

static bool core_zero_bool_has_entry_initializer(
    const MinicCoreLowerContext *context, MinicLocalId local_id) {
    const MinicC0Program *program = context->body->program;
    const MinicBlock *entry = minic_c0_program_block(
        program, context->source_function->body_block);
    size_t i;
    if (entry == NULL) {
        return false;
    }
    for (i = 0U; i < entry->statement_count && i < 64U; ++i) {
        const MinicStatement *statement =
            minic_c0_program_statement(program, entry->statements[i]);
        const MinicExpression *target;
        if (statement == NULL || statement->kind != MINIC_STATEMENT_ASSIGN) {
            break;
        }
        target = minic_c0_program_expression(
            program, statement->target_expression);
        if (target == NULL || target->kind != MINIC_EXPRESSION_LOCAL) {
            break;
        }
        if (target->value.local_id == local_id) {
            return core_zero_bool_literal(context, statement->expression);
        }
    }
    return false;
}

static bool core_zero_bool_is_immutable(
    const MinicCoreLowerContext *context, MinicLocalId local_id) {
    const MinicC0Program *program;
    const MinicLocal *local;
    size_t i;
    if (context == NULL || context->source_function == NULL ||
        context->body == NULL || context->body->program == NULL ||
        context->target == NULL ||
        local_id < context->source_function->local_begin ||
        local_id - context->source_function->local_begin >=
            context->source_function->local_count) {
        return false;
    }
    program = context->body->program;
    local = minic_c0_program_local(program, local_id);
    if (local == NULL || local->is_array || local->is_register_storage ||
        !minic_type_is_bool_integer(local->type) ||
        minic_type_is_volatile(local->type) ||
        !core_zero_bool_has_entry_initializer(context, local_id)) {
        return false;
    }
    for (i = 0U; i < program->statement_count; ++i) {
        const MinicStatement *statement = &program->statements[i];
        if (!core_zero_bool_is_local(
                program, statement->target_expression, local_id)) {
            continue;
        }
        if (statement->kind != MINIC_STATEMENT_ASSIGN ||
            !core_zero_bool_literal(context, statement->expression)) {
            return false;
        }
    }
    for (i = 0U; i < program->expression_count; ++i) {
        const MinicExpression *expression = &program->expressions[i];
        const MinicExpression *target;
        switch (expression->kind) {
        case MINIC_EXPRESSION_ADDRESS_OF:
            if (core_zero_bool_is_local(
                    program, expression->value.unary.operand, local_id)) {
                return false;
            }
            break;
        case MINIC_EXPRESSION_UNARY:
            if ((expression->value.unary.operator_kind ==
                     MINIC_UNARY_PRE_INCREMENT ||
                 expression->value.unary.operator_kind ==
                     MINIC_UNARY_POST_INCREMENT ||
                 expression->value.unary.operator_kind ==
                     MINIC_UNARY_PRE_DECREMENT ||
                 expression->value.unary.operator_kind ==
                     MINIC_UNARY_POST_DECREMENT) &&
                core_zero_bool_is_local(
                    program, expression->value.unary.operand, local_id)) {
                return false;
            }
            break;
        case MINIC_EXPRESSION_ASSIGNMENT:
        case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:
            target = minic_c0_program_expression(
                program, expression->value.binary.left);
            if (target != NULL && target->kind == MINIC_EXPRESSION_LOCAL &&
                target->value.local_id == local_id &&
                (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT ||
                 !core_zero_bool_literal(
                     context, expression->value.binary.right))) {
                return false;
            }
            break;
        default:
            break;
        }
    }
    for (i = 0U; i < program->inline_asm_count; ++i) {
        const MinicInlineAsm *asm_statement = &program->inline_asms[i];
        size_t j;
        for (j = 0U; j < asm_statement->output_count; ++j) {
            if (core_zero_bool_is_local(
                    program, asm_statement->outputs[j].expression, local_id)) {
                return false;
            }
        }
    }
    return true;
}

bool core_zero_bool_cfg_condition(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned int depth, bool *is_zero) {
    const MinicExpression *expression;
    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || is_zero == NULL ||
        depth > 16U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        if (!core_zero_bool_is_immutable(
                context, expression->value.local_id)) {
            return false;
        }
        *is_zero = true;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ ||
        expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        return core_zero_bool_cfg_condition(
            context, expression->value.unary.operand, depth + 1U, is_zero);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        if (!core_zero_bool_cfg_condition(
                context, expression->value.unary.operand, depth + 1U,
                is_zero)) {
            return false;
        }
        *is_zero = !*is_zero;
        return true;
    }
    return false;
}


