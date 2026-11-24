/* One unit owns the whole-source zero-only-boolean CFG proof. Preserve the
 * fail-closed checks on address escape, mutation and entry dominance.
 * A source-level refactor only: this file MUST NOT change the proof rules.
 * The current per-query program-wide scans are a separate measured
 * performance question; do not add unsound caching during extraction.
 * Focused T1 compilation/link tests passed; full Linux runtime must use
 * a newly compiled compiler identity, never old cached C-owner outputs. */
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

/* Both proofs require the same dominating leading assignment region.
 * Each caller retains its exact constant evaluation predicate. */
bool core_bool_has_entry_zero_initializer(
    const MinicCoreLowerContext *context, MinicLocalId local_id,
    bool (*is_zero)(const MinicCoreLowerContext *, MinicExpressionId)) {
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
            return is_zero(context, statement->expression);
        }
    }
    return false;
}

typedef struct CoreZeroBoolProof {
    const MinicCoreLowerContext *context;
    MinicLocalId local_id;
} CoreZeroBoolProof;

static bool core_zero_bool_check_effect(
    void *opaque, MinicLocalId id, MinicDirectLocalEffect effect,
    MinicExpressionId rhs) {
    const CoreZeroBoolProof *proof = opaque;
    return id != proof->local_id ||
           (effect == MINIC_DIRECT_LOCAL_ASSIGN &&
            core_zero_bool_literal(proof->context, rhs));
}

static bool core_zero_bool_is_immutable(
    const MinicCoreLowerContext *context, MinicLocalId local_id) {
    const MinicC0Program *program;
    const MinicLocal *local;
    CoreZeroBoolProof proof = {context, local_id};
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
        !core_bool_has_entry_zero_initializer(context, local_id, core_zero_bool_literal)) {
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


