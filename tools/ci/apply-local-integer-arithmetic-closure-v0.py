#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

fn_anchor = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
helper = r'''
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
if text.count(fn_anchor) != 1:
    raise SystemExit(f"expected one local evaluator, found {text.count(fn_anchor)}")
text = text.replace(fn_anchor, helper + fn_anchor, 1)
start = text.find(fn_anchor)
if start < 0:
    raise SystemExit("local evaluator missing")

comparison_anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
'''
pos = text.find(comparison_anchor, start)
if pos < 0:
    raise SystemExit("comparison anchor missing in local evaluator")
unary = r'''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
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
            !core_cfg_normalize_integer(context, expression->type, operand.bits, &bits)) {
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
'''
text = text[:pos] + unary + text[pos:]

add_anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND)) {
'''
pos = text.find(add_anchor, start)
if pos < 0:
    raise SystemExit("add/and anchor missing in local evaluator")
extra = r'''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
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
            !core_const_eval_integer_with_locals(context,
                                                expression->value.binary.left,
                                                &left) ||
            !core_const_eval_integer_with_locals(context,
                                                expression->value.binary.right,
                                                &right) ||
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
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_SUBTRACT:
            result_bits = left_common.bits - right_common.bits;
            break;
        case MINIC_BINARY_MULTIPLY:
            result_bits = left_common.bits * right_common.bits;
            break;
        case MINIC_BINARY_DIVIDE:
        case MINIC_BINARY_REMAINDER:
            if (minic_type_is_signed_integer(common_type)) {
                int64_t ls;
                int64_t rs;
                int64_t minimum;
                if (!core_const_signed_value(context, &left_common, &ls) ||
                    !core_const_signed_value(context, &right_common, &rs) || rs == 0) {
                    return false;
                }
                minimum = width == 64U ? INT64_MIN : -(INT64_C(1) << (width - 1U));
                if (ls == minimum && rs == -1) {
                    return false;
                }
                result_bits = (uint64_t)(
                    expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE
                        ? ls / rs
                        : ls % rs);
            } else {
                if (right_common.bits == 0U) {
                    return false;
                }
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
                int64_t ls;
                if (!core_const_signed_value(context, &left_common, &ls)) {
                    return false;
                }
                result_bits = (uint64_t)(ls >> (unsigned int)shift);
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
'''
text = text[:pos] + extra + text[pos:]

p.write_text(text)
print("MINIC_LOCAL_INTEGER_ARITHMETIC_CLOSURE_V0=APPLIED")
