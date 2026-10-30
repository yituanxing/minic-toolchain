#include "core/cfg_constant_calls.h"
#include "core/local_facts.h"
#include "core/opt_pure_statement.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original conservative CFG constant-return evaluator. */
static bool core_cfg_eval_callee_return_expression(
    const MinicCoreLowerContext *caller_context,
    const MinicFunction *callee,
    const MinicExpression *call_expression,
    MinicExpressionId return_expression,
    MinicConstValue *value) {
    MinicCoreLowerContext callee_context;
    MinicCoreLocalIntegerConstant *facts = NULL;
    size_t parameter_index;
    bool success;

    if (caller_context == NULL || caller_context->body == NULL ||
        caller_context->body->program == NULL || caller_context->target == NULL ||
        callee == NULL || call_expression == NULL || value == NULL ||
        call_expression->kind != MINIC_EXPRESSION_CALL ||
        callee->parameter_count != call_expression->value.call.argument_count ||
        caller_context->cfg_constant_call_depth >= 16U) {
        return false;
    }
    if (callee->local_count != 0U) {
        facts = (MinicCoreLocalIntegerConstant *)calloc(
            callee->local_count, sizeof(*facts));
        if (facts == NULL) {
            return false;
        }
    }
    callee_context = *caller_context;
    callee_context.source_function = callee;
    callee_context.local_integer_constants = facts;
    callee_context.cfg_constant_call_depth =
        caller_context->cfg_constant_call_depth + 1U;

    for (parameter_index = 0U; parameter_index < callee->parameter_count;
         ++parameter_index) {
        MinicConstValue argument;
        MinicLocalId local_id;

        if (!minic_type_is_integer(callee->parameter_types[parameter_index]) ||
            !core_const_eval_integer_with_locals(
                caller_context,
                minic_c0_call_argument(call_expression, parameter_index),
                &argument)) {
            continue;
        }
        local_id = callee->local_begin + parameter_index;
        core_local_constant_set(&callee_context, local_id, &argument);
    }

    success = core_const_eval_integer_with_locals(
        &callee_context, return_expression, value);
    free(facts);
    return success;
}

/* A file-local pointer getter with one unconditional return may read ordinary
 * (nonvolatile) fields, but it must not have an observable side effect.  This
 * is a purity proof, NOT a constant-value proof: a caller may discard such a
 * getter only when the outer call's result is independently known constant. */
static bool core_cfg_pure_pointer_getter_call(const MinicCoreLowerContext *context,
                                              const MinicExpression *expression,
                                              unsigned int depth) {
    const MinicC0Program *program;
    const MinicFunction *callee;
    const MinicBlock *body;
    const MinicStatement *first;
    size_t i;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression == NULL || expression->kind != MINIC_EXPRESSION_CALL ||
        expression->value.call.function_id == MINIC_FUNCTION_INVALID || depth > 16U)
        return false;
    program = context->body->program;
    callee = minic_c0_program_function(program, expression->value.call.function_id);
    if (callee == NULL || !callee->is_defined || !callee->is_internal ||
        !minic_type_is_pointer(callee->return_type) ||
        callee->body_block == MINIC_BLOCK_INVALID ||
        callee->parameter_count != expression->value.call.argument_count)
        return false;
    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count == 0U)
        return false;
    first = minic_c0_program_statement(program, body->statements[0]);
    if (first == NULL || first->kind != MINIC_STATEMENT_RETURN ||
        first->expression == MINIC_EXPRESSION_INVALID ||
        first->cleanup_context != first->cleanup_stop_context ||
        !minic_core_cfg_pure_call_argument(context, first->expression, depth + 1U))
        return false;
    for (i = 0U; i < expression->value.call.argument_count; ++i)
        if (!minic_core_cfg_pure_call_argument(
                context, minic_c0_call_argument(expression, i), depth + 1U))
            return false;
    return true;
}

/* LINUX_CONSTANT_INLINE_CALL_CFG_V0: feature-disabled Linux headers often
 * expose static-inline predicates that are exactly one constant return.  Such
 * a call is safe to consume as a CFG fact when all arguments are side-effect
 * free.  Ordinary expression lowering remains unchanged. */
bool minic_core_cfg_pure_call_argument(const MinicCoreLowerContext *context,
                                        MinicExpressionId expression_id,
                                        unsigned int depth) {
    const MinicExpression *expression;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        depth > 32U) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || minic_type_is_volatile(expression->type)) {
        return false;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
    case MINIC_EXPRESSION_SIZEOF:
    case MINIC_EXPRESSION_OFFSETOF:
    case MINIC_EXPRESSION_FUNCTION:
    case MINIC_EXPRESSION_GLOBAL_OBJECT:
    case MINIC_EXPRESSION_LABEL_ADDRESS:
        return true;
    case MINIC_EXPRESSION_LOCAL: {
        const MinicLocal *local = minic_c0_program_local(
            context->body->program, expression->value.local_id);
        return local != NULL && !minic_type_is_volatile(local->type);
    }
    case MINIC_EXPRESSION_BUILTIN_UNARY:
        /* __builtin_constant_p inspects its operand without evaluating it.
         * In particular it remains pure for operands with runtime reads or
         * side effects, and Core's local-fact evaluator can prove the result
         * without emitting the impossible arm of a compile-time guard. */
        if (expression->value.builtin_unary.operator_kind ==
            MINIC_BUILTIN_UNARY_CONSTANT_P) return true;
        /* Other represented unary builtins (CLZ/CTZ/POPCOUNT/FFS, byte swap,
         * ISDIGIT and ISNAN) are effect-free exactly when their operand is.
         * In particular FIELD_GET's __builtin_ffsll(mask) is not a CALL. */
        return minic_core_cfg_pure_call_argument(
            context, expression->value.builtin_unary.operand, depth + 1U);
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_ADDRESS_OF:
    case MINIC_EXPRESSION_DEREFERENCE:
    case MINIC_EXPRESSION_LVALUE_READ:
        return minic_core_cfg_pure_call_argument(
            context, expression->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_UNARY:
        return expression->value.unary.operator_kind <= MINIC_UNARY_BITWISE_NOT &&
               minic_core_cfg_pure_call_argument(
                   context, expression->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_MEMBER:
        return minic_core_cfg_pure_call_argument(
            context, expression->value.member.base, depth + 1U);
    case MINIC_EXPRESSION_SUBSCRIPT:
        return minic_core_cfg_pure_call_argument(context,
                                           expression->value.subscript.base,
                                           depth + 1U) &&
               minic_core_cfg_pure_call_argument(context,
                                           expression->value.subscript.index,
                                           depth + 1U);
    case MINIC_EXPRESSION_BINARY:
        return expression->value.binary.operator_kind != MINIC_BINARY_COMMA &&
               minic_core_cfg_pure_call_argument(context,
                                           expression->value.binary.left,
                                           depth + 1U) &&
               minic_core_cfg_pure_call_argument(context,
                                           expression->value.binary.right,
                                           depth + 1U);
    case MINIC_EXPRESSION_CONDITIONAL:
        return minic_core_cfg_pure_call_argument(context,
                                           expression->value.conditional.condition,
                                           depth + 1U) &&
               minic_core_cfg_pure_call_argument(context,
                                           expression->value.conditional.when_true,
                                           depth + 1U) &&
               minic_core_cfg_pure_call_argument(context,
                                           expression->value.conditional.when_false,
                                           depth + 1U);
    case MINIC_EXPRESSION_STATEMENT:
        /* GNU ({...}) is not automatically pure.  Only the single shared
         * no-effect guard proof may certify its prefix; its result must
         * independently pass this expression-purity check. */
        if (!minic_opt_pure_statement_block(
                context->body->program, context->target,
                context->reentry_index,
                expression->value.statement_expression.block,
                (void *)context, minic_core_cfg_pure_guard_truth, depth + 1U))
            return false;
        return expression->value.statement_expression.result ==
                   MINIC_EXPRESSION_INVALID ||
               minic_core_cfg_pure_call_argument(
                   context, expression->value.statement_expression.result,
                   depth + 1U);
    case MINIC_EXPRESSION_CALL: {
        MinicConstValue folded;
        /* An ordinary call may mutate memory and must remain a CFG effect
         * barrier. The already-bounded static-inline constant-return
         * evaluator proves a narrower case: all actual arguments are pure,
         * and the body computes one constant without externally visible
         * effects. This is needed by Linux AArch64 __fls(mask) inside
         * BUILD_BUG_ON guards after a KVM flag's constant mask specializes. */
        return minic_core_cfg_constant_inline_call(context, expression, &folded) ||
               core_cfg_pure_pointer_getter_call(context, expression, depth + 1U);
    }
    default:
        return false;
    }
}


/* M193_SMALL_CONSTANT_ARRAY_LOOP_CFG */
static bool core_cfg_small_loop_target_local(const MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicLocalId *local_id) {
    const MinicExpression *expression;
    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        local_id == NULL || expression_id == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
        expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    *local_id = expression->value.local_id;
    return true;
}

static bool core_cfg_small_loop_add_values(MinicCoreLowerContext *context,
                                           const MinicConstValue *left,
                                           const MinicConstValue *right,
                                           MinicType result_type,
                                           bool subtract,
                                           MinicConstValue *result) {
    MinicType common_type;
    MinicConstValue l;
    MinicConstValue r;
    MinicConstValue common_result;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || left == NULL || right == NULL || result == NULL ||
        !minic_type_is_integer(left->type) || !minic_type_is_integer(right->type) ||
        !minic_target_info_integer_common_for_program(context->target,
                                                      context->body->program,
                                                      left->type,
                                                      right->type,
                                                      &common_type) ||
        !minic_const_value_convert_integer(context->body->program,
                                           context->target,
                                           left,
                                           common_type,
                                           &l) ||
        !minic_const_value_convert_integer(context->body->program,
                                           context->target,
                                           right,
                                           common_type,
                                           &r)) {
        return false;
    }
    /* The interpreter is used only for tiny bounded loops.  Refuse signed
       arithmetic when it could overflow instead of relying on host behavior. */
    if (minic_type_is_signed_integer(common_type)) {
        int64_t ls;
        int64_t rs;
        if (!core_const_signed_value(context, &l, &ls) ||
            !core_const_signed_value(context, &r, &rs) ||
            (!subtract && ((rs > 0 && ls > INT64_MAX - rs) ||
                           (rs < 0 && ls < INT64_MIN - rs))) ||
            (subtract && ((rs < 0 && ls > INT64_MAX + rs) ||
                          (rs > 0 && ls < INT64_MIN + rs)))) {
            return false;
        }
    }
    common_result.type = common_type;
    common_result.bits = subtract ? l.bits - r.bits : l.bits + r.bits;
    return minic_const_value_convert_integer(context->body->program,
                                             context->target,
                                             &common_result,
                                             result_type,
                                             result);
}

static bool core_cfg_small_loop_update_expression(MinicCoreLowerContext *context,
                                                  MinicExpressionId expression_id) {
    const MinicExpression *expression;
    const MinicExpression *left_expression;
    MinicLocalId local_id;
    MinicConstValue left;
    MinicConstValue right;
    MinicConstValue result;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression_id == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) &&
        core_cfg_small_loop_target_local(
            context, expression->value.binary.left, &local_id) &&
        minic_core_local_fact_get(context, local_id, &left) &&
        core_const_eval_integer_with_locals(
            context, expression->value.binary.right, &right)) {
        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        if (left_expression == NULL ||
            !core_cfg_small_loop_add_values(
                context,
                &left,
                &right,
                left_expression->type,
                expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT,
                &result)) {
            return false;
        }
        core_local_constant_set(context, local_id, &result);
        return minic_core_local_fact_get(context, local_id, &left);
    }
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT &&
        core_cfg_small_loop_target_local(
            context, expression->value.binary.left, &local_id) &&
        core_const_eval_integer_with_locals(
            context, expression->value.binary.right, &result)) {
        core_local_constant_set(context, local_id, &result);
        return minic_core_local_fact_get(context, local_id, &left);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) &&
        core_cfg_small_loop_target_local(
            context, expression->value.unary.operand, &local_id) &&
        minic_core_local_fact_get(context, local_id, &left)) {
        MinicConstValue one;
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand == NULL) {
            return false;
        }
        one.type = minic_type_int();
        one.bits = 1U;
        if (!core_cfg_small_loop_add_values(
                context,
                &left,
                &one,
                operand->type,
                expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
                    expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT,
                &result)) {
            return false;
        }
        core_local_constant_set(context, local_id, &result);
        return minic_core_local_fact_get(context, local_id, &left);
    }
    return false;
}

static bool core_cfg_small_loop_execute_statement(MinicCoreLowerContext *context,
                                                  const MinicStatement *statement) {
    MinicLocalId local_id;
    MinicConstValue value;
    MinicConstValue check;

    if (context == NULL || statement == NULL ||
        statement->cleanup_context != statement->cleanup_stop_context) {
        return false;
    }
    if (statement->kind == MINIC_STATEMENT_LABEL) {
        return true;
    }
    if (statement->kind == MINIC_STATEMENT_ASSIGN &&
        core_cfg_small_loop_target_local(
            context, statement->target_expression, &local_id) &&
        statement->expression != MINIC_EXPRESSION_INVALID &&
        core_const_eval_integer_with_locals(context, statement->expression, &value)) {
        core_local_constant_set(context, local_id, &value);
        return minic_core_local_fact_get(context, local_id, &check);
    }
    if (statement->kind == MINIC_STATEMENT_EXPRESSION &&
        statement->expression != MINIC_EXPRESSION_INVALID) {
        return core_cfg_small_loop_update_expression(context, statement->expression);
    }
    return false;
}

static bool core_cfg_small_constant_loop_call(const MinicCoreLowerContext *caller_context,
                                              const MinicFunction *callee,
                                              const MinicExpression *call_expression,
                                              MinicConstValue *value) {
    const MinicC0Program *program;
    const MinicBlock *body;
    MinicCoreLowerContext callee_context;
    MinicCoreLocalIntegerConstant *facts;
    size_t statement_index;
    bool saw_loop;

    if (caller_context == NULL || caller_context->body == NULL ||
        caller_context->body->program == NULL || caller_context->target == NULL ||
        callee == NULL || call_expression == NULL || value == NULL ||
        call_expression->kind != MINIC_EXPRESSION_CALL ||
        call_expression->value.call.argument_count != 0U || callee->parameter_count != 0U ||
        callee->body_block == MINIC_BLOCK_INVALID || callee->local_count == 0U ||
        callee->local_count > 32U) {
        return false;
    }
    program = caller_context->body->program;
    body = minic_c0_program_block(program, callee->body_block);
    /* M194_SMALL_LOOP_PROBE */
    if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL && callee->name != NULL &&
        strcmp(callee->name, "skb_ext_total_length") == 0 && body != NULL) {
        size_t probe_i;
        (void)fprintf(stderr, "SMALL_LOOP_PROBE name=%s body=%zu locals=%zu\n",
                      callee->name, body->statement_count, callee->local_count);
        for (probe_i = 0U; probe_i < body->statement_count; ++probe_i) {
            const MinicStatement *ps = minic_c0_program_statement(program, body->statements[probe_i]);
            const MinicExpression *pe = ps != NULL && ps->expression != MINIC_EXPRESSION_INVALID
                                            ? minic_c0_program_expression(program, ps->expression)
                                            : NULL;
            (void)fprintf(stderr,
                          "SMALL_LOOP_TOP i=%zu kind=%d exprkind=%d then=%zu target=%zu clean=%zu/%zu\n",
                          probe_i, ps == NULL ? -1 : (int)ps->kind,
                          pe == NULL ? -1 : (int)pe->kind,
                          ps == NULL ? (size_t)MINIC_BLOCK_INVALID : (size_t)ps->then_block,
                          ps == NULL ? (size_t)MINIC_EXPRESSION_INVALID : (size_t)ps->target_expression,
                          ps == NULL ? 0U : (size_t)ps->cleanup_context,
                          ps == NULL ? 0U : (size_t)ps->cleanup_stop_context);
            if (ps != NULL && ps->kind == MINIC_STATEMENT_WHILE &&
                ps->then_block != MINIC_BLOCK_INVALID) {
                const MinicBlock *pb = minic_c0_program_block(program, ps->then_block);
                size_t probe_j;
                (void)fprintf(stderr, "SMALL_LOOP_BODY count=%zu\n", pb == NULL ? 0U : pb->statement_count);
                if (pb != NULL) {
                    for (probe_j = 0U; probe_j < pb->statement_count; ++probe_j) {
                        const MinicStatement *bs = minic_c0_program_statement(program, pb->statements[probe_j]);
                        const MinicExpression *be = bs != NULL && bs->expression != MINIC_EXPRESSION_INVALID
                                                        ? minic_c0_program_expression(program, bs->expression)
                                                        : NULL;
                        (void)fprintf(stderr,
                                      "SMALL_LOOP_STMT j=%zu kind=%d exprkind=%d op=%d target=%zu clean=%zu/%zu\n",
                                      probe_j, bs == NULL ? -1 : (int)bs->kind,
                                      be == NULL ? -1 : (int)be->kind,
                                      be != NULL && (be->kind == MINIC_EXPRESSION_BINARY ||
                                                     be->kind == MINIC_EXPRESSION_ASSIGNMENT ||
                                                     be->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT)
                                          ? (int)be->value.binary.operator_kind
                                          : be != NULL && be->kind == MINIC_EXPRESSION_UNARY
                                                ? (int)be->value.unary.operator_kind
                                                : -1,
                                      bs == NULL ? (size_t)MINIC_EXPRESSION_INVALID : (size_t)bs->target_expression,
                                      bs == NULL ? 0U : (size_t)bs->cleanup_context,
                                      bs == NULL ? 0U : (size_t)bs->cleanup_stop_context);
                        if (be != NULL && be->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
                            const MinicExpression *r = minic_c0_program_expression(program, be->value.binary.right);
                            const MinicExpression *u = r;
                            if (u != NULL && u->kind == MINIC_EXPRESSION_LVALUE_READ)
                                u = minic_c0_program_expression(program, u->value.unary.operand);
                            (void)fprintf(stderr,
                                          "SMALL_LOOP_RHS kind=%d unwrapped=%d left=%zu right=%zu\n",
                                          r == NULL ? -1 : (int)r->kind,
                                          u == NULL ? -1 : (int)u->kind,
                                          (size_t)be->value.binary.left,
                                          (size_t)be->value.binary.right);
                            if (u != NULL && u->kind == MINIC_EXPRESSION_SUBSCRIPT) {
                                const MinicExpression *base = minic_c0_program_expression(program, u->value.subscript.base);
                                const MinicExpression *idx = minic_c0_program_expression(program, u->value.subscript.index);
                                (void)fprintf(stderr,
                                              "SMALL_LOOP_SUB basekind=%d idxkind=%d base=%zu idx=%zu\n",
                                              base == NULL ? -1 : (int)base->kind,
                                              idx == NULL ? -1 : (int)idx->kind,
                                              (size_t)u->value.subscript.base,
                                              (size_t)u->value.subscript.index);
                            }
                        }
                    }
                }
            }
        }
    }
    if (body == NULL || body->statement_count < 3U || body->statement_count > 8U) {
        return false;
    }
    facts = (MinicCoreLocalIntegerConstant *)calloc(callee->local_count, sizeof(*facts));
    if (facts == NULL) {
        return false;
    }
    callee_context = *caller_context;
    callee_context.source_function = callee;
    callee_context.local_integer_constants = facts;
    callee_context.cfg_constant_call_depth = caller_context->cfg_constant_call_depth + 1U;
    saw_loop = false;

    for (statement_index = 0U; statement_index < body->statement_count; ++statement_index) {
        const MinicStatement *statement = minic_c0_program_statement(
            program, body->statements[statement_index]);
        if (statement == NULL ||
            statement->cleanup_context != statement->cleanup_stop_context) {
            free(facts);
            return false;
        }
        /* M195_SMALL_LOOP_FOR_INIT_EXPRESSION: parse_for() normalizes
           `for (i = 0; ...; i++)` by emitting the initializer as a top-level
           EXPRESSION statement immediately before the WHILE.  It is the same
           strict local-only assignment subset already accepted by the helper;
           admit it only before the one loop has been seen. */
        if (statement->kind == MINIC_STATEMENT_ASSIGN ||
            (!saw_loop && statement->kind == MINIC_STATEMENT_EXPRESSION)) {
            if (!core_cfg_small_loop_execute_statement(&callee_context, statement)) {
                free(facts);
                return false;
            }
            continue;
        }
        if (statement->kind == MINIC_STATEMENT_WHILE && !saw_loop &&
            statement->expression != MINIC_EXPRESSION_INVALID &&
            statement->then_block != MINIC_BLOCK_INVALID) {
            const MinicBlock *loop_body = minic_c0_program_block(program, statement->then_block);
            unsigned int iteration;
            if (loop_body == NULL || loop_body->statement_count == 0U ||
                loop_body->statement_count > 8U) {
                free(facts);
                return false;
            }
            saw_loop = true;
            for (iteration = 0U; iteration < 64U; ++iteration) {
                MinicConstValue condition;
                bool is_zero;
                size_t loop_index;
                if (!core_const_eval_integer_with_locals(
                        &callee_context, statement->expression, &condition) ||
                    !minic_const_value_is_zero(
                        program, caller_context->target, &condition, &is_zero)) {
                    free(facts);
                    return false;
                }
                if (is_zero) {
                    break;
                }
                for (loop_index = 0U; loop_index < loop_body->statement_count; ++loop_index) {
                    const MinicStatement *loop_statement = minic_c0_program_statement(
                        program, loop_body->statements[loop_index]);
                    if (!core_cfg_small_loop_execute_statement(
                            &callee_context, loop_statement)) {
                        free(facts);
                        return false;
                    }
                }
            }
            if (iteration == 64U) {
                free(facts);
                return false;
            }
            continue;
        }
        if (statement->kind == MINIC_STATEMENT_RETURN && saw_loop &&
            statement->expression != MINIC_EXPRESSION_INVALID) {
            MinicConstValue returned;
            bool success = core_const_eval_integer_with_locals(
                               &callee_context, statement->expression, &returned) &&
                           minic_const_value_convert_integer(program,
                                                             caller_context->target,
                                                             &returned,
                                                             call_expression->type,
                                                             value);
            free(facts);
            return success;
        }
        /* The frontend can append a synthetic return only after a real return;
           because we return immediately above, reaching any other top-level
           statement means this is outside the accepted tiny-loop subset. */
        free(facts);
        return false;
    }
    free(facts);
    return false;
}

/* A discarded IF arm is safe for the narrow fallthrough-return fold only
 * when it has no source-level reentry, labels, or inline ASM.  An unlabeled
 * RETURN in a branch proven not taken cannot execute its return expression;
 * admitting it lets CONFIG-disabled header helpers such as
 * `if (0) return q->limits.zoned; return BLK_ZONED_NONE;` fold without
 * reading the otherwise-live request_queue.  Preserve the original strict
 * rule for expression statements and all other control flow. */
static bool core_cfg_skipped_if_guard_block_safe(const MinicC0Program *program,
                                                 const MinicBlock *block) {
    const MinicStatement *statement;
    const MinicExpression *expression;

    if (block == NULL || block->statement_count == 0U) return true;
    if (block->statement_count != 1U) return false;
    statement = minic_c0_program_statement(program, block->statements[0]);
    if (statement == NULL ||
        statement->cleanup_context != statement->cleanup_stop_context ||
        statement->expression == MINIC_EXPRESSION_INVALID) return false;
    if (statement->kind == MINIC_STATEMENT_RETURN) return true;
    if (statement->kind != MINIC_STATEMENT_EXPRESSION) return false;
    expression = minic_c0_program_expression(program, statement->expression);
    return expression != NULL && expression->kind == MINIC_EXPRESSION_CALL &&
           expression->value.call.function_id != MINIC_FUNCTION_INVALID &&
           expression->value.call.argument_count == 0U;
}

/* Strictly interpret the common header predicate
 * `switch (pure_constant_helper(x)) { case N: return 1; default: return 0; }`.
 * No statements other than case/default labels and unconditional returns are
 * accepted; jumps, asm and side-effecting case segments stay untouched. */
static bool core_cfg_single_switch_constant_return(
    const MinicCoreLowerContext *context, const MinicFunction *callee,
    const MinicExpression *call_expression, const MinicBlock *body,
    MinicConstValue *value) {
    const MinicC0Program *program = context->body->program;
    const MinicStatement *top;
    const MinicBlock *cases;
    const MinicExpression *selector;
    MinicConstValue selector_constant;
    MinicConstValue selector_converted;
    MinicType selector_type;
    size_t selected = SIZE_MAX;
    size_t default_index = SIZE_MAX;
    size_t i;

    if (body->statement_count == 0U) return false;
    top = minic_c0_program_statement(program, body->statements[0]);
    if (top == NULL || top->kind != MINIC_STATEMENT_SWITCH ||
        top->cleanup_context != top->cleanup_stop_context ||
        top->expression == MINIC_EXPRESSION_INVALID ||
        top->then_block == MINIC_BLOCK_INVALID ||
        top->else_block != MINIC_BLOCK_INVALID) return false;
    selector = minic_c0_program_expression(program, top->expression);
    cases = minic_c0_program_block(program, top->then_block);
    if (selector == NULL || cases == NULL || cases->statement_count == 0U ||
        !minic_type_is_integer(selector->type) ||
        !minic_target_info_integer_promotion_for_program(
            context->target, program, selector->type, &selector_type) ||
        !minic_core_cfg_pure_call_argument(context, top->expression, 0U) ||
        !core_cfg_eval_callee_return_expression(context, callee, call_expression,
                                                top->expression, &selector_constant) ||
        !minic_const_value_convert_integer(program, context->target,
                                           &selector_constant, selector_type,
                                           &selector_converted)) return false;

    for (i = 0U; i < cases->statement_count; ++i) {
        const MinicStatement *item = minic_c0_program_statement(program, cases->statements[i]);
        if (item == NULL || item->cleanup_context != item->cleanup_stop_context)
            return false;
        if (item->kind == MINIC_STATEMENT_DEFAULT) {
            if (item->then_block != MINIC_BLOCK_INVALID ||
                item->else_block != MINIC_BLOCK_INVALID) return false;
            if (default_index != SIZE_MAX) return false;
            default_index = i;
        } else if (item->kind == MINIC_STATEMENT_CASE) {
            MinicConstValue case_constant;
            MinicConstValue case_converted;
            if (item->then_block != MINIC_BLOCK_INVALID ||
                item->else_block != MINIC_BLOCK_INVALID ||
                item->target_expression != MINIC_EXPRESSION_INVALID ||
                item->expression == MINIC_EXPRESSION_INVALID ||
                !minic_const_eval_integer(program, context->target,
                                          item->expression, &case_constant) ||
                !minic_const_value_convert_integer(program, context->target,
                                                   &case_constant, selector_type,
                                                   &case_converted)) return false;
            if (case_converted.bits == selector_converted.bits && selected == SIZE_MAX)
                selected = i;
        } else if (item->kind != MINIC_STATEMENT_RETURN ||
                   item->expression == MINIC_EXPRESSION_INVALID) {
            return false;
        }
    }
    if (selected == SIZE_MAX) selected = default_index;
    if (selected == SIZE_MAX) return false;
    for (i = selected + 1U; i < cases->statement_count; ++i) {
        const MinicStatement *item = minic_c0_program_statement(program, cases->statements[i]);
        MinicConstValue result;
        if (item == NULL) return false;
        if (item->kind == MINIC_STATEMENT_CASE ||
            item->kind == MINIC_STATEMENT_DEFAULT) continue;
        return item->kind == MINIC_STATEMENT_RETURN &&
               minic_core_cfg_pure_call_argument(context, item->expression, 0U) &&
               core_cfg_eval_callee_return_expression(
                   context, callee, call_expression, item->expression, &result) &&
               minic_const_value_convert_integer(
                   program, context->target, &result, call_expression->type, value);
    }
    return false;
}

bool minic_core_cfg_constant_inline_call(const MinicCoreLowerContext *context,
                                          const MinicExpression *expression,
                                          MinicConstValue *value) {
    const MinicC0Program *program;
    const MinicFunction *callee;
    const MinicBlock *body;
    const MinicStatement *statement;
    MinicConstValue returned;
    size_t argument_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || expression == NULL || value == NULL ||
        context->cfg_constant_call_depth >= 16U ||
        expression->kind != MINIC_EXPRESSION_CALL ||
        expression->value.call.function_id == MINIC_FUNCTION_INVALID ||
        !minic_type_is_integer(expression->type)) {
        return false;
    }
    program = context->body->program;
    callee = minic_c0_program_function(program, expression->value.call.function_id);
    if (callee == NULL || !callee->is_defined || !callee->is_internal ||
        !minic_type_is_integer(callee->return_type) || callee->body_block == MINIC_BLOCK_INVALID) {
        return false;
    }
    for (argument_index = 0U; argument_index < expression->value.call.argument_count;
         ++argument_index) {
        if (!minic_core_cfg_pure_call_argument(
                context, minic_c0_call_argument(expression, argument_index), 0U)) {
            return false;
        }
    }
    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count == 0U) {
        return false;
    }
    if (core_cfg_single_switch_constant_return(context, callee, expression, body, value)) {
        return true;
    }
    if (core_cfg_small_constant_loop_call(context, callee, expression, value)) {
        return true;
    }
    /* A leading compile-time if can make the function result independent of
       all later statements (common for CONFIG-disabled helpers). Follow only a
       selected one-return arm; otherwise fall back to the established strict
       direct-return evaluator below. */
    statement = minic_c0_program_statement(program, body->statements[0]);
    if (statement != NULL && statement->kind == MINIC_STATEMENT_IF &&
        statement->expression != MINIC_EXPRESSION_INVALID &&
        statement->cleanup_context == statement->cleanup_stop_context) {
        MinicConstValue condition;
        bool condition_is_zero;
        if (core_cfg_eval_callee_return_expression(context,
                                                   callee,
                                                   expression,
                                                   statement->expression,
                                                   &condition) &&
            minic_const_value_is_zero(program,
                                      context->target,
                                      &condition,
                                      &condition_is_zero)) {
            MinicBlockId selected_id = condition_is_zero
                                           ? statement->else_block
                                           : statement->then_block;
            if (selected_id != MINIC_BLOCK_INVALID) {
                const MinicBlock *selected = minic_c0_program_block(program, selected_id);
                if (selected != NULL && selected->statement_count == 1U) {
                    const MinicStatement *selected_statement = minic_c0_program_statement(
                        program, selected->statements[0]);
                    MinicConstValue selected_value;
                    if (selected_statement != NULL &&
                        selected_statement->kind == MINIC_STATEMENT_RETURN &&
                        selected_statement->expression != MINIC_EXPRESSION_INVALID &&
                        selected_statement->cleanup_context ==
                            selected_statement->cleanup_stop_context &&
                        core_cfg_eval_callee_return_expression(
                            context,
                            callee,
                            expression,
                            selected_statement->expression,
                            &selected_value) &&
                        minic_const_value_convert_integer(program,
                                                          context->target,
                                                          &selected_value,
                                                          expression->type,
                                                          value)) {
                        return true;
                    }
                }
            }
        }
    }
    /* A pure constant IF whose selected arm is empty simply falls through
     * to the next unconditional return.  This is common in small integer
     * helpers: invalid-input diagnostics live in the provably skipped arm,
     * while the return contains an otherwise foldable integer expression.
     * Do not evaluate arbitrary blocks or bypass labels/asm-goto here. */
    if (body->statement_count >= 2U && statement != NULL &&
        statement->kind == MINIC_STATEMENT_IF &&
        statement->expression != MINIC_EXPRESSION_INVALID &&
        statement->cleanup_context == statement->cleanup_stop_context &&
        minic_core_cfg_pure_call_argument(context, statement->expression, 0U)) {
        MinicConstValue condition;
        bool condition_is_zero;
        const MinicBlock *selected;
        const MinicBlock *skipped;
        const MinicStatement *next;

        if (core_cfg_eval_callee_return_expression(
                context, callee, expression, statement->expression, &condition) &&
            minic_const_value_is_zero(
                program, context->target, &condition, &condition_is_zero)) {
            MinicBlockId selected_id = condition_is_zero
                ? statement->else_block : statement->then_block;
            MinicBlockId skipped_id = condition_is_zero
                ? statement->then_block : statement->else_block;
            selected = minic_c0_program_block(program, selected_id);
            skipped = minic_c0_program_block(program, skipped_id);
            next = minic_c0_program_statement(program, body->statements[1]);
            if ((selected_id == MINIC_BLOCK_INVALID ||
                 (selected != NULL && selected->statement_count == 0U)) &&
                core_cfg_skipped_if_guard_block_safe(program, skipped) &&
                next != NULL && next->kind == MINIC_STATEMENT_RETURN &&
                next->expression != MINIC_EXPRESSION_INVALID &&
                next->cleanup_context == next->cleanup_stop_context &&
                minic_core_cfg_pure_call_argument(
                    context, next->expression, 0U) &&
                core_cfg_eval_callee_return_expression(
                    context, callee, expression, next->expression, &returned) &&
                minic_const_value_convert_integer(
                    program, context->target, &returned, expression->type, value)) {
                return true;
            }
        }
    }
    /* A top-level return terminates the function unconditionally.  The parser
       may append a synthetic fallback return after it; that statement is
       unreachable and must not be treated as an alternative return value.
       Only consume this narrow shape: the first top-level statement itself is
       a return with no outstanding cleanup.  Conditional/mixed bodies remain
       fail-closed and are handled by the separate CFG-aware helper paths. */
    statement = minic_c0_program_statement(program, body->statements[0]);
    if (statement == NULL || statement->kind != MINIC_STATEMENT_RETURN ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->cleanup_context != statement->cleanup_stop_context ||
                !core_cfg_eval_callee_return_expression(context,
                                                        callee,
                                                        expression,
                                                        statement->expression,
                                                        &returned)) {
        return false;
    }
    return minic_const_value_convert_integer(program,
                                             context->target,
                                             &returned,
                                             expression->type,
                                             value);
}


