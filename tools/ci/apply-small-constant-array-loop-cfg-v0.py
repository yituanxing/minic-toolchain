#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M193_SMALL_CONSTANT_ARRAY_LOOP_CFG"
if marker in text:
    print("MINIC_SMALL_CONSTANT_ARRAY_LOOP_CFG_V0=ALREADY")
    raise SystemExit(0)

# First let the existing local-aware evaluator consume one element of an
# internal readonly scalar array when its index is itself proven constant.
fn = '''static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,
                                                MinicExpressionId expression_id,
                                                MinicConstValue *value) {
'''
start = text.find(fn)
if start < 0:
    raise SystemExit("small-loop: local evaluator definition missing")
anchor = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
'''
pos = text.find(anchor, start)
if pos < 0:
    raise SystemExit("small-loop: evaluator validation anchor missing")
array_case = anchor + r'''    {
        const MinicExpression *read = expression;
        const MinicExpression *subscript;
        const MinicExpression *base;
        const MinicGlobalObject *object;
        MinicConstValue index_value;
        MinicConstValue element_value;
        MinicExpressionId base_id;
        unsigned int unwrap_depth;

        if (read->kind == MINIC_EXPRESSION_LVALUE_READ) {
            read = minic_c0_program_expression(
                context->body->program, read->value.unary.operand);
        }
        if (read != NULL && read->kind == MINIC_EXPRESSION_SUBSCRIPT) {
            subscript = read;
            base_id = subscript->value.subscript.base;
            base = minic_c0_program_expression(context->body->program, base_id);
            for (unwrap_depth = 0U; base != NULL && unwrap_depth < 8U; ++unwrap_depth) {
                if (base->kind == MINIC_EXPRESSION_CAST ||
                    base->kind == MINIC_EXPRESSION_BITCAST ||
                    base->kind == MINIC_EXPRESSION_CONVERSION ||
                    base->kind == MINIC_EXPRESSION_LVALUE_READ ||
                    base->kind == MINIC_EXPRESSION_ADDRESS_OF) {
                    base_id = base->value.unary.operand;
                    base = minic_c0_program_expression(context->body->program, base_id);
                    continue;
                }
                break;
            }
            if (base != NULL && base->kind == MINIC_EXPRESSION_GLOBAL_OBJECT &&
                core_const_eval_integer_with_locals(
                    context, subscript->value.subscript.index, &index_value)) {
                object = minic_c0_program_global_object(
                    context->body->program, base->value.global_object_id);
                if (object != NULL && object->is_internal && object->is_read_only &&
                    !object->is_extern && object->relocation_count == 0U &&
                    index_value.bits < object->initializer_count) {
                    element_value.type = subscript->type;
                    element_value.bits = object->initializer_values[index_value.bits];
                    if (minic_const_value_convert_integer(context->body->program,
                                                          context->target,
                                                          &element_value,
                                                          expression->type,
                                                          value)) {
                        return true;
                    }
                }
            }
        }
    }
'''
text = text[:pos] + array_case + text[pos + len(anchor):]

# Add a deliberately tiny interpreter before the generic constant-inline-call
# fallback.  It accepts only the straight-line + one bounded-loop subset used
# by common constexpr-style C helpers; everything else fails closed.
call_anchor = '''static bool core_cfg_constant_inline_call(const MinicCoreLowerContext *context,
                                          const MinicExpression *expression,
                                          MinicConstValue *value) {
'''
call_pos = text.find(call_anchor)
if call_pos < 0:
    raise SystemExit("small-loop: constant inline call definition missing")
helper = r'''
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
        core_local_constant_get(context, local_id, &left) &&
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
        return core_local_constant_get(context, local_id, &left);
    }
    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT &&
        core_cfg_small_loop_target_local(
            context, expression->value.binary.left, &local_id) &&
        core_const_eval_integer_with_locals(
            context, expression->value.binary.right, &result)) {
        core_local_constant_set(context, local_id, &result);
        return core_local_constant_get(context, local_id, &left);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) &&
        core_cfg_small_loop_target_local(
            context, expression->value.unary.operand, &local_id) &&
        core_local_constant_get(context, local_id, &left)) {
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
        return core_local_constant_get(context, local_id, &left);
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
        return core_local_constant_get(context, local_id, &check);
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
        if (statement->kind == MINIC_STATEMENT_ASSIGN) {
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

'''
text = text[:call_pos] + helper + text[call_pos:]

# Invoke the tiny interpreter after the ordinary callee/argument purity gates
# have succeeded and the function body is known nonempty, before narrower
# leading-if/direct-return fallbacks.
body_anchor = '''    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count == 0U) {
        return false;
    }
'''
call_start = text.find(call_anchor)
insert_pos = text.find(body_anchor, call_start)
if insert_pos < 0:
    raise SystemExit("small-loop: body validation anchor missing")
replacement = body_anchor + '''    if (core_cfg_small_constant_loop_call(context, callee, expression, value)) {
        return true;
    }
'''
text = text[:insert_pos] + replacement + text[insert_pos + len(body_anchor):]

p.write_text(text)
print("MINIC_SMALL_CONSTANT_ARRAY_LOOP_CFG_V0=APPLIED max_iterations=64 readonly_array=1")
