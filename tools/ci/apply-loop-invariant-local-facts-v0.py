#!/usr/bin/env python3
from pathlib import Path

# Preserve an already-proven local integer fact across a loop only when the
# local is non-escaped and a conservative AST walk proves the loop condition,
# body, update, nested statements, and cleanup edges do not modify or expose
# that local. Unknown/side-effecting shapes fail closed. This is a CFG fact
# optimization only; runtime lowering is unchanged.
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M190_LOOP_INVARIANT_LOCAL_FACTS"
if marker in text:
    print("MINIC_LOOP_INVARIANT_LOCAL_FACTS_V0=ALREADY")
else:
    lower_block_anchor = "static MinicCoreLowerStatus\nlower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {\n"
    pos = text.find(lower_block_anchor)
    if pos < 0:
        raise SystemExit("lower_block definition missing")

    helper = r'''
/* M190_LOOP_INVARIANT_LOCAL_FACTS */
static bool core_expression_may_modify_local(const MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicLocalId local_id,
                                             unsigned int depth);

static bool core_direct_local_lvalue(const MinicCoreLowerContext *context,
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

static bool core_block_may_modify_local(const MinicCoreLowerContext *context,
                                        const MinicBlock *block,
                                        MinicLocalId local_id,
                                        unsigned int depth) {
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL || depth > 64U) {
        return true;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement = minic_c0_program_statement(
            context->body->program, block->statements[index]);
        MinicLocalId target_local;
        const MinicBlock *child;

        if (statement == NULL ||
            statement->cleanup_context != statement->cleanup_stop_context) {
            return true;
        }
        switch (statement->kind) {
        case MINIC_STATEMENT_ASSIGN:
            if (core_direct_local_lvalue(context, statement->target_expression, &target_local) &&
                target_local == local_id) {
                return true;
            }
            if (core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_RECORD_COPY:
        case MINIC_STATEMENT_RECORD_INITIALIZE:
            if (core_direct_local_lvalue(context, statement->target_expression, &target_local) &&
                target_local == local_id) {
                return true;
            }
            if (core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_EXPRESSION:
        case MINIC_STATEMENT_RETURN:
            if (statement->expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_IF:
        case MINIC_STATEMENT_WHILE:
        case MINIC_STATEMENT_SWITCH:
            if (statement->expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            if (statement->then_block != MINIC_BLOCK_INVALID) {
                child = minic_c0_program_block(context->body->program, statement->then_block);
                if (child == NULL || core_block_may_modify_local(
                                         context, child, local_id, depth + 1U)) {
                    return true;
                }
            }
            if (statement->else_block != MINIC_BLOCK_INVALID) {
                child = minic_c0_program_block(context->body->program, statement->else_block);
                if (child == NULL || core_block_may_modify_local(
                                         context, child, local_id, depth + 1U)) {
                    return true;
                }
            }
            break;
        case MINIC_STATEMENT_INLINE_ASM:
            return true;
        case MINIC_STATEMENT_GOTO:
            if (statement->expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_CASE:
            if (statement->expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->expression, local_id, depth + 1U)) {
                return true;
            }
            if (statement->target_expression != MINIC_EXPRESSION_INVALID &&
                core_expression_may_modify_local(
                    context, statement->target_expression, local_id, depth + 1U)) {
                return true;
            }
            break;
        case MINIC_STATEMENT_BREAK:
        case MINIC_STATEMENT_LABEL:
        case MINIC_STATEMENT_DEFAULT:
            break;
        default:
            return true;
        }
    }
    return false;
}

static bool core_expression_may_modify_local(const MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicLocalId local_id,
                                             unsigned int depth) {
    const MinicExpression *expression;
    MinicLocalId target_local;
    size_t argument_index;
    const MinicBlock *block;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        expression_id == MINIC_EXPRESSION_INVALID || depth > 64U) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL) {
        return true;
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
        return false;
    case MINIC_EXPRESSION_ADDRESS_OF:
        if (core_direct_local_lvalue(context, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return true;
        }
        return core_expression_may_modify_local(
            context, expression->value.unary.operand, local_id, depth + 1U);
    case MINIC_EXPRESSION_DEREFERENCE:
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_DISCARD:
    case MINIC_EXPRESSION_LVALUE_READ:
        return core_expression_may_modify_local(
            context, expression->value.unary.operand, local_id, depth + 1U);
    case MINIC_EXPRESSION_UNARY:
        if (expression->value.unary.operator_kind > MINIC_UNARY_BITWISE_NOT &&
            core_direct_local_lvalue(
                context, expression->value.unary.operand, &target_local) &&
            target_local == local_id) {
            return true;
        }
        return core_expression_may_modify_local(
            context, expression->value.unary.operand, local_id, depth + 1U);
    case MINIC_EXPRESSION_ASSIGNMENT:
    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT:
        if (core_direct_local_lvalue(
                context, expression->value.binary.left, &target_local) &&
            target_local == local_id) {
            return true;
        }
        return core_expression_may_modify_local(
                   context, expression->value.binary.left, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.binary.right, local_id, depth + 1U);
    case MINIC_EXPRESSION_BINARY:
        return core_expression_may_modify_local(
                   context, expression->value.binary.left, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.binary.right, local_id, depth + 1U);
    case MINIC_EXPRESSION_CONDITIONAL:
        return core_expression_may_modify_local(
                   context, expression->value.conditional.condition, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.conditional.when_true, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.conditional.when_false, local_id, depth + 1U);
    case MINIC_EXPRESSION_CALL:
        if (expression->value.call.callee != MINIC_EXPRESSION_INVALID &&
            core_expression_may_modify_local(
                context, expression->value.call.callee, local_id, depth + 1U)) {
            return true;
        }
        for (argument_index = 0U;
             argument_index < expression->value.call.argument_count;
             ++argument_index) {
            if (core_expression_may_modify_local(
                    context,
                    expression->value.call.arguments[argument_index],
                    local_id,
                    depth + 1U)) {
                return true;
            }
        }
        return false;
    case MINIC_EXPRESSION_SUBSCRIPT:
        return core_expression_may_modify_local(
                   context, expression->value.subscript.base, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.subscript.index, local_id, depth + 1U);
    case MINIC_EXPRESSION_MEMBER:
        return core_expression_may_modify_local(
            context, expression->value.member.base, local_id, depth + 1U);
    case MINIC_EXPRESSION_STATEMENT:
        block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        return block == NULL ||
               core_block_may_modify_local(context, block, local_id, depth + 1U);
    case MINIC_EXPRESSION_BUILTIN_UNARY:
        return core_expression_may_modify_local(
            context, expression->value.builtin_unary.operand, local_id, depth + 1U);
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
        return core_expression_may_modify_local(
                   context, expression->value.overflow.left, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.overflow.right, local_id, depth + 1U) ||
               core_expression_may_modify_local(
                   context, expression->value.overflow.result_pointer, local_id, depth + 1U);
    case MINIC_EXPRESSION_COMPOUND_LITERAL:
    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
    case MINIC_EXPRESSION_BUILTIN_ALLOCA:
    case MINIC_EXPRESSION_BUILTIN_VA_START:
    case MINIC_EXPRESSION_BUILTIN_VA_COPY:
    case MINIC_EXPRESSION_BUILTIN_VA_END:
    case MINIC_EXPRESSION_BUILTIN_VA_ARG:
    default:
        return true;
    }
}

static void core_restore_loop_invariant_facts(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *incoming,
    const MinicStatement *loop_statement) {
    const MinicBlock *body;
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || incoming == NULL ||
        loop_statement == NULL || loop_statement->kind != MINIC_STATEMENT_WHILE) {
        return;
    }
    body = minic_c0_program_block(context->body->program, loop_statement->then_block);
    if (body == NULL) {
        return;
    }
    for (index = 0U; index < context->source_function->local_count; ++index) {
        MinicLocalId local_id = context->source_function->local_begin + index;
        if (!incoming[index].known || incoming[index].escaped ||
            context->local_integer_constants[index].escaped ||
            (loop_statement->expression != MINIC_EXPRESSION_INVALID &&
             core_expression_may_modify_local(
                 context, loop_statement->expression, local_id, 0U)) ||
            core_block_may_modify_local(context, body, local_id, 0U)) {
            continue;
        }
        context->local_integer_constants[index] = incoming[index];
    }
}

'''
    text = text[:pos] + helper + text[pos:]

    old = '''            case MINIC_STATEMENT_WHILE: {
                const MinicBlock *local_fact_loop_body;
                MinicBlock local_fact_single_iteration_body;
                MinicStatementId local_fact_continue_label;
                bool local_fact_single_pass;
'''
    new = '''            case MINIC_STATEMENT_WHILE: {
                const MinicBlock *local_fact_loop_body;
                MinicBlock local_fact_single_iteration_body;
                MinicStatementId local_fact_continue_label;
                MinicCoreLocalIntegerConstant *loop_incoming_facts;
                bool local_fact_single_pass;

                loop_incoming_facts = core_local_constants_snapshot(context);
                if (context->source_function != NULL &&
                    context->source_function->local_count != 0U &&
                    loop_incoming_facts == NULL) {
                    return MINIC_CORE_LOWER_ERROR;
                }
'''
    if text.count(old) != 1:
        raise SystemExit(f"while fact declaration anchor: expected one, found {text.count(old)}")
    text = text.replace(old, new, 1)

    old = '''                if (!local_fact_single_pass) {
                    core_local_constants_clear_known(context);
                }
                status = lower_while(
                    context, statement, MINIC_STATEMENT_INVALID, &statement_terminated);
                if (!local_fact_single_pass) {
                    core_local_constants_clear_known(context);
                }
                break;
'''
    new = '''                if (!local_fact_single_pass) {
                    core_local_constants_clear_known(context);
                    core_restore_loop_invariant_facts(
                        context, loop_incoming_facts, statement);
                }
                status = lower_while(
                    context, statement, MINIC_STATEMENT_INVALID, &statement_terminated);
                if (!local_fact_single_pass) {
                    core_local_constants_clear_known(context);
                    core_restore_loop_invariant_facts(
                        context, loop_incoming_facts, statement);
                }
                free(loop_incoming_facts);
                break;
'''
    if text.count(old) != 1:
        raise SystemExit(f"while fact barrier anchor: expected one, found {text.count(old)}")
    text = text.replace(old, new, 1)

    p.write_text(text)
    print("MINIC_LOOP_INVARIANT_LOCAL_FACTS_V0=APPLIED conservative_ast=1")
