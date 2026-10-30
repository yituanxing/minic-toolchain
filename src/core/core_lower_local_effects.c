#include "core/core_lower_internal.h"
#include "frontend/expression_semantics.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* One conservative source-effect owner for both scalar queries and batch
 * local-set collection. IF and LOOP consume a whole-subtree local mask once,
 * instead of recursively scanning that AST independently for every local.
 * Invalid nodes, unsupported source operations and depth exhaustion poison
 * the entire mask; absence of an effect must be positively proven. */
typedef struct CoreSourceLocalEffectScan {
    const MinicCoreLowerContext *context;
    unsigned char *modified;
    unsigned char *condition_owned;
    MinicLocalId local_begin;
    size_t local_count;
    unsigned int statement_expression_depth;
    bool all_modified;
} CoreSourceLocalEffectScan;

static void core_source_mark_statement_expression_locals(
    CoreSourceLocalEffectScan *scan, MinicBlockId block_id) {
    const MinicC0Program *program;
    size_t index;

    if (scan == NULL || scan->condition_owned == NULL ||
        scan->context == NULL || scan->context->body == NULL ||
        (program = scan->context->body->program) == NULL ||
        block_id == MINIC_BLOCK_INVALID || block_id >= program->block_count)
        return;
    for (index = 0U; index < scan->local_count; ++index) {
        const MinicLocal *local = minic_c0_program_local(
            program, scan->local_begin + index);
        if (local != NULL && local->has_declaration_block &&
            local->declaration_block == block_id)
            scan->condition_owned[index] = 1U;
    }
}

static void core_source_mark_all_modified(CoreSourceLocalEffectScan *scan) {
    if (scan == NULL || scan->all_modified) return;
    if (scan->modified != NULL && scan->local_count != 0U)
        (void)memset(scan->modified, 1, scan->local_count);
    scan->all_modified = true;
}

static void core_source_mark_local_modified(
    CoreSourceLocalEffectScan *scan, MinicLocalId id) {
    size_t index;
    if (scan == NULL || scan->all_modified || scan->modified == NULL ||
        id < scan->local_begin) return;
    index = (size_t)(id - scan->local_begin);
    if (index < scan->local_count) scan->modified[index] = 1U;
}

static bool core_source_direct_local_lvalue(
    const CoreSourceLocalEffectScan *scan,
    MinicExpressionId expression_id, MinicLocalId *local_id) {
    const MinicExpression *expression;
    if (scan == NULL || scan->context == NULL ||
        scan->context->body == NULL ||
        scan->context->body->program == NULL ||
        local_id == NULL || expression_id == MINIC_EXPRESSION_INVALID)
        return false;
    expression = minic_c0_program_expression(
        scan->context->body->program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
        expression->value_category != MINIC_VALUE_LVALUE)
        return false;
    *local_id = expression->value.local_id;
    return true;
}

static void core_source_collect_expression(
    CoreSourceLocalEffectScan *scan, MinicExpressionId id, unsigned int depth);
static void core_source_collect_block(
    CoreSourceLocalEffectScan *scan, const MinicBlock *block, unsigned int depth);

static void core_source_collect_block(
    CoreSourceLocalEffectScan *scan, const MinicBlock *block, unsigned int depth) {
    const MinicC0Program *program;
    size_t index;
    if (scan == NULL || scan->all_modified) return;
    if (scan->context == NULL || scan->context->body == NULL ||
        (program = scan->context->body->program) == NULL ||
        block == NULL || depth > 64U) {
        core_source_mark_all_modified(scan);
        return;
    }

    for (index = 0U; index < block->statement_count && !scan->all_modified; ++index) {
        const MinicStatement *statement = minic_c0_program_statement(
            program, block->statements[index]);
        const MinicBlock *child;
        MinicLocalId target;
        if (statement == NULL ||
            statement->cleanup_context != statement->cleanup_stop_context) {
            core_source_mark_all_modified(scan);
            break;
        }
        switch (statement->kind) {
        case MINIC_STATEMENT_ASSIGN:
        case MINIC_STATEMENT_RECORD_COPY:
        case MINIC_STATEMENT_RECORD_INITIALIZE:
            if (core_source_direct_local_lvalue(
                    scan, statement->target_expression, &target))
                core_source_mark_local_modified(scan, target);
            /* The lvalue address is executable: a[index++] = x writes index. */
            core_source_collect_expression(
                scan, statement->target_expression, depth + 1U);
            core_source_collect_expression(
                scan, statement->expression, depth + 1U);
            break;
        case MINIC_STATEMENT_EXPRESSION:
        case MINIC_STATEMENT_RETURN:
            if (statement->expression != MINIC_EXPRESSION_INVALID)
                core_source_collect_expression(
                    scan, statement->expression, depth + 1U);
            break;
        case MINIC_STATEMENT_IF:
        case MINIC_STATEMENT_WHILE:
        case MINIC_STATEMENT_SWITCH:
            if (statement->expression != MINIC_EXPRESSION_INVALID)
                core_source_collect_expression(
                    scan, statement->expression, depth + 1U);
            if (statement->then_block != MINIC_BLOCK_INVALID) {
                child = minic_c0_program_block(program, statement->then_block);
                if (scan->statement_expression_depth != 0U)
                    core_source_mark_statement_expression_locals(
                        scan, statement->then_block);
                core_source_collect_block(scan, child, depth + 1U);
            }
            if (statement->else_block != MINIC_BLOCK_INVALID) {
                child = minic_c0_program_block(program, statement->else_block);
                if (scan->statement_expression_depth != 0U)
                    core_source_mark_statement_expression_locals(
                        scan, statement->else_block);
                core_source_collect_block(scan, child, depth + 1U);
            }
            break;
        case MINIC_STATEMENT_INLINE_ASM:
            core_source_mark_all_modified(scan);
            break;
        case MINIC_STATEMENT_GOTO:
            if (statement->expression != MINIC_EXPRESSION_INVALID)
                core_source_collect_expression(
                    scan, statement->expression, depth + 1U);
            break;
        case MINIC_STATEMENT_CASE:
            if (statement->expression != MINIC_EXPRESSION_INVALID)
                core_source_collect_expression(
                    scan, statement->expression, depth + 1U);
            if (statement->target_expression != MINIC_EXPRESSION_INVALID)
                core_source_collect_expression(
                    scan, statement->target_expression, depth + 1U);
            break;
        case MINIC_STATEMENT_BREAK:
        case MINIC_STATEMENT_LABEL:
        case MINIC_STATEMENT_DEFAULT:
            break;
        default:
            core_source_mark_all_modified(scan);
            break;
        }
    }
}

static void core_source_collect_expression(
    CoreSourceLocalEffectScan *scan, MinicExpressionId id, unsigned int depth) {
    const MinicExpression *expression;
    const MinicC0Program *program;
    const MinicBlock *block;
    MinicLocalId target;
    size_t argument_index;
    if (scan == NULL || scan->all_modified) return;
    if (scan->context == NULL || scan->context->body == NULL ||
        (program = scan->context->body->program) == NULL ||
        id == MINIC_EXPRESSION_INVALID || depth > 64U ||
        (expression = minic_c0_program_expression(program, id)) == NULL) {
        core_source_mark_all_modified(scan);
        return;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
    case MINIC_EXPRESSION_FLOATING:
    case MINIC_EXPRESSION_LOCAL:
    case MINIC_EXPRESSION_GLOBAL_OBJECT:
    case MINIC_EXPRESSION_FIXED_REGISTER:
    case MINIC_EXPRESSION_FUNCTION:
    case MINIC_EXPRESSION_LABEL_ADDRESS:
    case MINIC_EXPRESSION_CALL_FRAME_ADDRESS:
    case MINIC_EXPRESSION_SIZEOF:
    case MINIC_EXPRESSION_OFFSETOF:
        return;
    case MINIC_EXPRESSION_ADDRESS_OF:
        if (core_source_direct_local_lvalue(
                scan, expression->value.unary.operand, &target))
            core_source_mark_local_modified(scan, target);
        core_source_collect_expression(
            scan, expression->value.unary.operand, depth + 1U);
        return;
    case MINIC_EXPRESSION_DEREFERENCE:
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_DISCARD:
    case MINIC_EXPRESSION_LVALUE_READ:
        core_source_collect_expression(
            scan, expression->value.unary.operand, depth + 1U);
        return;
    case MINIC_EXPRESSION_UNARY:
        if (expression->value.unary.operator_kind > MINIC_UNARY_BITWISE_NOT &&
            core_source_direct_local_lvalue(
                scan, expression->value.unary.operand, &target))
            core_source_mark_local_modified(scan, target);
        core_source_collect_expression(
            scan, expression->value.unary.operand, depth + 1U);
        return;
    case MINIC_EXPRESSION_ASSIGNMENT:
    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:
        if (core_source_direct_local_lvalue(
                scan, expression->value.binary.left, &target))
            core_source_mark_local_modified(scan, target);
        core_source_collect_expression(
            scan, expression->value.binary.left, depth + 1U);
        core_source_collect_expression(
            scan, expression->value.binary.right, depth + 1U);
        return;
    case MINIC_EXPRESSION_BINARY:
        core_source_collect_expression(
            scan, expression->value.binary.left, depth + 1U);
        core_source_collect_expression(
            scan, expression->value.binary.right, depth + 1U);
        return;
    case MINIC_EXPRESSION_CONDITIONAL:
        core_source_collect_expression(
            scan, expression->value.conditional.condition, depth + 1U);
        core_source_collect_expression(
            scan, expression->value.conditional.when_true, depth + 1U);
        core_source_collect_expression(
            scan, expression->value.conditional.when_false, depth + 1U);
        return;
    case MINIC_EXPRESSION_CALL:
        if (expression->value.call.callee != MINIC_EXPRESSION_INVALID)
            core_source_collect_expression(
                scan, expression->value.call.callee, depth + 1U);
        for (argument_index = 0U;
             argument_index < expression->value.call.argument_count &&
             !scan->all_modified; ++argument_index)
            core_source_collect_expression(
                scan, minic_c0_call_argument(expression, argument_index),
                depth + 1U);
        return;
    case MINIC_EXPRESSION_SUBSCRIPT:
        core_source_collect_expression(
            scan, expression->value.subscript.base, depth + 1U);
        core_source_collect_expression(
            scan, expression->value.subscript.index, depth + 1U);
        return;
    case MINIC_EXPRESSION_MEMBER:
        core_source_collect_expression(
            scan, expression->value.member.base, depth + 1U);
        return;
    case MINIC_EXPRESSION_STATEMENT:
        block = minic_c0_program_block(
            program, expression->value.statement_expression.block);
        core_source_mark_statement_expression_locals(
            scan, expression->value.statement_expression.block);
        ++scan->statement_expression_depth;
        core_source_collect_block(scan, block, depth + 1U);
        --scan->statement_expression_depth;
        /* GNU stmt-expression tail is detached from its source block. */
        if (expression->value.statement_expression.result != MINIC_EXPRESSION_INVALID)
            core_source_collect_expression(
                scan, expression->value.statement_expression.result,
                depth + 1U);
        return;
    case MINIC_EXPRESSION_BUILTIN_UNARY:
        /* __builtin_constant_p queries whether an operand is compile-time
         * known; the operand is not executed even if it contains ++, calls
         * or stores. Poisoning local facts for an unevaluated operand would
         * discard valid proofs in later source conditions. Other unary
         * builtins evaluate their operand and keep the normal effect scan. */
        if (expression->value.builtin_unary.operator_kind ==
            MINIC_BUILTIN_UNARY_CONSTANT_P)
            return;
        core_source_collect_expression(
            scan, expression->value.builtin_unary.operand, depth + 1U);
        return;
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW_P:
        core_source_collect_expression(
            scan, expression->value.overflow.left, depth + 1U);
        core_source_collect_expression(
            scan, expression->value.overflow.right, depth + 1U);
        if (expression->kind == MINIC_EXPRESSION_BUILTIN_OVERFLOW)
            core_source_collect_expression(
                scan, expression->value.overflow.result_pointer, depth + 1U);
        return;
    case MINIC_EXPRESSION_COMPOUND_LITERAL:
    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
    case MINIC_EXPRESSION_BUILTIN_ALLOCA:
    case MINIC_EXPRESSION_BUILTIN_VA_START:
    case MINIC_EXPRESSION_BUILTIN_VA_COPY:
    case MINIC_EXPRESSION_BUILTIN_VA_END:
    case MINIC_EXPRESSION_BUILTIN_VA_ARG:
    default:
        core_source_mark_all_modified(scan);
        return;
    }
}

static void core_source_initialize_effect_scan(
    CoreSourceLocalEffectScan *scan,
    const MinicCoreLowerContext *context,
    unsigned char *modified,
    MinicLocalId begin,
    size_t count) {
    (void)memset(scan, 0, sizeof(*scan));
    scan->context = context;
    scan->modified = modified;
    scan->local_begin = begin;
    scan->local_count = count;
}

void core_expression_collect_modified_locals(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned char *modified, unsigned int depth) {
    CoreSourceLocalEffectScan scan;
    if (context == NULL || context->source_function == NULL ||
        (context->source_function->local_count != 0U && modified == NULL)) return;
    core_source_initialize_effect_scan(
        &scan, context, modified, context->source_function->local_begin,
        context->source_function->local_count);
    core_source_collect_expression(&scan, id, depth);
}

void core_expression_collect_modified_and_condition_locals(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned char *modified, unsigned char *condition_owned,
    unsigned int depth) {
    CoreSourceLocalEffectScan scan;
    if (context == NULL || context->source_function == NULL ||
        (context->source_function->local_count != 0U &&
         (modified == NULL || condition_owned == NULL)))
        return;
    core_source_initialize_effect_scan(
        &scan, context, modified, context->source_function->local_begin,
        context->source_function->local_count);
    scan.condition_owned = condition_owned;
    core_source_collect_expression(&scan, id, depth);
    /* An unsupported source effect can modify even an otherwise scoped
     * temporary. No lifetime-based exemption is valid without a complete
     * scan, so unknown traversals fail closed on both outputs. */
    if (scan.all_modified && condition_owned != NULL && scan.local_count != 0U)
        (void)memset(condition_owned, 0, scan.local_count);
}

void core_block_collect_modified_locals(
    const MinicCoreLowerContext *context, const MinicBlock *block,
    unsigned char *modified, unsigned int depth) {
    CoreSourceLocalEffectScan scan;
    if (context == NULL || context->source_function == NULL ||
        (context->source_function->local_count != 0U && modified == NULL)) return;
    core_source_initialize_effect_scan(
        &scan, context, modified, context->source_function->local_begin,
        context->source_function->local_count);
    core_source_collect_block(&scan, block, depth);
}

bool core_expression_may_modify_local(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    MinicLocalId local_id, unsigned int depth) {
    CoreSourceLocalEffectScan scan;
    unsigned char modified = 0U;
    if (context == NULL || context->body == NULL ||
        context->body->program == NULL) return true;
    core_source_initialize_effect_scan(&scan, context, &modified, local_id, 1U);
    core_source_collect_expression(&scan, id, depth);
    return modified != 0U;
}

bool core_block_may_modify_local(
    const MinicCoreLowerContext *context, const MinicBlock *block,
    MinicLocalId local_id, unsigned int depth) {
    CoreSourceLocalEffectScan scan;
    unsigned char modified = 0U;
    if (context == NULL || context->body == NULL ||
        context->body->program == NULL) return true;
    core_source_initialize_effect_scan(&scan, context, &modified, local_id, 1U);
    core_source_collect_block(&scan, block, depth);
    return modified != 0U;
}
