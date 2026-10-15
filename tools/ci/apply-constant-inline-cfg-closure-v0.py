#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

helper_anchor = '''static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,
'''
helpers = r'''
/* LINUX_CONSTANT_INLINE_CFG_CLOSURE_V0
 *
 * Linux feature-disabled headers frequently expose a static-inline predicate
 * whose body is a single constant return (for example `return false`).  A call
 * to such a predicate is a constant expression for CFG purposes as long as
 * evaluating its arguments has no side effects.  This is deliberately a
 * reachability fact only; ordinary expression lowering is unchanged. */
static bool core_cfg_pure_call_argument(const MinicCoreLowerContext *context,
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
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_ADDRESS_OF:
    case MINIC_EXPRESSION_LVALUE_READ:
        return core_cfg_pure_call_argument(
            context, expression->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_UNARY:
        return expression->value.unary.operator_kind <= MINIC_UNARY_BITWISE_NOT &&
               core_cfg_pure_call_argument(
                   context, expression->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_MEMBER:
        return core_cfg_pure_call_argument(
            context, expression->value.member.base, depth + 1U);
    case MINIC_EXPRESSION_SUBSCRIPT:
        return core_cfg_pure_call_argument(context,
                                           expression->value.subscript.base,
                                           depth + 1U) &&
               core_cfg_pure_call_argument(context,
                                           expression->value.subscript.index,
                                           depth + 1U);
    case MINIC_EXPRESSION_BINARY:
        return expression->value.binary.operator_kind != MINIC_BINARY_COMMA &&
               core_cfg_pure_call_argument(context,
                                           expression->value.binary.left,
                                           depth + 1U) &&
               core_cfg_pure_call_argument(context,
                                           expression->value.binary.right,
                                           depth + 1U);
    case MINIC_EXPRESSION_CONDITIONAL:
        return core_cfg_pure_call_argument(context,
                                           expression->value.conditional.condition,
                                           depth + 1U) &&
               core_cfg_pure_call_argument(context,
                                           expression->value.conditional.when_true,
                                           depth + 1U) &&
               core_cfg_pure_call_argument(context,
                                           expression->value.conditional.when_false,
                                           depth + 1U);
    default:
        return false;
    }
}

static bool core_cfg_constant_inline_call(const MinicCoreLowerContext *context,
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
        expression->kind != MINIC_EXPRESSION_CALL ||
        expression->value.call.function_id == MINIC_FUNCTION_INVALID ||
        !minic_type_is_integer(expression->type)) {
        return false;
    }
    program = context->body->program;
    callee = minic_c0_program_function(program, expression->value.call.function_id);
    if (callee == NULL || !callee->is_defined || !callee->is_internal || !callee->is_inline ||
        !minic_type_is_integer(callee->return_type) ||
        callee->body_block == MINIC_BLOCK_INVALID) {
        return false;
    }
    for (argument_index = 0U; argument_index < expression->value.call.argument_count;
         ++argument_index) {
        if (!core_cfg_pure_call_argument(
                context, expression->value.call.arguments[argument_index], 0U)) {
            return false;
        }
    }
    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count != 1U) {
        return false;
    }
    statement = minic_c0_program_statement(program, body->statements[0]);
    if (statement == NULL || statement->kind != MINIC_STATEMENT_RETURN ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->cleanup_context != statement->cleanup_stop_context ||
        !minic_const_eval_integer(program,
                                 context->target,
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

static bool core_cfg_normalize_integer(const MinicCoreLowerContext *context,
                                       MinicType type,
                                       uint64_t bits,
                                       uint64_t *normalized) {
    unsigned int width;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || normalized == NULL ||
        !minic_target_info_integer_width(
            context->target, context->body->program, type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    if (minic_type_is_bool_integer(type)) {
        *normalized = bits == 0U ? 0U : 1U;
    } else if (width == 64U) {
        *normalized = bits;
    } else {
        *normalized = bits & ((UINT64_C(1) << width) - UINT64_C(1));
    }
    return true;
}

'''
if text.count(helper_anchor) != 1:
    raise SystemExit(f"expected one local integer evaluator anchor, found {text.count(helper_anchor)}")
text = text.replace(helper_anchor, helpers + helper_anchor, 1)

call_anchor = '''    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
'''
call_insert = '''    if (expression->kind == MINIC_EXPRESSION_CALL &&
        core_cfg_constant_inline_call(context, expression, value)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
'''
if text.count(call_anchor) != 1:
    raise SystemExit(f"expected one evaluator cast anchor, found {text.count(call_anchor)}")
text = text.replace(call_anchor, call_insert, 1)

unary_anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
'''
unary_insert = '''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_PLUS ||
         expression->value.unary.operator_kind == MINIC_UNARY_NEGATE ||
         expression->value.unary.operator_kind == MINIC_UNARY_BITWISE_NOT)) {
        MinicConstValue operand;
        uint64_t bits;

        if (!core_const_eval_integer_with_locals(
                context, expression->value.unary.operand, &operand) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &operand,
                                               expression->type,
                                               &operand) ||
            !core_cfg_normalize_integer(context,
                                        expression->type,
                                        operand.bits,
                                        &bits)) {
            return false;
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) {
            if (minic_type_is_signed_integer(expression->type)) {
                int64_t signed_value;
                unsigned int width;
                if (!core_const_signed_value(context, &operand, &signed_value) ||
                    !minic_target_info_integer_width(context->target,
                                                     context->body->program,
                                                     expression->type,
                                                     &width) ||
                    (width == 64U && signed_value == INT64_MIN) ||
                    (width < 64U && signed_value == -(INT64_C(1) << (width - 1U)))) {
                    return false;
                }
            }
            bits = UINT64_C(0) - bits;
        } else if (expression->value.unary.operator_kind == MINIC_UNARY_BITWISE_NOT) {
            bits = ~bits;
        }
        value->type = expression->type;
        return core_cfg_normalize_integer(context, expression->type, bits, &value->bits);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
'''
if text.count(unary_anchor) != 1:
    raise SystemExit(f"expected one comparison anchor, found {text.count(unary_anchor)}")
text = text.replace(unary_anchor, unary_insert, 1)

binary_anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND)) {
'''
binary_extra = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE ||
         expression->value.binary.operator_kind == MINIC_BINARY_REMAINDER ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicConstValue left;
        MinicConstValue right;
        MinicConstValue left_common;
        MinicConstValue right_common;
        MinicType common_type;
        uint64_t result_bits;
        unsigned int width;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &left) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &right) ||
            !minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          left_expression->type,
                                                          right_expression->type,
                                                          &common_type) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &left,
                                               common_type,
                                               &left_common) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &right,
                                               common_type,
                                               &right_common) ||
            !minic_target_info_integer_width(context->target,
                                             context->body->program,
                                             common_type,
                                             &width) ||
            width == 0U || width > 64U) {
            return false;
        }
        result_bits = 0U;
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_SUBTRACT:
            result_bits = left_common.bits - right_common.bits;
            break;
        case MINIC_BINARY_MULTIPLY:
            result_bits = left_common.bits * right_common.bits;
            break;
        case MINIC_BINARY_DIVIDE:
        case MINIC_BINARY_REMAINDER:
            if (right_common.bits == 0U) {
                return false;
            }
            if (minic_type_is_signed_integer(common_type)) {
                int64_t left_signed;
                int64_t right_signed;
                if (!core_const_signed_value(context, &left_common, &left_signed) ||
                    !core_const_signed_value(context, &right_common, &right_signed) ||
                    right_signed == 0 ||
                    (left_signed == INT64_MIN && right_signed == -1)) {
                    return false;
                }
                result_bits = (uint64_t)(
                    expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE
                        ? left_signed / right_signed
                        : left_signed % right_signed);
            } else {
                result_bits = expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE
                                  ? left_common.bits / right_common.bits
                                  : left_common.bits % right_common.bits;
            }
            break;
        case MINIC_BINARY_SHIFT_LEFT:
        case MINIC_BINARY_SHIFT_RIGHT: {
            uint64_t shift = right_common.bits;
            if (shift >= width) {
                return false;
            }
            if (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT) {
                result_bits = left_common.bits << (unsigned int)shift;
            } else if (minic_type_is_signed_integer(common_type)) {
                int64_t signed_left;
                if (!core_const_signed_value(context, &left_common, &signed_left)) {
                    return false;
                }
                result_bits = (uint64_t)(signed_left >> (unsigned int)shift);
            } else {
                result_bits = left_common.bits >> (unsigned int)shift;
            }
            break;
        }
        case MINIC_BINARY_BITWISE_XOR:
            result_bits = left_common.bits ^ right_common.bits;
            break;
        case MINIC_BINARY_BITWISE_OR:
            result_bits = left_common.bits | right_common.bits;
            break;
        default:
            return false;
        }
        left_common.type = common_type;
        if (!core_cfg_normalize_integer(context,
                                        common_type,
                                        result_bits,
                                        &left_common.bits)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &left_common,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND)) {
'''
if text.count(binary_anchor) != 1:
    raise SystemExit(f"expected one add/and anchor, found {text.count(binary_anchor)}")
text = text.replace(binary_anchor, binary_extra, 1)

condition_anchor = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL ||
        (!minic_type_is_integer(expression->type) && !minic_type_is_pointer(expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
condition_insert = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL ||
        (!minic_type_is_integer(expression->type) && !minic_type_is_pointer(expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (minic_type_is_integer(expression->type)) {
        MinicConstValue condition_constant;
        bool condition_is_zero;
        if (core_const_eval_integer_with_locals(
                context, expression_id, &condition_constant) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &condition_constant,
                                      &condition_is_zero)) {
            return set_branch(context,
                              context->block_id,
                              span,
                              condition_is_zero ? when_false : when_true);
        }
    }
'''
if text.count(condition_anchor) != 1:
    raise SystemExit(f"expected one condition validation anchor, found {text.count(condition_anchor)}")
text = text.replace(condition_anchor, condition_insert, 1)

p.write_text(text)
print("MINIC_CONSTANT_INLINE_CFG_CLOSURE_V0=APPLIED")
