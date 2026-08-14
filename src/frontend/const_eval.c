#include "frontend/const_eval.h"

#include <limits.h>
#include <string.h>

#define MINIC_CONST_EVAL_MAX_DEPTH 256U

static bool integer_width(const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          MinicType type,
                          unsigned int *width) {
    return minic_target_info_integer_width(target, program, type, width) && *width <= 64U;
}

static uint64_t width_mask(unsigned int width) {
    return width == 64U ? UINT64_MAX : (UINT64_C(1) << width) - UINT64_C(1);
}

static bool normalize_bits(const MinicC0Program *program,
                           const MinicTargetInfo *target,
                           MinicType type,
                           uint64_t bits,
                           uint64_t *normalized) {
    unsigned int width;

    if (normalized == NULL || !integer_width(program, target, type, &width)) {
        return false;
    }
    if (minic_type_is_bool_integer(type)) {
        *normalized = bits == 0U ? 0U : 1U;
        return true;
    }
    *normalized = bits & width_mask(width);
    return true;
}

static bool value_signed(const MinicC0Program *program,
                         const MinicTargetInfo *target,
                         const MinicConstValue *value,
                         int64_t *result) {
    unsigned int width;
    uint64_t bits;

    if (value == NULL || result == NULL || !minic_type_is_signed_integer(value->type) ||
        !integer_width(program, target, value->type, &width) ||
        !normalize_bits(program, target, value->type, value->bits, &bits)) {
        return false;
    }
    if (width < 64U && (bits & (UINT64_C(1) << (width - 1U))) != 0U) {
        bits |= ~width_mask(width);
    }
    (void)memcpy(result, &bits, sizeof(*result));
    return true;
}

static bool convert_value(const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicConstValue *source,
                          MinicType type,
                          MinicConstValue *result) {
    uint64_t bits;

    if (source == NULL || result == NULL || !minic_type_is_integer(source->type) ||
        !minic_type_is_integer(type)) {
        return false;
    }
    if (minic_type_is_bool_integer(type)) {
        result->type = type;
        result->bits = source->bits == 0U ? 0U : 1U;
        return true;
    }
    if (minic_type_is_signed_integer(source->type)) {
        int64_t signed_value;

        if (!value_signed(program, target, source, &signed_value)) {
            return false;
        }
        bits = (uint64_t)signed_value;
    } else if (!normalize_bits(program, target, source->type, source->bits, &bits)) {
        return false;
    }
    result->type = type;
    return normalize_bits(program, target, type, bits, &result->bits);
}

bool minic_const_value_convert_integer(const MinicC0Program *program,
                                       const MinicTargetInfo *target,
                                       const MinicConstValue *source,
                                       MinicType type,
                                       MinicConstValue *result) {
    return convert_value(program, target, source, type, result);
}

static bool value_truthy(const MinicC0Program *program,
                         const MinicTargetInfo *target,
                         const MinicConstValue *value,
                         bool *truthy) {
    uint64_t bits;

    if (truthy == NULL || value == NULL ||
        !normalize_bits(program, target, value->type, value->bits, &bits)) {
        return false;
    }
    *truthy = bits != 0U;
    return true;
}

static bool signed_range(const MinicC0Program *program,
                         const MinicTargetInfo *target,
                         MinicType type,
                         int64_t *minimum,
                         int64_t *maximum) {
    unsigned int width;

    if (minimum == NULL || maximum == NULL || !minic_type_is_signed_integer(type) ||
        !integer_width(program, target, type, &width)) {
        return false;
    }
    if (width == 64U) {
        *minimum = INT64_MIN;
        *maximum = INT64_MAX;
    } else {
        *minimum = -(INT64_C(1) << (width - 1U));
        *maximum = (INT64_C(1) << (width - 1U)) - INT64_C(1);
    }
    return true;
}

static bool
signed_add(int64_t left, int64_t right, int64_t minimum, int64_t maximum, int64_t *out) {
    if (out == NULL || (right > 0 && left > maximum - right) ||
        (right < 0 && left < minimum - right)) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool
signed_sub(int64_t left, int64_t right, int64_t minimum, int64_t maximum, int64_t *out) {
    if (out == NULL || (right < 0 && left > maximum + right) ||
        (right > 0 && left < minimum + right)) {
        return false;
    }
    *out = left - right;
    return true;
}

static bool
signed_mul(int64_t left, int64_t right, int64_t minimum, int64_t maximum, int64_t *out) {
    if (out == NULL) {
        return false;
    }
    if (left == 0 || right == 0) {
        *out = 0;
        return true;
    }
    if ((left == -1 && right == minimum) || (right == -1 && left == minimum) ||
        (left > 0 && right > 0 && left > maximum / right) ||
        (left > 0 && right < 0 && right < minimum / left) ||
        (left < 0 && right > 0 && left < minimum / right) ||
        (left < 0 && right < 0 && left < maximum / right)) {
        return false;
    }
    *out = left * right;
    return true;
}

static bool eval_expression(const MinicC0Program *program,
                            const MinicTargetInfo *target,
                            MinicExpressionId expression_id,
                            unsigned int depth,
                            MinicConstValue *value);

static bool eval_builtin_unary(const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicExpression *expression,
                               unsigned int depth,
                               MinicConstValue *value) {
    MinicConstValue operand;
    uint64_t bits;
    uint64_t count;
    unsigned int width;

    if (program == NULL || target == NULL || expression == NULL || value == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||
        expression->value.builtin_unary.operator_kind != MINIC_BUILTIN_UNARY_CLZLL ||
        !eval_expression(
            program, target, expression->value.builtin_unary.operand, depth + 1U, &operand) ||
        !minic_type_is_integer(operand.type) ||
        !integer_width(program, target, operand.type, &width) || width == 0U || width > 64U ||
        !normalize_bits(program, target, operand.type, operand.bits, &bits) || bits == 0U) {
        return false;
    }

    count = 0U;
    while ((bits & (UINT64_C(1) << (width - 1U))) == 0U) {
        count += 1U;
        bits <<= 1U;
    }
    value->type = expression->type;
    return normalize_bits(program, target, value->type, count, &value->bits);
}

static bool eval_binary(const MinicC0Program *program,
                        const MinicTargetInfo *target,
                        const MinicExpression *expression,
                        unsigned int depth,
                        MinicConstValue *value) {
    MinicConstValue left;
    MinicConstValue right;
    MinicConstValue converted_left;
    MinicConstValue converted_right;
    MinicType operation_type;
    bool left_truthy;
    bool right_truthy;
    uint64_t left_bits;
    uint64_t right_bits;
    unsigned int width;

    if (!eval_expression(program, target, expression->value.binary.left, depth + 1U, &left)) {
        return false;
    }
    if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
        if (!value_truthy(program, target, &left, &left_truthy)) {
            return false;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND && !left_truthy) {
            value->type = minic_type_int();
            value->bits = 0U;
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR && left_truthy) {
            value->type = minic_type_int();
            value->bits = 1U;
            return true;
        }
        if (!eval_expression(program, target, expression->value.binary.right, depth + 1U, &right) ||
            !value_truthy(program, target, &right, &right_truthy)) {
            return false;
        }
        value->type = minic_type_int();
        value->bits = right_truthy ? 1U : 0U;
        return true;
    }
    if (!eval_expression(program, target, expression->value.binary.right, depth + 1U, &right)) {
        return false;
    }

    if (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
        expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL ||
        expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
        expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
        expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
        expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL) {
        bool comparison;

        if (!minic_target_info_integer_common(target, left.type, right.type, &operation_type) ||
            !convert_value(program, target, &left, operation_type, &converted_left) ||
            !convert_value(program, target, &right, operation_type, &converted_right)) {
            return false;
        }
        if (minic_type_is_signed_integer(operation_type)) {
            int64_t signed_left;
            int64_t signed_right;

            if (!value_signed(program, target, &converted_left, &signed_left) ||
                !value_signed(program, target, &converted_right, &signed_right)) {
                return false;
            }
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_EQUAL:
                comparison = signed_left == signed_right;
                break;
            case MINIC_BINARY_NOT_EQUAL:
                comparison = signed_left != signed_right;
                break;
            case MINIC_BINARY_LESS:
                comparison = signed_left < signed_right;
                break;
            case MINIC_BINARY_LESS_EQUAL:
                comparison = signed_left <= signed_right;
                break;
            case MINIC_BINARY_GREATER:
                comparison = signed_left > signed_right;
                break;
            case MINIC_BINARY_GREATER_EQUAL:
                comparison = signed_left >= signed_right;
                break;
            default:
                return false;
            }
        } else {
            left_bits = converted_left.bits;
            right_bits = converted_right.bits;
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_EQUAL:
                comparison = left_bits == right_bits;
                break;
            case MINIC_BINARY_NOT_EQUAL:
                comparison = left_bits != right_bits;
                break;
            case MINIC_BINARY_LESS:
                comparison = left_bits < right_bits;
                break;
            case MINIC_BINARY_LESS_EQUAL:
                comparison = left_bits <= right_bits;
                break;
            case MINIC_BINARY_GREATER:
                comparison = left_bits > right_bits;
                break;
            case MINIC_BINARY_GREATER_EQUAL:
                comparison = left_bits >= right_bits;
                break;
            default:
                return false;
            }
        }
        value->type = minic_type_int();
        value->bits = comparison ? 1U : 0U;
        return true;
    }

    operation_type = expression->type;
    if (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
        expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT) {
        if (!minic_target_info_integer_promotion(target, left.type, &operation_type)) {
            return false;
        }
    }
    if (!minic_type_is_integer(operation_type) ||
        !convert_value(program, target, &left, operation_type, &converted_left) ||
        !integer_width(program, target, operation_type, &width)) {
        return false;
    }

    if (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
        expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT) {
        int64_t signed_count;
        uint64_t count;

        if (minic_type_is_signed_integer(right.type)) {
            if (!value_signed(program, target, &right, &signed_count) || signed_count < 0) {
                return false;
            }
            count = (uint64_t)signed_count;
        } else if (!normalize_bits(program, target, right.type, right.bits, &count)) {
            return false;
        }
        if (count >= (uint64_t)width) {
            return false;
        }
        left_bits = converted_left.bits;
        value->type = operation_type;
        if (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT) {
            if (minic_type_is_signed_integer(operation_type)) {
                int64_t signed_left;
                int64_t minimum;
                int64_t maximum;

                if (!value_signed(program, target, &converted_left, &signed_left) ||
                    signed_left < 0 ||
                    !signed_range(program, target, operation_type, &minimum, &maximum) ||
                    (count != 0U && signed_left > (maximum >> count))) {
                    return false;
                }
            }
            return normalize_bits(
                program, target, operation_type, left_bits << count, &value->bits);
        }
        if (minic_type_is_signed_integer(operation_type) && count != 0U &&
            (left_bits & (UINT64_C(1) << (width - 1U))) != 0U) {
            uint64_t fill =
                width_mask(width) ^ ((UINT64_C(1) << (width - (unsigned int)count)) - UINT64_C(1));
            left_bits = (left_bits >> count) | fill;
        } else {
            left_bits >>= count;
        }
        return normalize_bits(program, target, operation_type, left_bits, &value->bits);
    }

    if (!convert_value(program, target, &right, operation_type, &converted_right)) {
        return false;
    }
    left_bits = converted_left.bits;
    right_bits = converted_right.bits;
    value->type = operation_type;

    if (minic_type_is_unsigned_integer(operation_type)) {
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            left_bits += right_bits;
            break;
        case MINIC_BINARY_SUBTRACT:
            left_bits -= right_bits;
            break;
        case MINIC_BINARY_MULTIPLY:
            left_bits *= right_bits;
            break;
        case MINIC_BINARY_DIVIDE:
            if (right_bits == 0U)
                return false;
            left_bits /= right_bits;
            break;
        case MINIC_BINARY_REMAINDER:
            if (right_bits == 0U)
                return false;
            left_bits %= right_bits;
            break;
        case MINIC_BINARY_BITWISE_AND:
            left_bits &= right_bits;
            break;
        case MINIC_BINARY_BITWISE_XOR:
            left_bits ^= right_bits;
            break;
        case MINIC_BINARY_BITWISE_OR:
            left_bits |= right_bits;
            break;
        default:
            return false;
        }
        return normalize_bits(program, target, operation_type, left_bits, &value->bits);
    }

    {
        int64_t signed_left;
        int64_t signed_right;
        int64_t signed_result;
        int64_t minimum;
        int64_t maximum;

        if (!value_signed(program, target, &converted_left, &signed_left) ||
            !value_signed(program, target, &converted_right, &signed_right) ||
            !signed_range(program, target, operation_type, &minimum, &maximum)) {
            return false;
        }
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            if (!signed_add(signed_left, signed_right, minimum, maximum, &signed_result))
                return false;
            break;
        case MINIC_BINARY_SUBTRACT:
            if (!signed_sub(signed_left, signed_right, minimum, maximum, &signed_result))
                return false;
            break;
        case MINIC_BINARY_MULTIPLY:
            if (!signed_mul(signed_left, signed_right, minimum, maximum, &signed_result))
                return false;
            break;
        case MINIC_BINARY_DIVIDE:
            if (signed_right == 0 || (signed_left == minimum && signed_right == -1))
                return false;
            signed_result = signed_left / signed_right;
            break;
        case MINIC_BINARY_REMAINDER:
            if (signed_right == 0 || (signed_left == minimum && signed_right == -1))
                return false;
            signed_result = signed_left % signed_right;
            break;
        case MINIC_BINARY_BITWISE_AND:
            return normalize_bits(
                program, target, operation_type, left_bits & right_bits, &value->bits);
        case MINIC_BINARY_BITWISE_XOR:
            return normalize_bits(
                program, target, operation_type, left_bits ^ right_bits, &value->bits);
        case MINIC_BINARY_BITWISE_OR:
            return normalize_bits(
                program, target, operation_type, left_bits | right_bits, &value->bits);
        default:
            return false;
        }
        return normalize_bits(
            program, target, operation_type, (uint64_t)signed_result, &value->bits);
    }
}

static bool eval_expression(const MinicC0Program *program,
                            const MinicTargetInfo *target,
                            MinicExpressionId expression_id,
                            unsigned int depth,
                            MinicConstValue *value) {
    const MinicExpression *expression;

    if (program == NULL || target == NULL || value == NULL || depth > MINIC_CONST_EVAL_MAX_DEPTH) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        value->type = expression->type;
        return normalize_bits(program,
                              target,
                              expression->type,
                              (uint64_t)expression->value.integer_value,
                              &value->bits);
    case MINIC_EXPRESSION_SIZEOF: {
        size_t size;

        if (!minic_target_info_sizeof_type(target, program, expression->value.sizeof_type, &size)) {
            return false;
        }
        value->type = expression->type;
        return normalize_bits(program, target, expression->type, (uint64_t)size, &value->bits);
    }
    case MINIC_EXPRESSION_OFFSETOF: {
        const MinicRecord *record;
        size_t offset;

        record = minic_c0_program_record(program, expression->value.offsetof_value.record_id);
        if (record == NULL ||
            !minic_data_layout_record_field_offset(minic_target_info_data_layout(target),
                                                   program,
                                                   record,
                                                   expression->value.offsetof_value.field_index,
                                                   &offset) ||
            expression->value.offsetof_value.anonymous_prefix_offset > SIZE_MAX - offset) {
            return false;
        }
        offset += expression->value.offsetof_value.anonymous_prefix_offset;
        value->type = expression->type;
        return normalize_bits(program, target, expression->type, (uint64_t)offset, &value->bits);
    }
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION: {
        MinicConstValue operand;

        return eval_expression(
                   program, target, expression->value.unary.operand, depth + 1U, &operand) &&
               convert_value(program, target, &operand, expression->type, value);
    }
    case MINIC_EXPRESSION_UNARY: {
        MinicConstValue operand;
        MinicConstValue converted;
        bool truthy;

        if (!eval_expression(
                program, target, expression->value.unary.operand, depth + 1U, &operand)) {
            return false;
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
            if (!value_truthy(program, target, &operand, &truthy)) {
                return false;
            }
            value->type = minic_type_int();
            value->bits = truthy ? 0U : 1U;
            return true;
        }
        if (!convert_value(program, target, &operand, expression->type, &converted)) {
            return false;
        }
        value->type = expression->type;
        switch (expression->value.unary.operator_kind) {
        case MINIC_UNARY_PLUS:
            *value = converted;
            return true;
        case MINIC_UNARY_BITWISE_NOT:
            return normalize_bits(program, target, expression->type, ~converted.bits, &value->bits);
        case MINIC_UNARY_NEGATE:
            if (minic_type_is_unsigned_integer(expression->type)) {
                return normalize_bits(
                    program, target, expression->type, 0U - converted.bits, &value->bits);
            } else {
                int64_t signed_operand;
                int64_t minimum;
                int64_t maximum;

                if (!value_signed(program, target, &converted, &signed_operand) ||
                    !signed_range(program, target, expression->type, &minimum, &maximum) ||
                    signed_operand == minimum) {
                    return false;
                }
                (void)maximum;
                return normalize_bits(
                    program, target, expression->type, (uint64_t)(-signed_operand), &value->bits);
            }
        default:
            return false;
        }
    }
    case MINIC_EXPRESSION_BUILTIN_UNARY:
        return eval_builtin_unary(program, target, expression, depth, value);
    case MINIC_EXPRESSION_BINARY:
        return eval_binary(program, target, expression, depth, value);
    case MINIC_EXPRESSION_CONDITIONAL: {
        MinicConstValue condition;
        MinicConstValue selected;
        bool truthy;
        MinicExpressionId selected_id;

        if (!eval_expression(
                program, target, expression->value.conditional.condition, depth + 1U, &condition) ||
            !value_truthy(program, target, &condition, &truthy)) {
            return false;
        }
        if (truthy && expression->value.conditional.uses_condition_value) {
            return convert_value(program, target, &condition, expression->type, value);
        }
        selected_id = truthy ? expression->value.conditional.when_true
                             : expression->value.conditional.when_false;
        return eval_expression(program, target, selected_id, depth + 1U, &selected) &&
               convert_value(program, target, &selected, expression->type, value);
    }
    default:
        return false;
    }
}

bool minic_const_eval_integer(const MinicC0Program *program,
                              const MinicTargetInfo *target,
                              MinicExpressionId expression_id,
                              MinicConstValue *value) {
    return eval_expression(program, target, expression_id, 0U, value);
}

bool minic_const_value_is_zero(const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicConstValue *value,
                               bool *is_zero) {
    bool truthy;

    if (is_zero == NULL || !value_truthy(program, target, value, &truthy)) {
        return false;
    }
    *is_zero = !truthy;
    return true;
}

bool minic_const_value_as_int64(const MinicC0Program *program,
                                const MinicTargetInfo *target,
                                const MinicConstValue *value,
                                int64_t *result) {
    uint64_t bits;

    if (value == NULL || result == NULL) {
        return false;
    }
    if (minic_type_is_signed_integer(value->type)) {
        return value_signed(program, target, value, result);
    }
    if (!normalize_bits(program, target, value->type, value->bits, &bits) ||
        bits > (uint64_t)INT64_MAX) {
        return false;
    }
    *result = (int64_t)bits;
    return true;
}
