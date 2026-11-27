/* Whole-source zero-only Boolean proof, shared by CFG pruning and facts.
 * The latter may discount positively proven unreachable simple assignments. */
#include "compiler/c_lower/core_lower_internal.h"

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
bool core_source_constant_is_zero(const MinicCoreLowerContext *context,
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

/* Both modes require the same dominating leading assignment region. */
static bool core_bool_has_entry_zero_initializer(
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
            return core_source_constant_is_zero(context, statement->expression);
        }
    }
    return false;
}

/* Core lower_block owns the canonical external goto/asm-goto reentry
   oracle.  It is defined later in this translation unit. */
bool minic_core_lower_unreachable_external_reentry(
    const MinicCoreLowerContext *context,
    const MinicStatement *root_statement,
    MinicStatementId extra_statement_id);

/* The frontend still retains syntactic stores following a guaranteed
   return.  Core lower_block skips exactly these unreachable tails.  Discount
   such stores from a whole-function invariant proof, but only if there is
   no label/case/default reentry between the terminator and that store. */
static bool core_program_statement_must_return(
    const MinicCoreLowerContext *context,
    const MinicStatement *statement) {
    const MinicC0Program *program;
    const MinicBlock *then_block;
    const MinicStatement *first;
    MinicConstValue constant;
    bool zero;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || statement == NULL) {
        return false;
    }
    if (statement->kind == MINIC_STATEMENT_RETURN) {
        return true;
    }
    if (statement->kind != MINIC_STATEMENT_IF ||
        statement->then_block == MINIC_BLOCK_INVALID) {
        return false;
    }
    program = context->body->program;
    if (!minic_const_eval_integer(program, context->target,
                                  statement->expression, &constant) ||
        !minic_const_value_is_zero(program, context->target,
                                   &constant, &zero) || zero) {
        return false;
    }
    then_block = minic_c0_program_block(program, statement->then_block);
    if (then_block == NULL || then_block->statement_count == 0U) {
        return false;
    }
    first = minic_c0_program_statement(program, then_block->statements[0]);
    return first != NULL && first->kind == MINIC_STATEMENT_RETURN;
}

static bool core_program_stmt_is_dead_after_return(
    const MinicCoreLowerContext *context, MinicStatementId statement_id) {
    const MinicC0Program *program;
    size_t block_index;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL) {
        return false;
    }
    for (block_index = 0U; block_index < program->block_count; ++block_index) {
        const MinicBlock *block = &program->blocks[block_index];
        size_t i;
        bool must_return = false;
        for (i = 0U; i < block->statement_count; ++i) {
            const MinicStatement *statement =
                minic_c0_program_statement(program, block->statements[i]);
            if (statement == NULL) {
                return false;
            }
            if (block->statements[i] == statement_id) {
                return must_return &&
                    !minic_core_lower_unreachable_external_reentry(
                        context, statement, MINIC_STATEMENT_INVALID);
            }
            if (statement->kind == MINIC_STATEMENT_LABEL ||
                statement->kind == MINIC_STATEMENT_CASE ||
                statement->kind == MINIC_STATEMENT_DEFAULT) {
                must_return = false;
            } else if (core_program_statement_must_return(context, statement)) {
                must_return = true;
            }
        }
    }
    return false;
}

/* Parser may encode an ordinary "x = 1;" as a statement-expression
   containing MINIC_EXPRESSION_ASSIGNMENT rather than MINIC_STATEMENT_ASSIGN.
   The same proven unreachable-tail rule must apply to that exact statement.
   Do not generalize to assignments nested in potentially live expressions. */
static bool core_program_expr_is_dead_after_return(
    const MinicCoreLowerContext *context, MinicExpressionId expression_id) {
    const MinicC0Program *program;
    size_t i;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL) {
        return false;
    }
    for (i = 0U; i < program->statement_count; ++i) {
        const MinicStatement *stmt = &program->statements[i];
        if (stmt->kind != MINIC_STATEMENT_EXPRESSION ||
            stmt->expression != expression_id) {
            continue;
        }
        return core_program_stmt_is_dead_after_return(
            context, (MinicStatementId)i);
    }
    return false;
}

typedef struct CoreZeroBoolProof {
    const MinicCoreLowerContext *context;
    MinicLocalId local_id;
    bool allow_dead_assignments;
} CoreZeroBoolProof;

static bool core_zero_bool_check_effect(
    void *opaque, MinicLocalId id, MinicDirectLocalEffect effect,
    MinicExpressionId rhs, MinicStatementId statement_origin,
    MinicExpressionId expression_origin) {
    const CoreZeroBoolProof *proof = opaque;
    if (id != proof->local_id) return true;
    if (effect != MINIC_DIRECT_LOCAL_ASSIGN) return false;
    if (core_source_constant_is_zero(proof->context, rhs)) return true;
    if (!proof->allow_dead_assignments) return false;
    if (statement_origin != MINIC_STATEMENT_INVALID)
        return core_program_stmt_is_dead_after_return(proof->context, statement_origin);
    return expression_origin != MINIC_EXPRESSION_INVALID &&
           core_program_expr_is_dead_after_return(proof->context, expression_origin);
}

bool core_zero_bool_is_immutable(
    const MinicCoreLowerContext *context, MinicLocalId local_id, bool allow_dead_assignments) {
    const MinicC0Program *program;
    const MinicLocal *local;
    CoreZeroBoolProof proof = {context, local_id, allow_dead_assignments};
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
    if (local == NULL || minic_c0_local_array_object_info(
            program, local_id, NULL) || local->is_register_storage ||
        !minic_type_is_bool_integer(local->type) ||
        minic_type_is_volatile(local->type) ||
        !core_bool_has_entry_zero_initializer(context, local_id)) {
        return false;
    }
    return minic_c0_program_visit_direct_local_effects(
        program, core_zero_bool_check_effect, &proof);
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
                context, expression->value.local_id, false)) {
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


