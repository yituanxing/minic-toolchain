#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"

#include <limits.h>
#include <string.h>

#define MINIC_CONST_EVAL_MAX_DEPTH 256U

static bool integer_width(const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          MinicType type,
                          unsigned int *width) {
    return minic_target_info_integer_width(target, program, type, width) && *width <= 64U;
}

static bool integer_type_is_signed(const MinicC0Program *program, MinicType type) {
    MinicType effective_type;

    return minic_c0_type_effective_integer_type(program, type, &effective_type) &&
           minic_type_is_signed_integer(effective_type);
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

    if (value == NULL || result == NULL || !integer_type_is_signed(program, value->type) ||
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

static bool signed_range(const MinicC0Program *program,
                         const MinicTargetInfo *target,
                         MinicType type,
                         int64_t *minimum,
                         int64_t *maximum);

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
    if (integer_type_is_signed(program, source->type)) {
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

bool minic_const_value_integer_representable_in_type(const MinicC0Program *program,
                                                     const MinicTargetInfo *target,
                                                     const MinicConstValue *value,
                                                     MinicType destination_type) {
    unsigned int destination_width;
    uint64_t unsigned_value;

    if (program == NULL || target == NULL || value == NULL || !minic_type_is_integer(value->type) ||
        !minic_type_is_integer(destination_type) ||
        !integer_width(program, target, destination_type, &destination_width) ||
        destination_width == 0U) {
        return false;
    }

    if (integer_type_is_signed(program, value->type)) {
        int64_t signed_value;

        if (!value_signed(program, target, value, &signed_value)) {
            return false;
        }
        if (minic_type_is_bool_integer(destination_type)) {
            return signed_value == 0 || signed_value == 1;
        }
        if (integer_type_is_signed(program, destination_type)) {
            int64_t minimum;
            int64_t maximum;

            return signed_range(program, target, destination_type, &minimum, &maximum) &&
                   signed_value >= minimum && signed_value <= maximum;
        }
        if (signed_value < 0) {
            return false;
        }
        unsigned_value = (uint64_t)signed_value;
    } else if (!normalize_bits(program, target, value->type, value->bits, &unsigned_value)) {
        return false;
    }

    if (minic_type_is_bool_integer(destination_type)) {
        return unsigned_value <= UINT64_C(1);
    }
    if (integer_type_is_signed(program, destination_type)) {
        uint64_t maximum = destination_width == 64U
                               ? (uint64_t)INT64_MAX
                               : (UINT64_C(1) << (destination_width - 1U)) - UINT64_C(1);
        return unsigned_value <= maximum;
    }
    if (destination_width == 64U) {
        return true;
    }
    return unsigned_value <= (UINT64_C(1) << destination_width) - UINT64_C(1);
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

    if (minimum == NULL || maximum == NULL || !integer_type_is_signed(program, type) ||
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

static bool eval_null_based_pointer_constant(const MinicC0Program *program,
                                             const MinicTargetInfo *target,
                                             MinicExpressionId expression_id,
                                             unsigned int depth,
                                             uint64_t *byte_offset) {
    const MinicExpression *expression;

    if (program == NULL || target == NULL || byte_offset == NULL ||
        depth > MINIC_CONST_EVAL_MAX_DEPTH) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_pointer(expression->type)) {
        return false;
    }

    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_BITCAST) {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(program, expression->value.unary.operand);
        if (operand == NULL) {
            return false;
        }
        if (minic_type_is_pointer(operand->type)) {
            return eval_null_based_pointer_constant(program,
                                                    target,
                                                    expression->value.unary.operand,
                                                    depth + 1U,
                                                    byte_offset);
        }
        if (minic_type_is_integer(operand->type)) {
            MinicConstValue integer_value;
            uint64_t normalized;

            if (!eval_expression(program,
                                 target,
                                 expression->value.unary.operand,
                                 depth + 1U,
                                 &integer_value) ||
                !normalize_bits(program,
                                target,
                                integer_value.type,
                                integer_value.bits,
                                &normalized) ||
                normalized != 0U) {
                return false;
            }
            *byte_offset = 0U;
            return true;
        }
        return false;
    }

    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        const MinicExpression *addressed;

        addressed =
            minic_c0_program_expression(program, expression->value.unary.operand);
        if (addressed != NULL && addressed->kind == MINIC_EXPRESSION_MEMBER) {
            const MinicRecord *record;
            uint64_t base_offset;
            size_t field_offset;

            if (!eval_null_based_pointer_constant(program,
                                                  target,
                                                  addressed->value.member.base,
                                                  depth + 1U,
                                                  &base_offset)) {
                return false;
            }
            record = minic_c0_program_record(program, addressed->value.member.record_id);
            if (record == NULL ||
                !minic_data_layout_record_field_offset(
                    minic_target_info_data_layout(target),
                    program,
                    record,
                    addressed->value.member.field_index,
                    &field_offset) ||
                base_offset > UINT64_MAX - (uint64_t)field_offset) {
                return false;
            }
            *byte_offset = base_offset + (uint64_t)field_offset;
            return true;
        }
    }
    return false;
}

static bool eval_null_based_pointer_difference(const MinicC0Program *program,
                                               const MinicTargetInfo *target,
                                               const MinicExpression *expression,
                                               unsigned int depth,
                                               MinicConstValue *value) {
    const MinicExpression *left;
    const MinicExpression *right;
    MinicType pointee;
    uint64_t left_offset;
    uint64_t right_offset;
    uint64_t magnitude;
    size_t element_size;
    int64_t difference;

    if (program == NULL || target == NULL || expression == NULL || value == NULL ||
        expression->kind != MINIC_EXPRESSION_BINARY ||
        expression->value.binary.operator_kind != MINIC_BINARY_SUBTRACT ||
        !minic_type_is_integer(expression->type)) {
        return false;
    }
    left = minic_c0_program_expression(program, expression->value.binary.left);
    right = minic_c0_program_expression(program, expression->value.binary.right);
    if (left == NULL || right == NULL || !minic_type_is_pointer(left->type) ||
        !minic_type_is_pointer(right->type) ||
        !minic_c0_pointer_difference_compatible(program, left->type, right->type) ||
        !eval_null_based_pointer_constant(
            program, target, expression->value.binary.left, depth + 1U, &left_offset) ||
        !eval_null_based_pointer_constant(
            program, target, expression->value.binary.right, depth + 1U, &right_offset) ||
        !minic_type_pointee(left->type, &pointee) ||
        !minic_c0_pointer_arithmetic_element_size(
            program, minic_target_info_data_layout(target), left->type, &element_size) ||
        element_size == 0U) {
        return false;
    }
    (void)pointee;

    if (left_offset >= right_offset) {
        magnitude = left_offset - right_offset;
        if (magnitude % (uint64_t)element_size != 0U ||
            magnitude / (uint64_t)element_size > (uint64_t)INT64_MAX) {
            return false;
        }
        difference = (int64_t)(magnitude / (uint64_t)element_size);
    } else {
        magnitude = right_offset - left_offset;
        if (magnitude % (uint64_t)element_size != 0U ||
            magnitude / (uint64_t)element_size > (uint64_t)INT64_MAX) {
            return false;
        }
        difference = -(int64_t)(magnitude / (uint64_t)element_size);
    }
    value->type = expression->type;
    return normalize_bits(
        program, target, expression->type, (uint64_t)difference, &value->bits);
}

static bool integer_cast_operand_is_pointer_roundtrip_constant(const MinicC0Program *program,
                                                               const MinicTargetInfo *target,
                                                               const MinicExpression *expression,
                                                               unsigned int depth,
                                                               uint64_t *bits) {
    const MinicExpression *pointer_cast;
    const MinicExpression *integer_operand;
    MinicConstValue operand;
    uint64_t operand_bits;
    size_t pointer_size;
    unsigned int pointer_width;

    if (program == NULL || target == NULL || expression == NULL || bits == NULL ||
        !minic_type_is_integer(expression->type) || depth > MINIC_CONST_EVAL_MAX_DEPTH - 2U) {
        return false;
    }
    pointer_cast = minic_c0_program_expression(program, expression->value.unary.operand);
    if (pointer_cast == NULL || !minic_type_is_pointer(pointer_cast->type) ||
        (pointer_cast->kind != MINIC_EXPRESSION_CAST &&
         pointer_cast->kind != MINIC_EXPRESSION_BITCAST)) {
        return false;
    }
    integer_operand = minic_c0_program_expression(program, pointer_cast->value.unary.operand);
    if (integer_operand == NULL || !minic_type_is_integer(integer_operand->type) ||
        !eval_expression(
            program, target, pointer_cast->value.unary.operand, depth + 2U, &operand) ||
        !minic_target_info_sizeof_type(target, program, pointer_cast->type, &pointer_size) ||
        pointer_size == 0U || pointer_size > sizeof(uint64_t)) {
        return false;
    }
    if (integer_type_is_signed(program, operand.type)) {
        int64_t signed_value;

        if (!value_signed(program, target, &operand, &signed_value) || signed_value < 0) {
            return false;
        }
        operand_bits = (uint64_t)signed_value;
    } else if (!normalize_bits(program, target, operand.type, operand.bits, &operand_bits)) {
        return false;
    }
    pointer_width = (unsigned int)(pointer_size * (size_t)CHAR_BIT);
    if (pointer_width == 0U || pointer_width > 64U ||
        (pointer_width < 64U && operand_bits > width_mask(pointer_width))) {
        return false;
    }
    return normalize_bits(program, target, expression->type, operand_bits, bits);
}

static bool eval_builtin_unary(const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicExpression *expression,
                               unsigned int depth,
                               MinicConstValue *value) {
    MinicConstValue operand;
    MinicBuiltinUnaryOperator operator_kind;
    uint64_t bits;
    uint64_t count;
    unsigned int width;

    if (program == NULL || target == NULL || expression == NULL || value == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||
        !eval_expression(
            program, target, expression->value.builtin_unary.operand, depth + 1U, &operand) ||
        !minic_type_is_integer(operand.type) ||
        !integer_width(program, target, operand.type, &width) || width == 0U || width > 64U ||
        !normalize_bits(program, target, operand.type, operand.bits, &bits)) {
        return false;
    }

    operator_kind = expression->value.builtin_unary.operator_kind;
    count = 0U;
    switch (operator_kind) {
    case MINIC_BUILTIN_UNARY_CLZ:
    case MINIC_BUILTIN_UNARY_CLZL:
    case MINIC_BUILTIN_UNARY_CLZLL:
        if (bits == 0U) {
            return false;
        }
        while ((bits & (UINT64_C(1) << (width - 1U))) == 0U) {
            count += 1U;
            bits <<= 1U;
        }
        break;
    case MINIC_BUILTIN_UNARY_CTZ:
    case MINIC_BUILTIN_UNARY_CTZL:
    case MINIC_BUILTIN_UNARY_CTZLL:
        if (bits == 0U) {
            return false;
        }
        while ((bits & UINT64_C(1)) == 0U) {
            count += 1U;
            bits >>= 1U;
        }
        break;
    case MINIC_BUILTIN_UNARY_FFSLL:
        if (bits == 0U) {
            count = 0U;
            break;
        }
        count = 1U;
        while ((bits & UINT64_C(1)) == 0U) {
            count += 1U;
            bits >>= 1U;
        }
        break;
    case MINIC_BUILTIN_UNARY_ISDIGIT:
        count = bits >= UINT64_C(48) && bits <= UINT64_C(57) ? 1U : 0U;
        break;
    default:
        return false;
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

    if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT &&
        eval_null_based_pointer_difference(program, target, expression, depth, value)) {
        return true;
    }
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

        if (!minic_target_info_integer_common_for_program(
                target, program, left.type, right.type, &operation_type) ||
            !convert_value(program, target, &left, operation_type, &converted_left) ||
            !convert_value(program, target, &right, operation_type, &converted_right)) {
            return false;
        }
        if (integer_type_is_signed(program, operation_type)) {
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
        if (!minic_target_info_integer_common_for_program(
                target, program, left.type, left.type, &operation_type)) {
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

        if (integer_type_is_signed(program, right.type)) {
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
            if (integer_type_is_signed(program, operation_type)) {
                int64_t signed_left;

                /* GNU C folds nonnegative signed left shifts in integer constant
                   expressions using target-width bits even when the result sets
                   the sign bit (for example 1 << 31 on a 32-bit int). Keep
                   negative operands and out-of-width counts rejected above, but
                   preserve the target bit pattern instead of rejecting this GNU
                   extension as signed overflow. */
                if (!value_signed(program, target, &converted_left, &signed_left) ||
                    signed_left < 0) {
                    return false;
                }
            }
            return normalize_bits(
                program, target, operation_type, left_bits << count, &value->bits);
        }
        if (integer_type_is_signed(program, operation_type) && count != 0U &&
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
                                                   &offset)) {
            return false;
        }
        value->type = expression->type;
        return normalize_bits(program, target, expression->type, (uint64_t)offset, &value->bits);
    }
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION: {
        MinicConstValue operand;
        uint64_t pointer_roundtrip_bits;

        if (expression->kind == MINIC_EXPRESSION_CAST &&
            integer_cast_operand_is_pointer_roundtrip_constant(
                program, target, expression, depth, &pointer_roundtrip_bits)) {
            value->type = expression->type;
            value->bits = pointer_roundtrip_bits;
            return true;
        }
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

typedef struct MinicArithmeticConstValue {
    MinicType type;
    MinicConstValue integer;
    double floating;
    bool is_floating;
} MinicArithmeticConstValue;

static bool arithmetic_integer_to_double(const MinicC0Program *program,
                                         const MinicTargetInfo *target,
                                         const MinicConstValue *value,
                                         double *result) {
    uint64_t bits;

    if (program == NULL || target == NULL || value == NULL || result == NULL ||
        !minic_type_is_integer(value->type)) {
        return false;
    }
    if (integer_type_is_signed(program, value->type)) {
        int64_t signed_value;

        if (!value_signed(program, target, value, &signed_value)) {
            return false;
        }
        *result = (double)signed_value;
        return true;
    }
    if (!normalize_bits(program, target, value->type, value->bits, &bits)) {
        return false;
    }
    *result = (double)bits;
    return true;
}

static bool arithmetic_value_as_double(const MinicC0Program *program,
                                       const MinicTargetInfo *target,
                                       const MinicArithmeticConstValue *value,
                                       double *result) {
    if (value == NULL || result == NULL) {
        return false;
    }
    if (value->is_floating) {
        *result = value->floating;
        return true;
    }
    return arithmetic_integer_to_double(program, target, &value->integer, result);
}

static bool arithmetic_double_to_integer(const MinicC0Program *program,
                                         const MinicTargetInfo *target,
                                         double input,
                                         MinicType type,
                                         MinicConstValue *result) {
    unsigned int width;
    uint64_t bits;

    if (program == NULL || target == NULL || result == NULL || !minic_type_is_integer(type) ||
        !integer_width(program, target, type, &width) || width == 0U) {
        return false;
    }
    result->type = type;
    if (minic_type_is_bool_integer(type)) {
        result->bits = input == 0.0 ? 0U : 1U;
        return true;
    }
    if (integer_type_is_signed(program, type)) {
        int64_t signed_value;

        if (width == 64U) {
            /* 2^63 is exactly representable as binary64, INT64_MAX is not. */
            if (!(input >= -9223372036854775808.0 && input < 9223372036854775808.0)) {
                return false;
            }
        } else {
            int64_t minimum;
            int64_t maximum;

            if (!signed_range(program, target, type, &minimum, &maximum) ||
                !(input >= (double)minimum && input <= (double)maximum)) {
                return false;
            }
        }
        signed_value = (int64_t)input;
        bits = (uint64_t)signed_value;
    } else {
        if (width == 64U) {
            if (!(input >= 0.0 && input < 18446744073709551616.0)) {
                return false;
            }
        } else {
            uint64_t maximum;

            maximum = (UINT64_C(1) << width) - UINT64_C(1);
            if (!(input >= 0.0 && input <= (double)maximum)) {
                return false;
            }
        }
        bits = (uint64_t)input;
    }
    return normalize_bits(program, target, type, bits, &result->bits);
}

static bool eval_arithmetic_expression(const MinicC0Program *program,
                                       const MinicTargetInfo *target,
                                       MinicExpressionId expression_id,
                                       unsigned int depth,
                                       MinicArithmeticConstValue *value) {
    const MinicExpression *expression;

    if (program == NULL || target == NULL || value == NULL ||
        depth > MINIC_CONST_EVAL_MAX_DEPTH) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }

    (void)memset(value, 0, sizeof(*value));
    value->type = expression->type;
    if (minic_type_is_integer(expression->type)) {
        if (eval_expression(program, target, expression_id, depth, &value->integer)) {
            value->is_floating = false;
            return true;
        }
        if (expression->kind == MINIC_EXPRESSION_CAST ||
            expression->kind == MINIC_EXPRESSION_CONVERSION) {
            MinicArithmeticConstValue operand;
            double floating_operand;

            if (!eval_arithmetic_expression(program,
                                            target,
                                            expression->value.unary.operand,
                                            depth + 1U,
                                            &operand) ||
                !operand.is_floating ||
                !arithmetic_value_as_double(program, target, &operand, &floating_operand) ||
                !arithmetic_double_to_integer(
                    program, target, floating_operand, expression->type, &value->integer)) {
                return false;
            }
            value->is_floating = false;
            return true;
        }
        return false;
    }

    if (!minic_type_is_float(expression->type) && !minic_type_is_double(expression->type)) {
        return false;
    }
    value->is_floating = true;

    switch (expression->kind) {
    case MINIC_EXPRESSION_FLOATING: {
        double floating;

        (void)memcpy(&floating, &expression->value.floating_bits, sizeof(floating));
        if (minic_type_is_float(expression->type)) {
            floating = (double)(float)floating;
        }
        value->floating = floating;
        return true;
    }
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_CONVERSION: {
        MinicArithmeticConstValue operand;
        double converted;

        if (!eval_arithmetic_expression(program,
                                        target,
                                        expression->value.unary.operand,
                                        depth + 1U,
                                        &operand) ||
            !arithmetic_value_as_double(program, target, &operand, &converted)) {
            return false;
        }
        value->floating =
            minic_type_is_float(expression->type) ? (double)(float)converted : converted;
        return true;
    }
    case MINIC_EXPRESSION_UNARY: {
        MinicArithmeticConstValue operand;
        double converted;

        if (!eval_arithmetic_expression(program,
                                        target,
                                        expression->value.unary.operand,
                                        depth + 1U,
                                        &operand) ||
            !arithmetic_value_as_double(program, target, &operand, &converted)) {
            return false;
        }
        switch (expression->value.unary.operator_kind) {
        case MINIC_UNARY_PLUS:
            break;
        case MINIC_UNARY_NEGATE:
            converted = -converted;
            break;
        default:
            return false;
        }
        value->floating =
            minic_type_is_float(expression->type) ? (double)(float)converted : converted;
        return true;
    }
    case MINIC_EXPRESSION_BINARY: {
        MinicArithmeticConstValue left;
        MinicArithmeticConstValue right;
        double left_value;
        double right_value;
        double result_value;

        if (!eval_arithmetic_expression(program,
                                        target,
                                        expression->value.binary.left,
                                        depth + 1U,
                                        &left) ||
            !eval_arithmetic_expression(program,
                                        target,
                                        expression->value.binary.right,
                                        depth + 1U,
                                        &right) ||
            !arithmetic_value_as_double(program, target, &left, &left_value) ||
            !arithmetic_value_as_double(program, target, &right, &right_value)) {
            return false;
        }
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            result_value = left_value + right_value;
            break;
        case MINIC_BINARY_SUBTRACT:
            result_value = left_value - right_value;
            break;
        case MINIC_BINARY_MULTIPLY:
            result_value = left_value * right_value;
            break;
        case MINIC_BINARY_DIVIDE:
            if (right_value == 0.0) {
                return false;
            }
            result_value = left_value / right_value;
            break;
        default:
            return false;
        }
        value->floating = minic_type_is_float(expression->type)
                              ? (double)(float)result_value
                              : result_value;
        return true;
    }
    case MINIC_EXPRESSION_CONDITIONAL: {
        MinicArithmeticConstValue condition;
        MinicArithmeticConstValue selected;
        double condition_value;
        MinicExpressionId selected_id;

        if (!eval_arithmetic_expression(program,
                                        target,
                                        expression->value.conditional.condition,
                                        depth + 1U,
                                        &condition) ||
            !arithmetic_value_as_double(program, target, &condition, &condition_value)) {
            return false;
        }
        if (condition_value != 0.0 && expression->value.conditional.uses_condition_value) {
            value->floating = minic_type_is_float(expression->type)
                                  ? (double)(float)condition_value
                                  : condition_value;
            return true;
        }
        selected_id = condition_value != 0.0 ? expression->value.conditional.when_true
                                             : expression->value.conditional.when_false;
        if (!eval_arithmetic_expression(program, target, selected_id, depth + 1U, &selected) ||
            !arithmetic_value_as_double(program, target, &selected, &value->floating)) {
            return false;
        }
        if (minic_type_is_float(expression->type)) {
            value->floating = (double)(float)value->floating;
        }
        return true;
    }
    default:
        return false;
    }
}

bool minic_const_eval_arithmetic_to_integer(const MinicC0Program *program,
                                            const MinicTargetInfo *target,
                                            MinicExpressionId expression_id,
                                            MinicType destination_type,
                                            MinicConstValue *value) {
    MinicArithmeticConstValue arithmetic;
    double floating;

    if (value == NULL || !minic_type_is_integer(destination_type) ||
        !eval_arithmetic_expression(program, target, expression_id, 0U, &arithmetic)) {
        return false;
    }
    if (!arithmetic.is_floating) {
        return convert_value(program, target, &arithmetic.integer, destination_type, value);
    }
    return arithmetic_value_as_double(program, target, &arithmetic, &floating) &&
           arithmetic_double_to_integer(program, target, floating, destination_type, value);
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
    if (integer_type_is_signed(program, value->type)) {
        return value_signed(program, target, value, result);
    }
    if (!normalize_bits(program, target, value->type, value->bits, &bits) ||
        bits > (uint64_t)INT64_MAX) {
        return false;
    }
    *result = (int64_t)bits;
    return true;
}
