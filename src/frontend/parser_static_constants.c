#include "frontend/parser_static_constants.h"
#include "frontend/const_eval.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Static initializer constants, symbol and subobject relocations. */

static bool token_starts_type_name(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_KW_CONST || kind == MINIC_TOKEN_KW_CHAR ||
           kind == MINIC_TOKEN_KW_FLOAT || kind == MINIC_TOKEN_KW_DOUBLE ||
           kind == MINIC_TOKEN_KW_INT || kind == MINIC_TOKEN_KW_LONG ||
           kind == MINIC_TOKEN_KW_SIGNED || kind == MINIC_TOKEN_KW_UNSIGNED ||
           kind == MINIC_TOKEN_KW_VOID || kind == MINIC_TOKEN_KW_STRUCT ||
           kind == MINIC_TOKEN_IDENTIFIER;
}

bool minic_parser_parse_zero_pointer_constant(MinicParser *parser) {
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        int value;

        if (!minic_parser_parse_integer_value(parser, &value)) {
            return false;
        }
        if (value != 0) {
            minic_parser_error(parser, "static pointer initializer must be null");
            return false;
        }
        return true;
    }

    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicType cast_type;

        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (token_starts_type_name(parser->current.kind)) {
            if (!minic_parser_parse_type_name(parser, &cast_type) ||
                !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after null cast") ||
                !minic_type_is_pointer(cast_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "static null cast requires a pointer type");
                }
                return false;
            }
            return minic_parser_parse_zero_pointer_constant(parser);
        }
        if (!minic_parser_parse_zero_pointer_constant(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after null constant")) {
            return false;
        }
        return true;
    }

    minic_parser_error(parser, "static pointer initializer must be null");
    return false;
}

static bool static_pointer_expression_has_explicit_cast(const MinicC0Program *program,
                                                        MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(program, expression->value.unary.operand);
        if (expression->kind == MINIC_EXPRESSION_CAST && minic_type_is_pointer(expression->type) &&
            operand != NULL && minic_type_is_pointer(operand->type)) {
            return true;
        }
        expression = operand;
    }
    return false;
}

bool minic_parser_static_symbol_function_relocation(const MinicC0Program *program,
                                                      MinicExpressionId expression_id,
                                                      MinicFunctionId *target_function_id) {
    const MinicExpression *expression;
    const MinicExpression *addressed;
    MinicFunctionId function_id;

    if (program == NULL || target_function_id == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression = minic_c0_program_expression(program, expression->value.unary.operand);
    }
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_FUNCTION) {
        function_id = expression->value.function_id;
    } else if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        addressed = minic_c0_program_expression(program, expression->value.unary.operand);
        if (addressed == NULL || addressed->kind != MINIC_EXPRESSION_FUNCTION) {
            return false;
        }
        function_id = addressed->value.function_id;
    } else {
        return false;
    }
    if (function_id == MINIC_FUNCTION_INVALID ||
        minic_c0_program_function(program, function_id) == NULL) {
        return false;
    }
    *target_function_id = function_id;
    return true;
}

static bool static_pointer_offset_bytes(const MinicParser *parser,
                                        MinicType pointee_type,
                                        MinicExpressionId offset_expression_id,
                                        bool subtract,
                                        int64_t *byte_offset) {
    MinicConstValue constant;
    int64_t count;
    size_t size;
    size_t alignment;
    uint64_t magnitude;
    uint64_t limit;
    uint64_t product;

    if (parser == NULL || byte_offset == NULL ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, offset_expression_id, &constant) ||
        !minic_const_value_as_int64(parser->program, parser->target_info, &constant, &count) ||
        !minic_data_layout_type(minic_target_info_data_layout(parser->target_info),
                                parser->program,
                                pointee_type,
                                &size,
                                &alignment) ||
        size == 0U) {
        return false;
    }
    (void)alignment;
    if (count >= 0) {
        magnitude = (uint64_t)count;
        limit = (uint64_t)INT64_MAX;
    } else {
        magnitude = (uint64_t)(-(count + 1)) + UINT64_C(1);
        limit = (uint64_t)INT64_MAX + UINT64_C(1);
    }
    if (magnitude != 0U && size > limit / magnitude) {
        return false;
    }
    product = magnitude * size;
    if (count < 0) {
        *byte_offset = product == (uint64_t)INT64_MAX + UINT64_C(1) ? INT64_MIN : -(int64_t)product;
    } else {
        *byte_offset = (int64_t)product;
    }
    if (!subtract) {
        return true;
    }
    if (*byte_offset == INT64_MIN) {
        return false;
    }
    *byte_offset = -*byte_offset;
    return true;
}

bool minic_parser_static_symbol_add_pointer_offset(int64_t base, int64_t delta, int64_t *result) {
    if (result == NULL || (delta > 0 && base > INT64_MAX - delta) ||
        (delta < 0 && base < INT64_MIN - delta)) {
        return false;
    }
    *result = base + delta;
    return true;
}

static bool static_object_address_relocation_target(const MinicParser *parser,
                                                    MinicExpressionId expression_id,
                                                    MinicGlobalObjectId *target_object_id,
                                                    int64_t *target_byte_addend) {
    const MinicExpression *expression;
    const MinicExpression *addressed;
    MinicGlobalObjectId object_id;
    int64_t byte_addend;

    if (parser == NULL || target_object_id == NULL || target_byte_addend == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(parser->program, expression_id);
    }
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        const MinicExpression *left;
        MinicType pointee_type;
        int64_t delta;

        left = minic_c0_program_expression(parser->program, expression->value.binary.left);
        if (left == NULL || !minic_type_pointee(left->type, &pointee_type) ||
            !static_object_address_relocation_target(
                parser, expression->value.binary.left, &object_id, &byte_addend) ||
            !static_pointer_offset_bytes(parser,
                                         pointee_type,
                                         expression->value.binary.right,
                                         expression->value.binary.operator_kind ==
                                             MINIC_BINARY_SUBTRACT,
                                         &delta) ||
            !minic_parser_static_symbol_add_pointer_offset(byte_addend, delta, &byte_addend)) {
            return false;
        }
        *target_object_id = object_id;
        *target_byte_addend = byte_addend;
        return true;
    }
    if (expression->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    addressed = minic_c0_program_expression(parser->program, expression->value.unary.operand);
    if (addressed == NULL) {
        return false;
    }
    byte_addend = 0;
    if (addressed->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        object_id = addressed->value.global_object_id;
    } else if (addressed->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        const MinicGlobalObject *object;
        const MinicArrayType *array_type;

        base = minic_c0_program_expression(parser->program, addressed->value.subscript.base);
        if (base == NULL || base->kind != MINIC_EXPRESSION_GLOBAL_OBJECT ||
            !static_pointer_offset_bytes(
                parser, addressed->type, addressed->value.subscript.index, false, &byte_addend)) {
            return false;
        }
        object_id = base->value.global_object_id;
        object = minic_c0_program_global_object(parser->program, object_id);
        array_type = object != NULL && minic_type_is_array(object->type)
                         ? minic_c0_program_array_type(parser->program, object->type.array_type_id)
                         : NULL;
        if (array_type == NULL || !minic_type_equal(array_type->element_type, addressed->type)) {
            return false;
        }
    } else {
        return false;
    }
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        minic_c0_program_global_object(parser->program, object_id) == NULL) {
        return false;
    }
    *target_object_id = object_id;
    *target_byte_addend = byte_addend;
    return true;
}

typedef struct MinicStaticObjectRelocationTarget {
    MinicGlobalObjectId object_id;
    size_t member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t member_depth;
    int64_t byte_addend;
} MinicStaticObjectRelocationTarget;

typedef struct MinicStaticPointerInitializer {
    bool has_relocation;
    bool relocation_is_function;
    bool relocation_is_label;
    bool has_explicit_pointer_cast;
    uint64_t bits;
    MinicFunctionId function_id;
    MinicStatementId label_statement_id;
    MinicStaticObjectRelocationTarget relocation_target;
} MinicStaticPointerInitializer;

typedef struct MinicStaticArraySlot {
    uint64_t integer_bits;
    MinicStaticPointerInitializer pointer_initializer;
} MinicStaticArraySlot;

static bool static_object_subobject_relocation_path(const MinicParser *parser,
                                                    MinicExpressionId expression_id,
                                                    MinicStaticObjectRelocationTarget *target) {
    const MinicExpression *expression;

    if (parser == NULL || target == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        if (expression->value.global_object_id == MINIC_GLOBAL_OBJECT_INVALID ||
            minic_c0_program_global_object(parser->program, expression->value.global_object_id) ==
                NULL) {
            return false;
        }
        (void)memset(target, 0, sizeof(*target));
        target->object_id = expression->value.global_object_id;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        MinicExpressionId base_id;

        base_id = expression->value.member.base;
        base = minic_c0_program_expression(parser->program, base_id);
        if (base == NULL || base->kind != MINIC_EXPRESSION_ADDRESS_OF) {
            return false;
        }
        base_id = base->value.unary.operand;
        if (!static_object_subobject_relocation_path(parser, base_id, target) ||
            target->member_depth == MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH) {
            return false;
        }
        target->member_indices[target->member_depth++] = expression->value.member.field_index;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        MinicArrayObjectInfo array_info;
        int64_t delta;

        base = minic_c0_program_expression(parser->program, expression->value.subscript.base);
        if (base == NULL ||
            !minic_c0_expression_array_object_info(parser->program, base, &array_info) ||
            !static_object_subobject_relocation_path(
                parser, expression->value.subscript.base, target) ||
            !static_pointer_offset_bytes(parser,
                                         array_info.element_type,
                                         expression->value.subscript.index,
                                         false,
                                         &delta) ||
            !minic_parser_static_symbol_add_pointer_offset(target->byte_addend, delta, &target->byte_addend)) {
            return false;
        }
        return true;
    }
    return false;
}

bool minic_parser_static_symbol_object_relocation_path(const MinicParser *parser,
                                                  MinicExpressionId expression_id,
                                                  MinicStaticObjectRelocationTarget *target) {
    const MinicC0Program *program;
    const MinicExpression *expression;

    if (parser == NULL || target == NULL) {
        return false;
    }
    program = parser->program;
    if (static_object_address_relocation_target(
            parser, expression_id, &target->object_id, &target->byte_addend)) {
        target->member_depth = 0U;
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
    while (expression != NULL && (expression->kind == MINIC_EXPRESSION_CAST ||
                                  expression->kind == MINIC_EXPRESSION_BITCAST ||
                                  expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(program, expression_id);
    }
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        const MinicExpression *left;
        MinicType pointee_type;
        int64_t delta;

        left = minic_c0_program_expression(program, expression->value.binary.left);
        if (left != NULL && minic_type_pointee(left->type, &pointee_type) &&
            minic_parser_static_symbol_object_relocation_path(parser, expression->value.binary.left, target) &&
            static_pointer_offset_bytes(parser,
                                        pointee_type,
                                        expression->value.binary.right,
                                        expression->value.binary.operator_kind ==
                                            MINIC_BINARY_SUBTRACT,
                                        &delta) &&
            minic_parser_static_symbol_add_pointer_offset(target->byte_addend, delta, &target->byte_addend)) {
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
            const MinicExpression *right;

            right = minic_c0_program_expression(program, expression->value.binary.right);
            if (right != NULL && minic_type_pointee(right->type, &pointee_type) &&
                minic_parser_static_symbol_object_relocation_path(
                    parser, expression->value.binary.right, target) &&
                static_pointer_offset_bytes(
                    parser, pointee_type, expression->value.binary.left, false, &delta) &&
                minic_parser_static_symbol_add_pointer_offset(target->byte_addend, delta, &target->byte_addend)) {
                return true;
            }
        }
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        return static_object_subobject_relocation_path(
            parser, expression->value.unary.operand, target);
    }
    {
        MinicArrayObjectInfo array_info;

        if (minic_c0_expression_array_object_info(program, expression, &array_info)) {
            return static_object_subobject_relocation_path(parser, expression_id, target);
        }
    }
    return false;
}

static bool static_pointer_mask_bits(const MinicParser *parser, uint64_t value, uint64_t *bits) {
    const MinicDataLayout *layout;
    unsigned int pointer_bits;

    if (parser == NULL || bits == NULL) {
        return false;
    }
    layout = minic_target_info_data_layout(parser->target_info);
    if (layout == NULL || layout->pointer_size == 0U || layout->pointer_size > 8U) {
        return false;
    }
    pointer_bits = (unsigned int)(layout->pointer_size * 8U);
    *bits = value;
    if (pointer_bits < 64U) {
        *bits &= (UINT64_C(1) << pointer_bits) - UINT64_C(1);
    }
    return true;
}

static bool static_pointer_integer_constant_bits(const MinicParser *parser,
                                                 MinicExpressionId expression_id,
                                                 uint64_t *bits) {
    const MinicExpression *expression;
    const MinicExpression *operand;
    MinicConstValue constant;

    if (parser == NULL || bits == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_CAST ||
        !minic_type_is_pointer(expression->type)) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, expression->value.unary.operand);
    if (operand == NULL || !minic_type_is_integer(operand->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression->value.unary.operand, &constant)) {
        return false;
    }
    return static_pointer_mask_bits(parser, constant.bits, bits);
}

static bool static_pointer_absolute_offset_bits(const MinicParser *parser,
                                                MinicType pointee_type,
                                                MinicExpressionId offset_expression_id,
                                                bool subtract,
                                                uint64_t *byte_offset_bits) {
    MinicConstValue constant;
    int64_t signed_count;
    uint64_t count_bits;
    uint64_t product;
    size_t size;

    if (parser == NULL || byte_offset_bits == NULL ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, offset_expression_id, &constant) ||
        !minic_target_info_sizeof_type(parser->target_info, parser->program, pointee_type, &size) ||
        size == 0U) {
        return false;
    }
    if (minic_const_value_as_int64(
            parser->program, parser->target_info, &constant, &signed_count)) {
        count_bits = (uint64_t)signed_count;
    } else {
        unsigned int width;

        if (!minic_target_info_integer_width(
                parser->target_info, parser->program, constant.type, &width) ||
            width == 0U || width > 64U) {
            return false;
        }
        count_bits = constant.bits;
    }
    product = count_bits * (uint64_t)size;
    if (subtract) {
        product = UINT64_C(0) - product;
    }
    return static_pointer_mask_bits(parser, product, byte_offset_bits);
}

static bool static_absolute_pointer_expression_bits(const MinicParser *parser,
                                                    MinicExpressionId expression_id,
                                                    uint64_t *bits);

static bool static_absolute_lvalue_address_bits(const MinicParser *parser,
                                                MinicExpressionId expression_id,
                                                uint64_t *bits) {
    const MinicExpression *expression;

    if (parser == NULL || bits == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_DEREFERENCE) {
        return static_absolute_pointer_expression_bits(
            parser, expression->value.unary.operand, bits);
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        const MinicRecord *record;
        size_t field_offset;
        uint64_t base_bits;

        base = minic_c0_program_expression(parser->program, expression->value.member.base);
        record = minic_c0_program_record(parser->program, expression->value.member.record_id);
        if (base == NULL || record == NULL ||
            !minic_data_layout_record_field_offset(
                minic_target_info_data_layout(parser->target_info),
                parser->program,
                record,
                expression->value.member.field_index,
                &field_offset)) {
            return false;
        }
        if (minic_type_is_pointer(base->type)) {
            if (!static_absolute_pointer_expression_bits(
                    parser, expression->value.member.base, &base_bits)) {
                return false;
            }
        } else if (!static_absolute_lvalue_address_bits(
                       parser, expression->value.member.base, &base_bits)) {
            return false;
        }
        return static_pointer_mask_bits(parser, base_bits + (uint64_t)field_offset, bits);
    }
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        uint64_t base_bits;
        uint64_t delta_bits;

        base = minic_c0_program_expression(parser->program, expression->value.subscript.base);
        if (base == NULL) {
            return false;
        }
        if (minic_type_is_pointer(base->type)) {
            if (!static_absolute_pointer_expression_bits(
                    parser, expression->value.subscript.base, &base_bits)) {
                return false;
            }
        } else if (!static_absolute_lvalue_address_bits(
                       parser, expression->value.subscript.base, &base_bits)) {
            return false;
        }
        if (!static_pointer_absolute_offset_bits(parser,
                                                 expression->type,
                                                 expression->value.subscript.index,
                                                 false,
                                                 &delta_bits)) {
            return false;
        }
        return static_pointer_mask_bits(parser, base_bits + delta_bits, bits);
    }
    return false;
}

static bool static_absolute_pointer_expression_bits(const MinicParser *parser,
                                                    MinicExpressionId expression_id,
                                                    uint64_t *bits) {
    const MinicExpression *expression;

    if (parser == NULL || bits == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_pointer(expression->type)) {
        return false;
    }
    if (static_pointer_integer_constant_bits(parser, expression_id, bits)) {
        return true;
    }
    if ((expression->kind == MINIC_EXPRESSION_CAST ||
         expression->kind == MINIC_EXPRESSION_BITCAST ||
         expression->kind == MINIC_EXPRESSION_CONVERSION)) {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(parser->program, expression->value.unary.operand);
        if (operand != NULL && minic_type_is_pointer(operand->type)) {
            return static_absolute_pointer_expression_bits(
                parser, expression->value.unary.operand, bits);
        }
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        return static_absolute_lvalue_address_bits(
            parser, expression->value.unary.operand, bits);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        const MinicExpression *left;
        const MinicExpression *right;
        MinicExpressionId pointer_id;
        MinicExpressionId offset_id;
        MinicType pointee_type;
        uint64_t base_bits;
        uint64_t delta_bits;
        bool subtract;

        left = minic_c0_program_expression(parser->program, expression->value.binary.left);
        right = minic_c0_program_expression(parser->program, expression->value.binary.right);
        pointer_id = MINIC_EXPRESSION_INVALID;
        offset_id = MINIC_EXPRESSION_INVALID;
        subtract = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
        if (left != NULL && minic_type_is_pointer(left->type)) {
            pointer_id = expression->value.binary.left;
            offset_id = expression->value.binary.right;
        } else if (!subtract && right != NULL && minic_type_is_pointer(right->type)) {
            pointer_id = expression->value.binary.right;
            offset_id = expression->value.binary.left;
        }
        if (pointer_id == MINIC_EXPRESSION_INVALID ||
            !minic_type_pointee(
                minic_c0_program_expression(parser->program, pointer_id)->type, &pointee_type) ||
            !static_absolute_pointer_expression_bits(parser, pointer_id, &base_bits) ||
            !static_pointer_absolute_offset_bits(
                parser, pointee_type, offset_id, subtract, &delta_bits)) {
            return false;
        }
        return static_pointer_mask_bits(parser, base_bits + delta_bits, bits);
    }
    return false;
}

static void static_pointer_initializer_reset(MinicStaticPointerInitializer *initializer) {
    if (initializer == NULL) {
        return;
    }
    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->function_id = MINIC_FUNCTION_INVALID;
    initializer->label_statement_id = MINIC_STATEMENT_INVALID;
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
}

static bool static_pointer_initializers_same_value(const MinicStaticPointerInitializer *left,
                                                   const MinicStaticPointerInitializer *right) {
    size_t index;

    if (left == NULL || right == NULL || left->has_relocation != right->has_relocation) {
        return false;
    }
    if (!left->has_relocation) {
        return left->bits == right->bits;
    }
    if (left->relocation_is_function != right->relocation_is_function ||
        left->relocation_is_label != right->relocation_is_label) {
        return false;
    }
    if (left->relocation_is_function) {
        return left->function_id == right->function_id;
    }
    if (left->relocation_is_label) {
        return left->label_statement_id == right->label_statement_id;
    }
    if (left->relocation_target.object_id != right->relocation_target.object_id ||
        left->relocation_target.member_depth != right->relocation_target.member_depth ||
        left->relocation_target.byte_addend != right->relocation_target.byte_addend) {
        return false;
    }
    for (index = 0U; index < left->relocation_target.member_depth; ++index) {
        if (left->relocation_target.member_indices[index] !=
            right->relocation_target.member_indices[index]) {
            return false;
        }
    }
    return true;
}

static bool static_pointer_initializer_from_expression(MinicParser *parser,
                                                       MinicExpressionId expression_id,
                                                       MinicStaticPointerInitializer *initializer) {
    const MinicExpression *expression;

    if (parser == NULL || initializer == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    initializer->has_explicit_pointer_cast =
        initializer->has_explicit_pointer_cast ||
        static_pointer_expression_has_explicit_cast(parser->program, expression_id);
    if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, expression_id)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS) {
        initializer->has_relocation = true;
        initializer->relocation_is_label = true;
        initializer->label_statement_id = expression->value.label_statement_id;
        return true;
    }
    if (minic_parser_static_symbol_function_relocation(
            parser->program, expression_id, &initializer->function_id)) {
        initializer->has_relocation = true;
        initializer->relocation_is_function = true;
        return true;
    }
    if (minic_parser_static_symbol_object_relocation_path(
            parser, expression_id, &initializer->relocation_target)) {
        initializer->has_relocation = true;
        return true;
    }
    if (static_pointer_integer_constant_bits(parser, expression_id, &initializer->bits)) {
        return true;
    }
    if (static_absolute_pointer_expression_bits(parser, expression_id, &initializer->bits)) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        const MinicExpression *left;
        const MinicExpression *right;
        const MinicExpression *pointer_expression;
        MinicExpressionId pointer_id;
        MinicExpressionId offset_id;
        MinicStaticPointerInitializer base_initializer;
        MinicType pointee_type;
        uint64_t delta_bits;
        uint64_t result_bits;
        bool subtract;

        left = minic_c0_program_expression(parser->program, expression->value.binary.left);
        right = minic_c0_program_expression(parser->program, expression->value.binary.right);
        pointer_expression = NULL;
        pointer_id = MINIC_EXPRESSION_INVALID;
        offset_id = MINIC_EXPRESSION_INVALID;
        subtract = expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
        if (left != NULL && minic_type_is_pointer(left->type)) {
            pointer_expression = left;
            pointer_id = expression->value.binary.left;
            offset_id = expression->value.binary.right;
        } else if (!subtract && right != NULL && minic_type_is_pointer(right->type)) {
            pointer_expression = right;
            pointer_id = expression->value.binary.right;
            offset_id = expression->value.binary.left;
        }
        static_pointer_initializer_reset(&base_initializer);
        if (pointer_expression != NULL &&
            minic_type_pointee(pointer_expression->type, &pointee_type) &&
            static_pointer_initializer_from_expression(parser, pointer_id, &base_initializer) &&
            !base_initializer.has_relocation &&
            static_pointer_absolute_offset_bits(
                parser, pointee_type, offset_id, subtract, &delta_bits) &&
            static_pointer_mask_bits(parser, base_initializer.bits + delta_bits, &result_bits)) {
            *initializer = base_initializer;
            initializer->bits = result_bits;
            return true;
        }
    }
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value) {
        MinicConstValue condition_constant;
        int64_t condition_value;
        MinicExpressionId selected_id;

        if (minic_const_eval_integer(parser->program,
                                     parser->target_info,
                                     expression->value.conditional.condition,
                                     &condition_constant) &&
            minic_const_value_as_int64(
                parser->program, parser->target_info, &condition_constant, &condition_value)) {
            selected_id = condition_value != 0 ? expression->value.conditional.when_true
                                               : expression->value.conditional.when_false;
            return static_pointer_initializer_from_expression(parser, selected_id, initializer);
        }
        {
            MinicStaticPointerInitializer when_true;
            MinicStaticPointerInitializer when_false;

            static_pointer_initializer_reset(&when_true);
            static_pointer_initializer_reset(&when_false);
            if (!static_pointer_initializer_from_expression(
                    parser, expression->value.conditional.when_true, &when_true) ||
                !static_pointer_initializer_from_expression(
                    parser, expression->value.conditional.when_false, &when_false) ||
                !static_pointer_initializers_same_value(&when_true, &when_false)) {
                return false;
            }
            when_true.has_explicit_pointer_cast =
                when_true.has_explicit_pointer_cast || when_false.has_explicit_pointer_cast;
            *initializer = when_true;
            return true;
        }
    }
    return false;
}

static bool static_pointer_initializer_type_compatible(const MinicParser *parser,
                                                       MinicType target_type,
                                                       MinicExpressionId expression_id) {
    const MinicExpression *source;

    if (parser == NULL) {
        return false;
    }
    if (minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        return true;
    }
    source = minic_c0_program_expression(parser->program, expression_id);
    return source != NULL && minic_type_gnu_pointer_sign_compatible(target_type, source->type);
}

bool minic_parser_static_symbol_pointer_initializer(MinicParser *parser,
                                             MinicType target_type,
                                             MinicStaticPointerInitializer *initializer) {
    MinicExpressionId expression_id;

    if (parser == NULL || initializer == NULL || !minic_type_is_pointer(target_type) ||
        !minic_parser_parse_expression(parser, &expression_id, 0U) ||
        !minic_parser_apply_array_decay(parser, expression_id, &expression_id)) {
        return false;
    }
    static_pointer_initializer_reset(initializer);
    if (!static_pointer_initializer_type_compatible(parser, target_type, expression_id)) {
        minic_parser_error(parser, "static pointer initializer type mismatch");
        return false;
    }
    if (static_pointer_initializer_from_expression(parser, expression_id, initializer)) {
        return true;
    }
    minic_parser_error(parser,
                       "static pointer initializer requires a null or static symbol address "
                       "constant");
    return false;
}

static bool static_floating_constant_value(MinicParser *parser,
                                           MinicExpressionId expression_id,
                                           double *value) {
    const MinicExpression *expression;

    if (parser == NULL || parser->program == NULL || value == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_FLOATING) {
        if (minic_type_is_float(expression->type)) {
            uint32_t bits;
            float floating_value;

            bits = (uint32_t)expression->value.floating_bits;
            (void)memcpy(&floating_value, &bits, sizeof(floating_value));
            *value = (double)floating_value;
            return true;
        }
        if (minic_type_is_double(expression->type)) {
            (void)memcpy(value, &expression->value.floating_bits, sizeof(*value));
            return true;
        }
        return false;
    }
    if (minic_type_is_integer(expression->type)) {
        MinicConstValue constant;

        if (!minic_const_eval_integer(
                parser->program, parser->target_info, expression_id, &constant)) {
            return false;
        }
        if (minic_type_is_unsigned_integer(constant.type)) {
            MinicConstValue widened;

            if (!minic_const_value_convert_integer(parser->program,
                                                   parser->target_info,
                                                   &constant,
                                                   minic_type_unsigned_long_long(),
                                                   &widened)) {
                return false;
            }
            *value = (double)widened.bits;
            return true;
        } else {
            int64_t signed_value;

            if (!minic_const_value_as_int64(
                    parser->program, parser->target_info, &constant, &signed_value)) {
                return false;
            }
            *value = (double)signed_value;
            return true;
        }
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_PLUS ||
         expression->value.unary.operator_kind == MINIC_UNARY_NEGATE)) {
        if (!static_floating_constant_value(
                parser, expression->value.unary.operand, value)) {
            return false;
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) {
            *value = -*value;
        }
        return true;
    }
    return false;
}

bool minic_parser_static_symbol_floating_initializer_bits(MinicParser *parser,
                                                   MinicType target_type,
                                                   uint64_t *bits) {
    MinicExpressionId expression_id;
    double value;

    _Static_assert(sizeof(float) == sizeof(uint32_t), "MiniC requires binary32 host float");
    _Static_assert(sizeof(double) == sizeof(uint64_t), "MiniC requires binary64 host double");

    if (parser == NULL || bits == NULL ||
        (!minic_type_is_float(target_type) && !minic_type_is_double(target_type)) ||
        !minic_parser_parse_expression(parser, &expression_id, 0U) ||
        !static_floating_constant_value(parser, expression_id, &value)) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "static floating initializer requires an arithmetic constant");
        }
        return false;
    }
    if (minic_type_is_float(target_type)) {
        float converted;
        uint32_t converted_bits;

        converted = (float)value;
        (void)memcpy(&converted_bits, &converted, sizeof(converted_bits));
        *bits = (uint64_t)converted_bits;
    } else {
        (void)memcpy(bits, &value, sizeof(value));
    }
    return true;
}
