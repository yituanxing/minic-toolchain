#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]

(root / "src/target/target_info.h").write_text(r'''#ifndef MINIC_TARGET_TARGET_INFO_H
#define MINIC_TARGET_TARGET_INFO_H

#include "target/data_layout.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct MinicTargetInfo {
    const MinicDataLayout *data_layout;
    bool gnu_sizeof_void_is_one;
    bool gnu_sizeof_function_is_one;
} MinicTargetInfo;

const MinicTargetInfo *minic_default_target_info(void);
const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target);
bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size);
bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits);

#endif
''')

(root / "src/target/target_info.c").write_text(r'''#include "target/target_info.h"

#include <limits.h>

const MinicTargetInfo *minic_default_target_info(void) {
    static MinicTargetInfo target;

    if (target.data_layout == NULL) {
        target.data_layout = minic_default_data_layout();
        target.gnu_sizeof_void_is_one = true;
        target.gnu_sizeof_function_is_one = true;
    }
    return &target;
}

const MinicDataLayout *minic_target_info_data_layout(const MinicTargetInfo *target) {
    return target == NULL ? NULL : target->data_layout;
}

bool minic_target_info_sizeof_type(const MinicTargetInfo *target,
                                   const MinicC0Program *program,
                                   MinicType type,
                                   size_t *size) {
    size_t alignment;

    if (target == NULL || program == NULL || size == NULL) {
        return false;
    }
    if (minic_type_is_void(type)) {
        if (!target->gnu_sizeof_void_is_one) {
            return false;
        }
        *size = 1U;
        return true;
    }
    if (minic_type_is_function(type)) {
        if (!target->gnu_sizeof_function_is_one) {
            return false;
        }
        *size = 1U;
        return true;
    }
    return minic_data_layout_type(target->data_layout, program, type, size, &alignment);
}

bool minic_target_info_integer_width(const MinicTargetInfo *target,
                                     const MinicC0Program *program,
                                     MinicType type,
                                     unsigned int *bits) {
    size_t size;
    size_t alignment;

    if (target == NULL || program == NULL || bits == NULL || !minic_type_is_integer(type) ||
        !minic_data_layout_type(target->data_layout, program, type, &size, &alignment) ||
        size == 0U || size > (size_t)(UINT_MAX / CHAR_BIT)) {
        return false;
    }
    (void)alignment;
    *bits = (unsigned int)(size * CHAR_BIT);
    return true;
}
''')

(root / "src/frontend/const_eval.h").write_text(r'''#ifndef MINIC_FRONTEND_CONST_EVAL_H
#define MINIC_FRONTEND_CONST_EVAL_H

#include "frontend/ast.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct MinicConstValue {
    MinicType type;
    uint64_t bits;
} MinicConstValue;

bool minic_const_eval_integer(const MinicC0Program *program,
                              const MinicTargetInfo *target,
                              MinicExpressionId expression_id,
                              MinicConstValue *value);
bool minic_const_value_is_zero(const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicConstValue *value,
                               bool *is_zero);
bool minic_const_value_as_int64(const MinicC0Program *program,
                                const MinicTargetInfo *target,
                                const MinicConstValue *value,
                                int64_t *result);

#endif
''')

(root / "src/frontend/const_eval.c").write_text(r'''#include "frontend/const_eval.h"

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

static bool signed_add(int64_t left, int64_t right, int64_t minimum, int64_t maximum, int64_t *out) {
    if (out == NULL || (right > 0 && left > maximum - right) ||
        (right < 0 && left < minimum - right)) {
        return false;
    }
    *out = left + right;
    return true;
}

static bool signed_sub(int64_t left, int64_t right, int64_t minimum, int64_t maximum, int64_t *out) {
    if (out == NULL || (right < 0 && left > maximum + right) ||
        (right > 0 && left < minimum + right)) {
        return false;
    }
    *out = left - right;
    return true;
}

static bool signed_mul(int64_t left, int64_t right, int64_t minimum, int64_t maximum, int64_t *out) {
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

        if (!minic_type_integer_common(left.type, right.type, &operation_type) ||
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
            case MINIC_BINARY_EQUAL: comparison = signed_left == signed_right; break;
            case MINIC_BINARY_NOT_EQUAL: comparison = signed_left != signed_right; break;
            case MINIC_BINARY_LESS: comparison = signed_left < signed_right; break;
            case MINIC_BINARY_LESS_EQUAL: comparison = signed_left <= signed_right; break;
            case MINIC_BINARY_GREATER: comparison = signed_left > signed_right; break;
            case MINIC_BINARY_GREATER_EQUAL: comparison = signed_left >= signed_right; break;
            default: return false;
            }
        } else {
            left_bits = converted_left.bits;
            right_bits = converted_right.bits;
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_EQUAL: comparison = left_bits == right_bits; break;
            case MINIC_BINARY_NOT_EQUAL: comparison = left_bits != right_bits; break;
            case MINIC_BINARY_LESS: comparison = left_bits < right_bits; break;
            case MINIC_BINARY_LESS_EQUAL: comparison = left_bits <= right_bits; break;
            case MINIC_BINARY_GREATER: comparison = left_bits > right_bits; break;
            case MINIC_BINARY_GREATER_EQUAL: comparison = left_bits >= right_bits; break;
            default: return false;
            }
        }
        value->type = minic_type_int();
        value->bits = comparison ? 1U : 0U;
        return true;
    }

    operation_type = expression->type;
    if (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
        expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT) {
        if (!minic_type_integer_common(left.type, left.type, &operation_type)) {
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

                if (!value_signed(program, target, &converted_left, &signed_left) || signed_left < 0 ||
                    !signed_range(program, target, operation_type, &minimum, &maximum) ||
                    (count != 0U && signed_left > (maximum >> count))) {
                    return false;
                }
            }
            return normalize_bits(program, target, operation_type, left_bits << count, &value->bits);
        }
        if (minic_type_is_signed_integer(operation_type) && count != 0U &&
            (left_bits & (UINT64_C(1) << (width - 1U))) != 0U) {
            uint64_t fill = width_mask(width) ^
                            ((UINT64_C(1) << (width - (unsigned int)count)) - UINT64_C(1));
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
        case MINIC_BINARY_ADD: left_bits += right_bits; break;
        case MINIC_BINARY_SUBTRACT: left_bits -= right_bits; break;
        case MINIC_BINARY_MULTIPLY: left_bits *= right_bits; break;
        case MINIC_BINARY_DIVIDE:
            if (right_bits == 0U) return false;
            left_bits /= right_bits;
            break;
        case MINIC_BINARY_REMAINDER:
            if (right_bits == 0U) return false;
            left_bits %= right_bits;
            break;
        case MINIC_BINARY_BITWISE_AND: left_bits &= right_bits; break;
        case MINIC_BINARY_BITWISE_XOR: left_bits ^= right_bits; break;
        case MINIC_BINARY_BITWISE_OR: left_bits |= right_bits; break;
        default: return false;
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
            if (!signed_add(signed_left, signed_right, minimum, maximum, &signed_result)) return false;
            break;
        case MINIC_BINARY_SUBTRACT:
            if (!signed_sub(signed_left, signed_right, minimum, maximum, &signed_result)) return false;
            break;
        case MINIC_BINARY_MULTIPLY:
            if (!signed_mul(signed_left, signed_right, minimum, maximum, &signed_result)) return false;
            break;
        case MINIC_BINARY_DIVIDE:
            if (signed_right == 0 || (signed_left == minimum && signed_right == -1)) return false;
            signed_result = signed_left / signed_right;
            break;
        case MINIC_BINARY_REMAINDER:
            if (signed_right == 0 || (signed_left == minimum && signed_right == -1)) return false;
            signed_result = signed_left % signed_right;
            break;
        case MINIC_BINARY_BITWISE_AND:
            return normalize_bits(program, target, operation_type, left_bits & right_bits, &value->bits);
        case MINIC_BINARY_BITWISE_XOR:
            return normalize_bits(program, target, operation_type, left_bits ^ right_bits, &value->bits);
        case MINIC_BINARY_BITWISE_OR:
            return normalize_bits(program, target, operation_type, left_bits | right_bits, &value->bits);
        default:
            return false;
        }
        return normalize_bits(program, target, operation_type, (uint64_t)signed_result, &value->bits);
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
        return normalize_bits(
            program, target, expression->type, (uint64_t)expression->value.integer_value, &value->bits);
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

        return eval_expression(program, target, expression->value.unary.operand, depth + 1U, &operand) &&
               convert_value(program, target, &operand, expression->type, value);
    }
    case MINIC_EXPRESSION_UNARY: {
        MinicConstValue operand;
        MinicConstValue converted;
        bool truthy;

        if (!eval_expression(program, target, expression->value.unary.operand, depth + 1U, &operand)) {
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
                return normalize_bits(program, target, expression->type, 0U - converted.bits, &value->bits);
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
    if (!normalize_bits(program, target, value->type, value->bits, &bits) || bits > (uint64_t)INT64_MAX) {
        return false;
    }
    *result = (int64_t)bits;
    return true;
}
''')

# Make the parser depend on TargetInfo, with DataLayout obtained through it.
path = root / "src/frontend/parser_internal.h"
text = path.read_text()
text = replace_once(
    text,
    '#include "frontend/attribute.h"\n',
    '#include "frontend/attribute.h"\n#include "frontend/const_eval.h"\n',
    "parser-include-consteval",
)
text = replace_once(
    text,
    '#include "target/data_layout.h"\n',
    '#include "target/target_info.h"\n',
    "parser-include-target-info",
)
text = replace_once(
    text,
    '    const MinicDataLayout *data_layout;\n',
    '    const MinicTargetInfo *target_info;\n',
    "parser-target-info-field",
)
old_eval_decl = '''bool minic_parser_evaluate_integer_constant_expression(const MinicC0Program *program,
                                                       MinicExpressionId expression_id,
                                                       int64_t *value);
'''
if old_eval_decl in text:
    text = text.replace(old_eval_decl, '', 1)
path.write_text(text)

# Replace parser DataLayout access through TargetInfo in the known frontend consumers.
for relative in ["src/frontend/parser_core.c", "src/frontend/parser_type_query.c"]:
    path = root / relative
    text = path.read_text()
    text = text.replace(
        "parser->data_layout", "minic_target_info_data_layout(parser->target_info)")
    path.write_text(text)

# Token ICE sizeof remains, but uses TargetInfo rather than bypassing GNU type-query semantics.
path = root / "src/frontend/parser_core.c"
text = path.read_text()
old = '''    size_t measured_size;
    size_t measured_alignment;

    if (parser == NULL || size == NULL ||
        !minic_data_layout_type(
            minic_target_info_data_layout(parser->target_info), parser->program, type, &measured_size, &measured_alignment)) {
        return false;
    }
    (void)measured_alignment;
    *size = (uint64_t)measured_size;
'''
new = '''    size_t measured_size;

    if (parser == NULL || size == NULL ||
        !minic_target_info_sizeof_type(parser->target_info, parser->program, type, &measured_size)) {
        return false;
    }
    *size = (uint64_t)measured_size;
'''
text = replace_once(text, old, new, "token-ice-target-sizeof")
path.write_text(text)

# Replace the old untyped AST evaluator and move consumers to ConstEval v0.
path = root / "src/frontend/parser_expression.c"
text = path.read_text()
start = text.find("bool minic_parser_evaluate_integer_constant_expression(")
end = text.find("static bool parse_builtin_types_compatible_p(", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate old AST integer evaluator")
text = text[:start] + text[end:]
old = '''    int64_t condition_value;

    if (!generic_token_text_equals(parser, "__builtin_choose_expr")) {
'''
new = '''    MinicConstValue condition_value;
    bool condition_is_zero;

    if (!generic_token_text_equals(parser, "__builtin_choose_expr")) {
'''
text = replace_once(text, old, new, "choose-expr-typed-value")
old = '''    if (!minic_parser_evaluate_integer_constant_expression(
            parser->program, condition_id, &condition_value)) {
        minic_parser_error(
            parser, "__builtin_choose_expr condition must be an integer constant expression");
        return false;
    }
    *expression_id = condition_value != 0 ? when_true_id : when_false_id;
'''
new = '''    if (!minic_const_eval_integer(
            parser->program, parser->target_info, condition_id, &condition_value) ||
        !minic_const_value_is_zero(
            parser->program, parser->target_info, &condition_value, &condition_is_zero)) {
        minic_parser_error(
            parser, "__builtin_choose_expr condition must be an integer constant expression");
        return false;
    }
    *expression_id = condition_is_zero ? when_false_id : when_true_id;
'''
text = replace_once(text, old, new, "choose-expr-typed-eval")
old = '''    int64_t constant_value;
    bool is_constant;
'''
new = '''    MinicConstValue constant_value;
    bool is_constant;
'''
text = replace_once(text, old, new, "constant-p-typed-value")
old = '''    is_constant = minic_parser_evaluate_integer_constant_expression(
        parser->program, operand_id, &constant_value);
    (void)constant_value;
'''
new = '''    is_constant = minic_const_eval_integer(
        parser->program, parser->target_info, operand_id, &constant_value);
'''
text = replace_once(text, old, new, "constant-p-typed-eval")

# sizeof expression validation goes through TargetInfo so GNU sizeof(void/function) stays out of DataLayout.
old = '''    if (!type_is_complete_object(parser->program, measured_type)) {
        minic_parser_error(parser, "sizeof requires a complete object type");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
'''
new = '''    {
        size_t measured_size;

        if (!minic_target_info_sizeof_type(
                parser->target_info, parser->program, measured_type, &measured_size)) {
            minic_parser_error(parser, "sizeof requires a supported complete type");
            return false;
        }
        (void)measured_size;
    }

    (void)memset(&expression, 0, sizeof(expression));
'''
text = replace_once(text, old, new, "expression-target-sizeof")
path.write_text(text)

# Static assertions use typed ConstEval.
path = root / "src/frontend/parser_static_assert.c"
text = path.read_text()
text = replace_once(
    text,
    '''    int64_t condition_value;
''',
    '''    MinicConstValue condition_value;
    bool condition_is_zero;
''',
    "static-assert-value",
)
old = '''    if (condition == NULL || !minic_type_is_integer(condition->type) ||
        !minic_parser_evaluate_integer_constant_expression(
            parser->program, condition_id, &condition_value)) {
'''
new = '''    if (condition == NULL || !minic_type_is_integer(condition->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, condition_id, &condition_value) ||
        !minic_const_value_is_zero(
            parser->program, parser->target_info, &condition_value, &condition_is_zero)) {
'''
text = replace_once(text, old, new, "static-assert-typed-eval")
text = replace_once(
    text,
    '''    if (condition_value == 0) {
''',
    '''    if (condition_is_zero) {
''',
    "static-assert-zero",
)
path.write_text(text)

# Bit-field width is the first declaration-time consumer migrated from token ICE to expression AST + typed ConstEval.
path = root / "src/frontend/parser_record.c"
text = path.read_text()
old = '''    if (parser->current.kind == MINIC_TOKEN_COLON) {
        int64_t bit_width;

        if (!minic_type_is_integer(base_type)) {
            minic_parser_error(parser, "unnamed bit-field requires an integer type");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_integer_constant_expression_value(parser, &bit_width)) {
            return false;
        }
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_COLON) {
        MinicConstValue width_value;
        MinicExpressionId width_expression;
        int64_t bit_width;

        if (!minic_type_is_integer(base_type)) {
            minic_parser_error(parser, "unnamed bit-field requires an integer type");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &width_expression, 0U) ||
            !minic_const_eval_integer(
                parser->program, parser->target_info, width_expression, &width_value) ||
            !minic_const_value_as_int64(
                parser->program, parser->target_info, &width_value, &bit_width)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "bit-field width must be an integer constant expression");
            }
            return false;
        }
'''
text = replace_once(text, old, new, "bitfield-typed-consteval")
path.write_text(text)

# Thread TargetInfo into the parser root.
path = root / "src/frontend/parser_function.c"
text = path.read_text()
text = replace_once(
    text,
    '''    parser.data_layout = minic_default_data_layout();
''',
    '''    parser.target_info = minic_default_target_info();
''',
    "parser-init-target-info",
)
path.write_text(text)

# Add the new modules to the compiler build.
path = root / "Makefile"
text = path.read_text()
text = replace_once(
    text,
    '''\tsrc/frontend/cast_normalization.c \\
\tsrc/frontend/ast_function.c \\
''',
    '''\tsrc/frontend/cast_normalization.c \\
\tsrc/frontend/const_eval.c \\
\tsrc/frontend/ast_function.c \\
''',
    "makefile-consteval",
)
text = replace_once(
    text,
    '''\tsrc/target/data_layout.c \\
\tsrc/target/riscv64/layout.c \\
''',
    '''\tsrc/target/data_layout.c \\
\tsrc/target/target_info.c \\
\tsrc/target/riscv64/layout.c \\
''',
    "makefile-target-info",
)
path.write_text(text)

# Linux-shaped regression: choose_expr selects the constant arm while the unselected arm references runtime locals.
(root / "tests/compiler/c0/gnu_choose_expr_bitfield.c").write_text(r'''static int linux_build_bug_shape(unsigned long offset, unsigned long size) {
    return (int)sizeof(struct {
        int : (-!!(__builtin_choose_expr(
            (sizeof(int) ==
             sizeof(*(8 ? ((void *)((long)((offset) > (size - 1)) * 0l)) : (int *)8))),
            (offset) > (size - 1),
            0)));
        int payload;
    });
}

_Static_assert(sizeof(void) == 1, "GNU sizeof(void) must remain byte-sized");
_Static_assert((~0U) == 0xffffffffU, "typed consteval keeps unsigned-int width");
_Static_assert((unsigned long long)(~0U) == 0x00000000ffffffffULL,
               "integer conversion must preserve source width before widening");

int main(void) {
    return linux_build_bug_shape(2UL, 1UL) > 0 ? 0 : 1;
}
''')

(root / "tests/compiler/c0/run-gnu-choose-expr-bitfield.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-choose-expr-bitfield
source="$root/tests/compiler/c0/gnu_choose_expr_bitfield.c"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/probe.i"
"$minic" -S "$work/probe.i" -o "$work/probe.s"
test -s "$work/probe.s"
grep -F 'linux_build_bug_shape:' "$work/probe.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_choose_expr_bitfield bitfield=typed-ast-consteval choose-expr=selected-arm sizeof-void=gnu-byte target-info=1 unsigned-width=preserved'
''')

path = root / "tools/dev/pr76-focused.sh"
text = path.read_text()
text = replace_once(
    text,
    '''sh tests/compiler/c0/run-static-assert-declaration.sh
''',
    '''sh tests/compiler/c0/run-static-assert-declaration.sh
sh tests/compiler/c0/run-gnu-choose-expr-bitfield.sh
''',
    "focused-typed-consteval",
)
path.write_text(text)
