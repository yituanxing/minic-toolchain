#include "frontend/parser_internal.h"
#include "frontend/declaration_sema.h"
#include "frontend/initializer.h"
#include "frontend/semantic_snapshot.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool apply_static_object_metadata(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         const char *section_name,
                                         size_t section_name_length,
                                         bool has_section,
                                         size_t explicit_alignment) {
    MinicDeclarationExternalObjectAttributes attributes;

    if (parser == NULL || object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        return false;
    }
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.section_name = section_name;
    attributes.section_name_length = section_name_length;
    attributes.explicit_alignment = explicit_alignment;
    attributes.has_section = has_section;
    return minic_declaration_apply_object_attributes(parser->program, object_id, &attributes);
}

MinicGlobalObjectId minic_parser_find_global_object_entity(const MinicParser *parser,
                                                           MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return MINIC_GLOBAL_OBJECT_INVALID;
    }
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, index);
        if (object != NULL && object->name_length == name_length &&
            memcmp(object->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_GLOBAL_OBJECT_INVALID;
}

MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,
                                                    MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    const MinicGlobalObject *object;

    object_id = minic_parser_find_scoped_global_object(parser, name_span);
    if (object_id != MINIC_GLOBAL_OBJECT_INVALID) {
        return object_id;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    object = object_id == MINIC_GLOBAL_OBJECT_INVALID
                 ? NULL
                 : minic_c0_program_global_object(parser->program, object_id);
    return object != NULL && !object->is_block_scope_extern_only ? object_id
                                                                 : MINIC_GLOBAL_OBJECT_INVALID;
}

MinicFixedRegisterBindingId minic_parser_find_fixed_register_binding(const MinicParser *parser,
                                                                     MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return MINIC_FIXED_REGISTER_BINDING_INVALID;
    }
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->fixed_register_binding_count; ++index) {
        const MinicFixedRegisterBinding *binding;

        binding = minic_c0_program_fixed_register_binding(parser->program, index);
        if (binding != NULL && !binding->is_local && binding->name_length == name_length &&
            memcmp(binding->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_FIXED_REGISTER_BINDING_INVALID;
}

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

static bool static_function_address_relocation_target(const MinicC0Program *program,
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

static bool static_add_pointer_offset(int64_t base, int64_t delta, int64_t *result) {
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
            !static_add_pointer_offset(byte_addend, delta, &byte_addend)) {
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
            !static_add_pointer_offset(target->byte_addend, delta, &target->byte_addend)) {
            return false;
        }
        return true;
    }
    return false;
}

static bool static_object_address_relocation_path(const MinicParser *parser,
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
            static_object_address_relocation_path(parser, expression->value.binary.left, target) &&
            static_pointer_offset_bytes(parser,
                                        pointee_type,
                                        expression->value.binary.right,
                                        expression->value.binary.operator_kind ==
                                            MINIC_BINARY_SUBTRACT,
                                        &delta) &&
            static_add_pointer_offset(target->byte_addend, delta, &target->byte_addend)) {
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
            const MinicExpression *right;

            right = minic_c0_program_expression(program, expression->value.binary.right);
            if (right != NULL && minic_type_pointee(right->type, &pointee_type) &&
                static_object_address_relocation_path(
                    parser, expression->value.binary.right, target) &&
                static_pointer_offset_bytes(
                    parser, pointee_type, expression->value.binary.left, false, &delta) &&
                static_add_pointer_offset(target->byte_addend, delta, &target->byte_addend)) {
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
    if (static_function_address_relocation_target(
            parser->program, expression_id, &initializer->function_id)) {
        initializer->has_relocation = true;
        initializer->relocation_is_function = true;
        return true;
    }
    if (static_object_address_relocation_path(
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

static bool parse_static_pointer_initializer(MinicParser *parser,
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

static bool begin_static_object_definition(MinicParser *parser,
                                           MinicType type,
                                           MinicSourceSpan name_span,
                                           MinicGlobalObjectId *object_id) {
    const MinicGlobalObject *existing;

    if (parser == NULL || object_id == NULL) {
        return false;
    }
    *object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (*object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                type,
                                                true,
                                                minic_type_is_const(type),
                                                object_id)) {
            minic_parser_error(parser, "cannot create static object definition");
            return false;
        }
        return true;
    }
    existing = minic_c0_program_global_object(parser->program, *object_id);
    if (existing == NULL || !existing->is_internal || !minic_type_equal(existing->type, type) ||
        !minic_c0_global_object_begin_definition(parser->program, *object_id)) {
        minic_parser_error(parser, "conflicting static object definition");
        return false;
    }
    return true;
}

bool minic_parser_parse_static_pointer_object_initializer(MinicParser *parser,
                                                          MinicGlobalObjectId object_id,
                                                          MinicType pointer_type) {
    MinicStaticPointerInitializer initializer;
    bool recorded;

    if (parser == NULL || object_id >= parser->program->global_object_count ||
        !minic_type_is_pointer(pointer_type) ||
        !parse_static_pointer_initializer(parser, pointer_type, &initializer)) {
        return false;
    }
    if (!initializer.has_relocation) {
        if (!minic_c0_global_object_add_initializer_bits(
                parser->program, object_id, initializer.bits)) {
            minic_parser_error(parser, "cannot record static pointer constant bits");
            return false;
        }
        return true;
    }
    if (initializer.relocation_is_label) {
        recorded =
            minic_c0_global_object_add_label_relocation(parser->program,
                                                        object_id,
                                                        MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                                                        0U,
                                                        initializer.label_statement_id);
    } else if (initializer.relocation_is_function) {
        recorded = initializer.has_explicit_pointer_cast
                       ? minic_c0_global_object_add_function_relocation_cast(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                             0U,
                             initializer.function_id)
                       : minic_c0_global_object_add_function_relocation(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                             0U,
                             initializer.function_id);
    } else {
        recorded = initializer.has_explicit_pointer_cast
                       ? minic_c0_global_object_add_object_relocation_path_addend_cast(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                             0U,
                             initializer.relocation_target.object_id,
                             initializer.relocation_target.member_indices,
                             initializer.relocation_target.member_depth,
                             initializer.relocation_target.byte_addend)
                       : minic_c0_global_object_add_object_relocation_path_addend(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                             0U,
                             initializer.relocation_target.object_id,
                             initializer.relocation_target.member_indices,
                             initializer.relocation_target.member_depth,
                             initializer.relocation_target.byte_addend);
    }
    if (!recorded || !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot record static symbolic pointer relocation");
        return false;
    }
    return true;
}

static bool parse_static_zero_definition(MinicParser *parser,
                                         MinicType object_type,
                                         MinicSourceSpan name_span);

static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    bool braced;

    if (!begin_static_object_definition(parser, type, name_span, &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin static scalar initializer");
        }
        return false;
    }

    braced = parser->current.kind == MINIC_TOKEN_LBRACE;
    if (braced && !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(type)) {
        uint64_t bits;

        if (!minic_parser_parse_integer_initializer_bits(parser, type, &bits) ||
            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, bits)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static integer initializer");
            }
            return false;
        }
    } else if (minic_type_is_pointer(type)) {
        if (!minic_parser_parse_static_pointer_object_initializer(parser, object_id, type)) {
            return false;
        }
    } else {
        minic_parser_error(parser, "unsupported static scalar type");
        return false;
    }
    if (braced) {
        if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
            return false;
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_RBRACE, "expected '}' after static scalar initializer")) {
            return false;
        }
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        MinicSourceSpan next_name_span;

        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected static scalar declarator after ','");
            return false;
        }
        next_name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            return parse_static_scalar(parser, type, next_name_span);
        }
        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            return parse_static_zero_definition(parser, type, next_name_span);
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            return minic_parser_parse_static_zero_declaration_list_after_head(parser,
                                                                               type,
                                                                               type,
                                                                               next_name_span,
                                                                               NULL,
                                                                               0U,
                                                                               false,
                                                                               0U);
        }
        minic_parser_error(parser,
                           "expected '=', ',' or ';' after static scalar declarator");
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}

static bool
append_static_constant_zero(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type);

static bool append_static_field_zeros(MinicParser *parser,
                                      MinicGlobalObjectId object_id,
                                      const MinicRecordField *field) {
    size_t element_index;

    if (field == NULL || field->element_count == 0U) {
        return false;
    }
    /* Flexible and GNU zero-length array members participate in record layout,
     * but contribute no scalar initializer slot to the fixed object extent. */
    if (field->is_flexible_array || field->is_zero_length_array) {
        return true;
    }
    for (element_index = 0U; element_index < field->element_count; ++element_index) {
        if (!append_static_constant_zero(parser, object_id, field->type)) {
            return false;
        }
    }
    return true;
}

static bool
append_static_constant_zero(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return minic_c0_global_object_add_initializer(parser->program, object_id, 0);
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;
        size_t element_index;

        array_type = minic_c0_program_array_type(parser->program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U) {
            return false;
        }
        for (element_index = 0U; element_index < array_type->element_count; ++element_index) {
            if (!append_static_constant_zero(parser, object_id, array_type->element_type)) {
                return false;
            }
        }
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;
        size_t field_limit;

        record = minic_c0_program_record(parser->program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        field_limit = record->is_union ? 1U : record->field_count;
        for (field_index = 0U; field_index < field_limit; ++field_index) {
            if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type);

static bool static_integer_address_constant_delta(const MinicParser *parser,
                                                  MinicExpressionId expression_id,
                                                  int64_t *delta) {
    MinicConstValue value;

    return parser != NULL && delta != NULL &&
           minic_const_eval_integer(parser->program, parser->target_info, expression_id, &value) &&
           minic_const_value_as_int64(parser->program, parser->target_info, &value, delta);
}

static bool static_integer_address_relocation_target(const MinicParser *parser,
                                                     MinicExpressionId expression_id,
                                                     MinicStaticObjectRelocationTarget *target) {
    const MinicExpression *expression;

    if (parser == NULL || target == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST || expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION) {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(parser->program, expression->value.unary.operand);
        if (operand != NULL && minic_type_is_pointer(operand->type)) {
            return static_object_address_relocation_path(
                parser, expression->value.unary.operand, target);
        }
        return static_integer_address_relocation_target(
            parser, expression->value.unary.operand, target);
    }
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        MinicConstValue condition;
        bool is_zero;
        MinicExpressionId selected_id;

        if (!minic_const_eval_integer(parser->program,
                                      parser->target_info,
                                      expression->value.conditional.condition,
                                      &condition) ||
            !minic_const_value_is_zero(
                parser->program, parser->target_info, &condition, &is_zero)) {
            return false;
        }
        selected_id = !is_zero && expression->value.conditional.uses_condition_value
                          ? expression->value.conditional.condition
                          : (is_zero ? expression->value.conditional.when_false
                                     : expression->value.conditional.when_true);
        return static_integer_address_relocation_target(parser, selected_id, target);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT)) {
        MinicStaticObjectRelocationTarget base;
        int64_t delta;

        if (static_integer_address_relocation_target(
                parser, expression->value.binary.left, &base) &&
            static_integer_address_constant_delta(parser, expression->value.binary.right, &delta)) {
            if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) {
                if (delta == INT64_MIN) {
                    return false;
                }
                delta = -delta;
            }
            if (!static_add_pointer_offset(base.byte_addend, delta, &base.byte_addend)) {
                return false;
            }
            *target = base;
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
            static_integer_address_constant_delta(parser, expression->value.binary.left, &delta) &&
            static_integer_address_relocation_target(
                parser, expression->value.binary.right, &base) &&
            static_add_pointer_offset(base.byte_addend, delta, &base.byte_addend)) {
            *target = base;
            return true;
        }
    }
    return false;
}

static bool static_integer_address_slot_supported(const MinicParser *parser, MinicType type) {
    const MinicDataLayout *layout;
    size_t alignment;
    size_t size;

    layout = parser == NULL ? NULL : minic_target_info_data_layout(parser->target_info);
    return layout != NULL && minic_type_is_integer(type) &&
           minic_data_layout_type(layout, parser->program, type, &size, &alignment) &&
           size == layout->pointer_size;
}

static bool parse_static_scalar_constant_at(MinicParser *parser,
                                            MinicGlobalObjectId object_id,
                                            MinicType type,
                                            bool overwrite,
                                            size_t overwrite_slot) {
    bool braced;

    if (parser == NULL || object_id >= parser->program->global_object_count) {
        return false;
    }
    braced = parser->current.kind == MINIC_TOKEN_LBRACE;
    if (braced && !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(type)) {
        MinicConstValue constant;
        MinicConstValue converted;
        MinicExpressionId expression_id;
        MinicStaticObjectRelocationTarget relocation_target;
        MinicFunctionId relocation_function_id;
        uint64_t parsed_bits;
        bool has_symbolic_address;
        bool symbolic_address_is_function;

        if (!minic_parser_parse_expression(parser, &expression_id, 0U)) {
            return false;
        }
        relocation_function_id = MINIC_FUNCTION_INVALID;
        symbolic_address_is_function = false;
        if (minic_const_eval_integer(
                parser->program, parser->target_info, expression_id, &constant) &&
            minic_const_value_convert_integer(
                parser->program, parser->target_info, &constant, type, &converted)) {
            parsed_bits = converted.bits;
            has_symbolic_address = false;
        } else if (minic_const_eval_arithmetic_to_integer(parser->program,
                                                         parser->target_info,
                                                         expression_id,
                                                         type,
                                                         &converted)) {
            parsed_bits = converted.bits;
            has_symbolic_address = false;
        } else if (static_integer_address_slot_supported(parser, type) &&
                   static_function_address_relocation_target(
                       parser->program, expression_id, &relocation_function_id)) {
            parsed_bits = 0U;
            has_symbolic_address = true;
            symbolic_address_is_function = true;
        } else if (static_integer_address_slot_supported(parser, type) &&
                   static_integer_address_relocation_target(
                       parser, expression_id, &relocation_target)) {
            parsed_bits = 0U;
            has_symbolic_address = true;
        } else {
            minic_parser_error(parser,
                               "integer initializer requires an arithmetic constant or "
                               "pointer-sized symbolic address");
            return false;
        }
        if (overwrite) {
            if (!minic_c0_global_object_replace_aggregate_initializer_bits(
                    parser->program, object_id, overwrite_slot, parsed_bits)) {
                minic_parser_error(parser, "cannot replace backward static scalar initializer");
                return false;
            }
        } else if (!minic_c0_global_object_add_initializer_bits(
                       parser->program, object_id, parsed_bits)) {
            minic_parser_error(parser, "cannot record static aggregate integer initializer");
            return false;
        }
        if (has_symbolic_address) {
            size_t relocation_slot;
            bool recorded;

            relocation_slot =
                overwrite ? overwrite_slot
                          : parser->program->global_objects[object_id].initializer_count - 1U;
            if (symbolic_address_is_function) {
                recorded = minic_c0_global_object_add_function_relocation(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                    relocation_slot,
                    relocation_function_id);
            } else {
                recorded = minic_c0_global_object_add_integer_object_relocation_path_addend(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                    relocation_slot,
                    relocation_target.object_id,
                    relocation_target.member_indices,
                    relocation_target.member_depth,
                    relocation_target.byte_addend);
            }
            if (!recorded) {
                minic_parser_error(parser, "cannot record static symbolic integer relocation");
                return false;
            }
        }
    } else if (minic_type_is_pointer(type)) {
        MinicStaticPointerInitializer initializer;
        size_t slot_index;

        slot_index = overwrite ? overwrite_slot
                               : parser->program->global_objects[object_id].initializer_count;
        if (!parse_static_pointer_initializer(parser, type, &initializer)) {
            return false;
        }
        if (initializer.has_relocation) {
            bool recorded;

            if (overwrite &&
                !minic_c0_global_object_replace_aggregate_initializer_bits(
                    parser->program, object_id, overwrite_slot, 0U)) {
                minic_parser_error(parser, "cannot replace backward static relocation slot");
                return false;
            }
            if (!overwrite &&
                !minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U)) {
                minic_parser_error(parser, "cannot reserve nested static relocation slot");
                return false;
            }
            if (initializer.relocation_is_label) {
                recorded = minic_c0_global_object_add_label_relocation(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                    slot_index,
                    initializer.label_statement_id);
            } else if (initializer.relocation_is_function) {
                recorded = initializer.has_explicit_pointer_cast
                               ? minic_c0_global_object_add_function_relocation_cast(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                     slot_index,
                                     initializer.function_id)
                               : minic_c0_global_object_add_function_relocation(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                     slot_index,
                                     initializer.function_id);
            } else {
                recorded = initializer.has_explicit_pointer_cast
                               ? minic_c0_global_object_add_object_relocation_path_addend_cast(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                     slot_index,
                                     initializer.relocation_target.object_id,
                                     initializer.relocation_target.member_indices,
                                     initializer.relocation_target.member_depth,
                                     initializer.relocation_target.byte_addend)
                               : minic_c0_global_object_add_object_relocation_path_addend(
                                     parser->program,
                                     object_id,
                                     MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                     slot_index,
                                     initializer.relocation_target.object_id,
                                     initializer.relocation_target.member_indices,
                                     initializer.relocation_target.member_depth,
                                     initializer.relocation_target.byte_addend);
            }
            if (!recorded) {
                minic_parser_error(parser, "cannot record nested static symbolic relocation");
                return false;
            }
        } else if (overwrite) {
            if (!minic_c0_global_object_replace_aggregate_initializer_bits(
                    parser->program, object_id, overwrite_slot, initializer.bits)) {
                minic_parser_error(parser, "cannot replace backward static pointer initializer");
                return false;
            }
        } else if (!minic_c0_global_object_add_initializer_bits(
                       parser->program, object_id, initializer.bits)) {
            minic_parser_error(parser, "cannot record static pointer constant bits");
            return false;
        }
    } else {
        return false;
    }
    if (!braced) {
        return true;
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after scalar initializer");
}

static bool
parse_static_scalar_constant(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type) {
    return parse_static_scalar_constant_at(parser, object_id, type, false, 0U);
}

static bool grow_static_array_slots(MinicParser *parser,
                                    MinicStaticArraySlot **slots,
                                    size_t *capacity,
                                    size_t required) {
    MinicStaticArraySlot *resized;
    size_t old_capacity;
    size_t new_capacity;

    if (parser == NULL || slots == NULL || capacity == NULL) {
        return false;
    }
    if (required <= *capacity) {
        return true;
    }
    old_capacity = *capacity;
    new_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2U) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2U;
    }
    if (new_capacity < required || new_capacity > SIZE_MAX / sizeof(**slots)) {
        minic_parser_error(parser, "static array initializer slot count overflows");
        return false;
    }
    resized = (MinicStaticArraySlot *)realloc(*slots, new_capacity * sizeof(**slots));
    if (resized == NULL) {
        minic_parser_error(parser, "out of memory while planning static array initializer");
        return false;
    }
    (void)memset(resized + old_capacity, 0, (new_capacity - old_capacity) * sizeof(*resized));
    *slots = resized;
    *capacity = new_capacity;
    return true;
}

static bool parse_static_array_scalar_slot(MinicParser *parser,
                                           MinicType element_type,
                                           MinicStaticArraySlot *slot) {
    bool braced;

    if (parser == NULL || slot == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type))) {
        return false;
    }
    (void)memset(slot, 0, sizeof(*slot));
    braced = parser->current.kind == MINIC_TOKEN_LBRACE;
    if (braced && !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(element_type)) {
        if (!minic_parser_parse_integer_initializer_bits(
                parser, element_type, &slot->integer_bits)) {
            return false;
        }
    } else if (!parse_static_pointer_initializer(
                   parser, element_type, &slot->pointer_initializer)) {
        return false;
    }
    if (!braced) {
        return true;
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA && !minic_parser_advance(parser)) {
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after scalar initializer");
}

static bool
materialize_static_pointer_array_slot(MinicParser *parser,
                                      MinicGlobalObjectId object_id,
                                      size_t slot_index,
                                      const MinicStaticPointerInitializer *initializer) {
    bool recorded;

    if (parser == NULL || initializer == NULL ||
        !minic_c0_global_object_add_initializer_bits(
            parser->program, object_id, initializer->has_relocation ? 0U : initializer->bits)) {
        return false;
    }
    if (!initializer->has_relocation) {
        return true;
    }
    if (initializer->relocation_is_label) {
        recorded = minic_c0_global_object_add_label_relocation(
            parser->program,
            object_id,
            MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
            slot_index,
            initializer->label_statement_id);
    } else if (initializer->relocation_is_function) {
        recorded = initializer->has_explicit_pointer_cast
                       ? minic_c0_global_object_add_function_relocation_cast(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                             slot_index,
                             initializer->function_id)
                       : minic_c0_global_object_add_function_relocation(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                             slot_index,
                             initializer->function_id);
    } else {
        recorded = initializer->has_explicit_pointer_cast
                       ? minic_c0_global_object_add_object_relocation_path_addend_cast(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                             slot_index,
                             initializer->relocation_target.object_id,
                             initializer->relocation_target.member_indices,
                             initializer->relocation_target.member_depth,
                             initializer->relocation_target.byte_addend)
                       : minic_c0_global_object_add_object_relocation_path_addend(
                             parser->program,
                             object_id,
                             MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                             slot_index,
                             initializer->relocation_target.object_id,
                             initializer->relocation_target.member_indices,
                             initializer->relocation_target.member_depth,
                             initializer->relocation_target.byte_addend);
    }
    if (!recorded) {
        MinicType slot_type;

        if (!minic_c0_global_relocation_slot_type(parser->program,
                                                  &parser->program->global_objects[object_id],
                                                  MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                                  slot_index,
                                                  &slot_type)) {
            minic_parser_error(parser, "static pointer array relocation slot type is unavailable");
            return false;
        }
        if (initializer->relocation_is_function &&
            !minic_c0_global_relocation_function_target_compatible(
                parser->program,
                slot_type,
                initializer->function_id,
                initializer->has_explicit_pointer_cast)) {
            minic_parser_error(parser, "static pointer array function target type mismatch");
            return false;
        }
        if (!initializer->relocation_is_label && !initializer->relocation_is_function) {
            MinicGlobalRelocation probe;
            size_t depth;

            (void)memset(&probe, 0, sizeof(probe));
            probe.target_kind = MINIC_GLOBAL_RELOCATION_OBJECT;
            probe.target_id = initializer->relocation_target.object_id;
            probe.target_member_depth = initializer->relocation_target.member_depth;
            probe.target_byte_addend = initializer->relocation_target.byte_addend;
            probe.has_explicit_pointer_cast = initializer->has_explicit_pointer_cast;
            for (depth = 0U; depth < probe.target_member_depth; ++depth) {
                probe.target_member_indices[depth] =
                    initializer->relocation_target.member_indices[depth];
            }
            if (!minic_c0_global_relocation_object_target_compatible(
                    parser->program, &probe, slot_type)) {
                minic_parser_error(parser, "static pointer array object target type mismatch");
                return false;
            }
        }
        minic_parser_error(parser, "static pointer array relocation commit rejected");
        return false;
    }
    return true;
}

static bool materialize_static_array_slots(MinicParser *parser,
                                           MinicGlobalObjectId object_id,
                                           MinicType element_type,
                                           const MinicStaticArraySlot *slots,
                                           size_t slot_count,
                                           size_t aggregate_slot_base) {
    size_t index;

    if (parser == NULL || (slot_count != 0U && slots == NULL) ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type))) {
        return false;
    }
    for (index = 0U; index < slot_count; ++index) {
        if (minic_type_is_integer(element_type)) {
            if (!minic_c0_global_object_add_initializer_bits(
                    parser->program, object_id, slots[index].integer_bits)) {
                minic_parser_error(parser, "cannot materialize static integer array slot");
                return false;
            }
        } else {
            size_t aggregate_slot;

            if (aggregate_slot_base > SIZE_MAX - index) {
                minic_parser_error(parser, "static pointer array aggregate slot overflows");
                return false;
            }
            aggregate_slot = aggregate_slot_base + index;
            if (!materialize_static_pointer_array_slot(
                    parser, object_id, aggregate_slot, &slots[index].pointer_initializer)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot materialize static pointer array slot");
                }
                return false;
            }
        }
    }
    return true;
}

static bool parse_static_scalar_array_transaction(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  MinicType element_type,
                                                  size_t element_count,
                                                  bool infer_bound) {
    MinicArrayInitializerPlan plan;
    MinicStaticArraySlot *action_values;
    MinicStaticArraySlot *final_slots;
    const MinicGlobalObject *object;
    size_t action_capacity;
    size_t final_capacity;
    size_t extent;
    size_t index;
    bool success;

    action_values = NULL;
    final_slots = NULL;
    action_capacity = 0U;
    final_capacity = 0U;
    extent = 0U;
    success = false;
    if (parser != NULL && !infer_bound && element_count != 0U &&
        minic_type_is_char_integer(element_type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        return minic_parser_add_bounded_string_literal_initializer(
            parser, object_id, element_count);
    }

    minic_array_initializer_plan_initialize(&plan, element_count, infer_bound);
    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) ||
        (!infer_bound && element_count == 0U) || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid static scalar array initializer");
        }
        goto done;
    }
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t action_id;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (!minic_parser_parse_array_designator(
                    parser, element_count, infer_bound, &first, &last) ||
                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "static array designator extent overflows");
                }
                goto done;
            }
        } else if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
            minic_parser_error(parser, "too many nested static array initializers");
            goto done;
        }
        if (!grow_static_array_slots(parser, &action_values, &action_capacity, action_id + 1U) ||
            !parse_static_array_scalar_slot(parser, element_type, &action_values[action_id])) {
            goto done;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static array initializer");
            goto done;
        }
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after static array initializer")) {
        goto done;
    }
    extent = minic_array_initializer_plan_element_count(&plan);
    if (infer_bound) {
        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL || !minic_type_is_array(object->type) ||
            (extent == 0U
                 ? !minic_c0_program_complete_zero_length_array_type(
                       parser->program, object->type)
                 : !minic_c0_program_complete_array_type(
                       parser->program, object->type, extent))) {
            minic_parser_error(parser, "cannot complete inferred static array type");
            goto done;
        }
    }
    if (!grow_static_array_slots(parser, &final_slots, &final_capacity, extent)) {
        goto done;
    }
    for (index = 0U; index < extent; ++index) {
        size_t owner;

        if (!minic_array_initializer_plan_final_owner(&plan, index, &owner)) {
            minic_parser_error(parser, "cannot resolve static array initializer owner");
            goto done;
        }
        if (owner != MINIC_INITIALIZER_ACTION_INVALID) {
            final_slots[index] = action_values[owner];
        }
    }
    object = minic_c0_program_global_object(parser->program, object_id);
    if (object == NULL ||
        !materialize_static_array_slots(parser,
                                        object_id,
                                        element_type,
                                        final_slots,
                                        extent,
                                        object->initializer_count)) {
        goto done;
    }
    success = true;

done:
    free(action_values);
    free(final_slots);
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

typedef struct MinicStaticAggregateArrayAction {
    uint64_t *values;
    size_t value_count;
    MinicGlobalRelocation *relocations;
    size_t relocation_count;
    MinicGlobalUnionSelection *union_selections;
    size_t union_selection_count;
} MinicStaticAggregateArrayAction;

static void destroy_static_aggregate_array_actions(MinicStaticAggregateArrayAction *actions,
                                                   size_t action_count) {
    size_t index;

    if (actions == NULL) {
        return;
    }
    for (index = 0U; index < action_count; ++index) {
        free(actions[index].values);
        free(actions[index].relocations);
        free(actions[index].union_selections);
    }
    free(actions);
}

static bool grow_static_aggregate_array_actions(MinicParser *parser,
                                                MinicStaticAggregateArrayAction **actions,
                                                size_t *capacity,
                                                size_t required) {
    MinicStaticAggregateArrayAction *resized;
    size_t old_capacity;
    size_t new_capacity;

    if (parser == NULL || actions == NULL || capacity == NULL) {
        return false;
    }
    if (required <= *capacity) {
        return true;
    }
    old_capacity = *capacity;
    new_capacity = old_capacity == 0U ? 8U : old_capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2U) {
            minic_parser_error(parser, "static aggregate array action capacity overflows");
            return false;
        }
        new_capacity *= 2U;
    }
    if (new_capacity > SIZE_MAX / sizeof(**actions)) {
        minic_parser_error(parser, "static aggregate array action capacity overflows");
        return false;
    }
    resized = (MinicStaticAggregateArrayAction *)realloc(*actions, new_capacity * sizeof(*resized));
    if (resized == NULL) {
        minic_parser_error(parser, "out of memory while growing static aggregate array actions");
        return false;
    }
    (void)memset(resized + old_capacity, 0, (new_capacity - old_capacity) * sizeof(*resized));
    *actions = resized;
    *capacity = new_capacity;
    return true;
}

static bool capture_static_aggregate_array_action(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  size_t initializer_begin,
                                                  size_t relocation_begin,
                                                  size_t union_selection_begin,
                                                  MinicStaticAggregateArrayAction *action) {
    MinicGlobalObject *object;
    size_t value_count;
    size_t relocation_count;
    size_t union_selection_count;
    size_t index;

    if (parser == NULL || action == NULL || object_id >= parser->program->global_object_count) {
        return false;
    }
    object = &parser->program->global_objects[object_id];
    if (initializer_begin > object->initializer_count ||
        relocation_begin > object->relocation_count ||
        union_selection_begin > object->union_selection_count) {
        return false;
    }
    value_count = object->initializer_count - initializer_begin;
    relocation_count = object->relocation_count - relocation_begin;
    union_selection_count = object->union_selection_count - union_selection_begin;
    if (value_count == 0U || value_count > SIZE_MAX / sizeof(*action->values) ||
        relocation_count > SIZE_MAX / sizeof(*action->relocations) ||
        union_selection_count > SIZE_MAX / sizeof(*action->union_selections)) {
        minic_parser_error(parser, "invalid static aggregate array action payload");
        return false;
    }

    action->values = (uint64_t *)malloc(value_count * sizeof(*action->values));
    action->relocations =
        relocation_count == 0U
            ? NULL
            : (MinicGlobalRelocation *)malloc(relocation_count * sizeof(*action->relocations));
    action->union_selections = union_selection_count == 0U
                                   ? NULL
                                   : (MinicGlobalUnionSelection *)malloc(
                                         union_selection_count * sizeof(*action->union_selections));
    if (action->values == NULL || (relocation_count != 0U && action->relocations == NULL) ||
        (union_selection_count != 0U && action->union_selections == NULL)) {
        free(action->values);
        free(action->relocations);
        free(action->union_selections);
        action->values = NULL;
        action->relocations = NULL;
        action->union_selections = NULL;
        minic_parser_error(parser, "out of memory while capturing static aggregate array action");
        return false;
    }

    (void)memcpy(action->values,
                 object->initializer_values + initializer_begin,
                 value_count * sizeof(*action->values));
    if (relocation_count != 0U) {
        (void)memcpy(action->relocations,
                     object->relocations + relocation_begin,
                     relocation_count * sizeof(*action->relocations));
    }
    if (union_selection_count != 0U) {
        (void)memcpy(action->union_selections,
                     object->union_selections + union_selection_begin,
                     union_selection_count * sizeof(*action->union_selections));
    }
    for (index = 0U; index < relocation_count; ++index) {
        if (action->relocations[index].location_index < initializer_begin) {
            minic_parser_error(parser,
                               "static aggregate array relocation precedes captured action");
            return false;
        }
        action->relocations[index].location_index -= initializer_begin;
    }
    for (index = 0U; index < union_selection_count; ++index) {
        if (action->union_selections[index].initializer_slot < initializer_begin) {
            minic_parser_error(parser,
                               "static aggregate array union selection precedes captured action");
            return false;
        }
        action->union_selections[index].initializer_slot -= initializer_begin;
    }

    action->value_count = value_count;
    action->relocation_count = relocation_count;
    action->union_selection_count = union_selection_count;
    object->initializer_count = initializer_begin;
    object->relocation_count = relocation_begin;
    object->union_selection_count = union_selection_begin;
    return true;
}

static bool
materialize_static_aggregate_array_action(MinicParser *parser,
                                          MinicGlobalObjectId object_id,
                                          const MinicStaticAggregateArrayAction *action) {
    MinicGlobalObject *object;
    size_t destination_begin;
    size_t index;

    if (parser == NULL || action == NULL || object_id >= parser->program->global_object_count) {
        return false;
    }
    object = &parser->program->global_objects[object_id];
    destination_begin = object->initializer_count;
    for (index = 0U; index < action->value_count; ++index) {
        if (!minic_c0_global_object_add_initializer_bits(
                parser->program, object_id, action->values[index])) {
            minic_parser_error(parser, "cannot materialize static aggregate array slots");
            return false;
        }
    }
    for (index = 0U; index < action->union_selection_count; ++index) {
        const MinicGlobalUnionSelection *selection;
        size_t initializer_slot;

        selection = &action->union_selections[index];
        if (selection->initializer_slot > SIZE_MAX - destination_begin) {
            minic_parser_error(parser, "static aggregate array union selection index overflows");
            return false;
        }
        initializer_slot = destination_begin + selection->initializer_slot;
        if (!minic_c0_global_object_select_union_member_with_span(parser->program,
                                                                  object_id,
                                                                  initializer_slot,
                                                                  selection->record_id,
                                                                  selection->field_index,
                                                                  selection->initializer_span)) {
            minic_parser_error(parser, "cannot materialize static aggregate array union selection");
            return false;
        }
    }
    for (index = 0U; index < action->relocation_count; ++index) {
        const MinicGlobalRelocation *relocation;
        size_t location_index;
        bool recorded;

        relocation = &action->relocations[index];
        if (relocation->location_index > SIZE_MAX - destination_begin) {
            minic_parser_error(parser, "static aggregate array relocation index overflows");
            return false;
        }
        location_index = destination_begin + relocation->location_index;
        if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_LABEL) {
            recorded = minic_c0_global_object_add_label_relocation(
                parser->program,
                object_id,
                MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                location_index,
                (MinicStatementId)relocation->target_id);
        } else if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION) {
            recorded = relocation->has_explicit_pointer_cast
                           ? minic_c0_global_object_add_function_relocation_cast(
                                 parser->program,
                                 object_id,
                                 MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                 location_index,
                                 (MinicFunctionId)relocation->target_id)
                           : minic_c0_global_object_add_function_relocation(
                                 parser->program,
                                 object_id,
                                 MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                 location_index,
                                 (MinicFunctionId)relocation->target_id);
        } else {
            recorded = relocation->has_explicit_pointer_cast
                           ? minic_c0_global_object_add_object_relocation_path_addend_cast(
                                 parser->program,
                                 object_id,
                                 MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                 location_index,
                                 (MinicGlobalObjectId)relocation->target_id,
                                 relocation->target_member_indices,
                                 relocation->target_member_depth,
                                 relocation->target_byte_addend)
                           : minic_c0_global_object_add_object_relocation_path_addend(
                                 parser->program,
                                 object_id,
                                 MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                                 location_index,
                                 (MinicGlobalObjectId)relocation->target_id,
                                 relocation->target_member_indices,
                                 relocation->target_member_depth,
                                 relocation->target_byte_addend);
        }
        if (!recorded) {
            minic_parser_error(parser,
                               "cannot materialize static aggregate array relocation "
                               "(captured-location=%u target=%u relative-slot=%zu "
                               "destination-slot=%zu)",
                               (unsigned int)relocation->location_kind,
                               (unsigned int)relocation->target_kind,
                               relocation->location_index,
                               location_index);
            return false;
        }
    }
    return true;
}

static bool append_static_chained_array_designator_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType array_type) {
    const MinicArrayType *array;
    size_t first;
    size_t last;
    size_t index;

    if (parser == NULL || !minic_type_is_array(array_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    array = minic_c0_program_array_type(parser->program, array_type.array_type_id);
    if (array == NULL || array->element_count == 0U || array->is_zero_length ||
        !minic_parser_parse_array_designator_component(
            parser, array->element_count, false, &first, &last)) {
        return false;
    }
    if (first != last) {
        minic_parser_error(
            parser, "GNU range designators inside chained static arrays are not supported yet");
        return false;
    }
    for (index = 0U; index < first; ++index) {
        if (!append_static_constant_zero(parser, object_id, array->element_type)) {
            minic_parser_error(parser, "cannot zero-fill chained static array prefix");
            return false;
        }
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (!minic_type_is_array(array->element_type) ||
            !append_static_chained_array_designator_value(parser, object_id, array->element_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "chained array designator requires another array dimension");
            }
            return false;
        }
    } else {
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_EQUAL, "expected '=' after chained array designator") ||
            !minic_parser_parse_static_storage_initializer_value(
                parser, object_id, array->element_type)) {
            return false;
        }
    }
    for (index = first + 1U; index < array->element_count; ++index) {
        if (!append_static_constant_zero(parser, object_id, array->element_type)) {
            minic_parser_error(parser, "cannot zero-fill chained static array suffix");
            return false;
        }
    }
    return true;
}

static bool parse_static_forward_array_initializer(MinicParser *parser,
                                                   MinicGlobalObjectId object_id,
                                                   MinicType element_type,
                                                   size_t element_count,
                                                   bool infer_bound,
                                                   size_t *parsed_extent) {
    MinicArrayInitializerPlan plan;
    MinicStaticAggregateArrayAction *actions;
    size_t action_capacity;
    size_t action_count;
    size_t extent;
    size_t index;
    bool success;

    actions = NULL;
    action_capacity = 0U;
    action_count = 0U;
    extent = 0U;
    success = false;
    minic_array_initializer_plan_initialize(&plan, element_count, infer_bound);
    if (parser == NULL || object_id >= parser->program->global_object_count ||
        (!infer_bound && element_count == 0U) || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid static aggregate array initializer");
        }
        goto done;
    }
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t action_id;
        const MinicGlobalObject *object;
        size_t initializer_begin;
        size_t relocation_begin;
        size_t union_selection_begin;
        bool chained_designator;

        chained_designator = false;
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (!minic_parser_parse_array_designator_component(
                    parser, element_count, infer_bound, &first, &last) ||
                !minic_array_initializer_plan_add_designated(&plan, first, last, &action_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "static aggregate array designator extent overflows");
                }
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
                if (first != last) {
                    minic_parser_error(parser, "outer GNU range designator cannot be chained yet");
                    goto done;
                }
                if (!minic_type_is_array(element_type)) {
                    minic_parser_error(parser, "chained array designator exceeds array dimensions");
                    goto done;
                }
                chained_designator = true;
            } else if (!minic_parser_expect(
                           parser, MINIC_TOKEN_EQUAL, "expected '=' after array designator")) {
                goto done;
            }
        } else if (!minic_array_initializer_plan_add_positional(&plan, &action_id)) {
            minic_parser_error(parser, "too many nested static array initializers");
            goto done;
        }
        if (!grow_static_aggregate_array_actions(
                parser, &actions, &action_capacity, action_id + 1U)) {
            goto done;
        }
        if (action_id >= action_count) {
            action_count = action_id + 1U;
        }
        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL) {
            goto done;
        }
        initializer_begin = object->initializer_count;
        relocation_begin = object->relocation_count;
        union_selection_begin = object->union_selection_count;
        if ((chained_designator
                 ? !append_static_chained_array_designator_value(parser, object_id, element_type)
                 : !minic_parser_parse_static_storage_initializer_value(
                       parser, object_id, element_type)) ||
            !capture_static_aggregate_array_action(parser,
                                                   object_id,
                                                   initializer_begin,
                                                   relocation_begin,
                                                   union_selection_begin,
                                                   &actions[action_id])) {
            goto done;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static array initializer");
            goto done;
        }
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after static array initializer")) {
        goto done;
    }
    extent = minic_array_initializer_plan_element_count(&plan);
    for (index = 0U; index < extent; ++index) {
        size_t owner;

        if (!minic_array_initializer_plan_final_owner(&plan, index, &owner)) {
            minic_parser_error(parser, "cannot resolve static aggregate array initializer owner");
            goto done;
        }
        if (owner == MINIC_INITIALIZER_ACTION_INVALID) {
            if (!append_static_constant_zero(parser, object_id, element_type)) {
                minic_parser_error(parser, "cannot zero-fill static aggregate array element");
                goto done;
            }
        } else if (owner >= action_count ||
                   !materialize_static_aggregate_array_action(parser, object_id, &actions[owner])) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot materialize static aggregate array action");
            }
            goto done;
        }
    }
    if (parsed_extent != NULL) {
        *parsed_extent = extent;
    }
    success = true;

done:
    destroy_static_aggregate_array_actions(actions, action_count);
    minic_array_initializer_plan_destroy(&plan);
    return success;
}

static bool parse_static_array_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicArrayType *array_type) {
    MinicType element_type;
    size_t element_count;
    size_t parsed_extent;
    bool infer_bound;

    if (array_type == NULL) {
        minic_parser_error(parser, "invalid static array initializer type");
        return false;
    }
    element_type = array_type->element_type;
    element_count = array_type->element_count;
    infer_bound = element_count == 0U && !array_type->is_zero_length;
    if (element_count == 0U && array_type->is_zero_length) {
        return minic_parser_expect(
                   parser, MINIC_TOKEN_LBRACE, "expected '{' for zero-length array initializer") &&
               minic_parser_expect(
                   parser, MINIC_TOKEN_RBRACE, "zero-length array initializer must be empty");
    }
    if (minic_type_is_integer(element_type) || minic_type_is_pointer(element_type)) {
        return parse_static_scalar_array_transaction(
            parser, object_id, element_type, element_count, infer_bound);
    }
    parsed_extent = 0U;
    if (!parse_static_forward_array_initializer(
            parser, object_id, element_type, element_count, infer_bound, &parsed_extent)) {
        return false;
    }
    if (infer_bound) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL || !minic_type_is_array(object->type) ||
            (parsed_extent == 0U
                 ? !minic_c0_program_complete_zero_length_array_type(
                       parser->program, object->type)
                 : !minic_c0_program_complete_array_type(
                       parser->program, object->type, parsed_extent))) {
            minic_parser_error(parser, "cannot complete inferred static aggregate array type");
            return false;
        }
    }
    return true;
}

typedef struct MinicStaticRecordDesignator {
    size_t field_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t depth;
    bool has_array_index;
    size_t array_index;
} MinicStaticRecordDesignator;

static bool parse_static_record_designator_path(MinicParser *parser,
                                                const MinicRecord *record,
                                                MinicStaticRecordDesignator *designator) {
    const MinicRecord *current_record;

    if (parser == NULL || record == NULL || designator == NULL ||
        parser->current.kind != MINIC_TOKEN_DOT) {
        return false;
    }
    (void)memset(designator, 0, sizeof(*designator));
    current_record = record;
    while (parser->current.kind == MINIC_TOKEN_DOT) {
        MinicRecordFieldPath field_path;
        MinicSourceSpan field_span;
        const MinicRecordField *field;
        size_t path_index;

        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected member name after '.' in initializer");
            return false;
        }
        field_span = parser->current.span;
        if (!minic_parser_find_record_field_path(parser, current_record, field_span, &field_path) ||
            !field_path.found || field_path.ambiguous || field_path.depth == 0U) {
            minic_parser_error(parser,
                               "static record designator requires an unambiguous member path");
            return false;
        }
        if (field_path.depth > MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH - designator->depth) {
            minic_parser_error(parser,
                               "static record designator path exceeds implementation limit");
            return false;
        }
        field = NULL;
        for (path_index = 0U; path_index < field_path.depth; ++path_index) {
            size_t field_index;

            field_index = field_path.field_indices[path_index];
            designator->field_indices[designator->depth++] = field_index;
            field = minic_c0_record_field(current_record, field_index);
            if (field == NULL) {
                return false;
            }
            if (path_index + 1U < field_path.depth) {
                if (field->element_count != 1U || field->is_array || field->is_bit_field ||
                    field->is_flexible_array || !minic_type_is_record(field->type)) {
                    minic_parser_error(
                        parser,
                        "promoted static record designator path requires scalar record members");
                    return false;
                }
                current_record = minic_c0_program_record(parser->program, field->type.record_id);
                if (current_record == NULL || !current_record->is_complete) {
                    minic_parser_error(
                        parser, "static record designator path requires complete record members");
                    return false;
                }
            }
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t first;
            size_t last;

            if (field == NULL || !field->is_array || field->element_count == 0U ||
                field->is_bit_field || field->is_flexible_array) {
                minic_parser_error(
                    parser, "array designator after record member requires a fixed array field");
                return false;
            }
            if (!minic_parser_parse_array_designator(
                    parser, field->element_count, false, &first, &last)) {
                return false;
            }
            if (first != last) {
                minic_parser_error(
                    parser, "GNU range designators after record members are not supported yet");
                return false;
            }
            designator->has_array_index = true;
            designator->array_index = first;
            return true;
        }
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            break;
        }
        if (field == NULL || field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array || !minic_type_is_record(field->type)) {
            minic_parser_error(
                parser, "intermediate static record designator member must be a scalar record");
            return false;
        }
        current_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            minic_parser_error(parser,
                               "static record designator path requires complete record members");
            return false;
        }
    }
    return designator->depth != 0U &&
           minic_parser_expect(
               parser, MINIC_TOKEN_EQUAL, "expected '=' after static record designator");
}

static bool static_record_designator_leaf_slot(const MinicC0Program *program,
                                               const MinicRecord *record,
                                               const MinicStaticRecordDesignator *designator,
                                               size_t *slot_index,
                                               const MinicRecordField **leaf_field) {
    const MinicRecord *current_record;
    size_t depth;
    size_t total;

    if (program == NULL || record == NULL || designator == NULL || slot_index == NULL ||
        leaf_field == NULL || designator->depth == 0U) {
        return false;
    }
    current_record = record;
    total = 0U;
    for (depth = 0U; depth < designator->depth; ++depth) {
        const MinicRecordField *field;
        size_t relative;
        size_t field_index;

        field_index = designator->field_indices[depth];
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || field->element_count == 0U || field->is_flexible_array ||
            !minic_c0_global_record_field_initializer_slot(
                program, current_record, field_index, &relative) ||
            total > SIZE_MAX - relative) {
            return false;
        }
        total += relative;
        if (depth + 1U == designator->depth) {
            *slot_index = total;
            *leaf_field = field;
            return true;
        }
        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            return false;
        }
        current_record = minic_c0_program_record(program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            return false;
        }
    }
    return false;
}

static bool overwrite_static_zero_field_value(MinicParser *parser,
                                              MinicGlobalObjectId object_id,
                                              const MinicRecordField *field,
                                              size_t field_base_slot);

static bool try_overwrite_static_zero_noncanonical_union_designator(
    MinicParser *parser,
    MinicGlobalObjectId object_id,
    const MinicRecord *record,
    const MinicStaticRecordDesignator *designator,
    size_t record_base_slot,
    bool *handled) {
    const MinicRecord *current_record;
    const MinicGlobalObject *object;
    size_t depth;
    size_t total;

    if (parser == NULL || record == NULL || designator == NULL || handled == NULL ||
        designator->depth == 0U || object_id >= parser->program->global_object_count) {
        return false;
    }
    *handled = false;
    current_record = record;
    object = &parser->program->global_objects[object_id];
    total = 0U;
    for (depth = 0U; depth < designator->depth; ++depth) {
        const MinicRecordField *field;
        size_t field_index;

        field_index = designator->field_indices[depth];
        field = minic_c0_record_field(current_record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_array || field->is_bit_field ||
            field->is_flexible_array) {
            return true;
        }
        if (current_record->is_union && field_index != 0U) {
            const MinicRecordField *canonical_field;
            MinicRecordId union_record_id;
            size_t canonical_element_slots;
            size_t canonical_slots;
            size_t selected_element_slots;
            size_t selected_slots;
            size_t slot_begin;
            size_t slot_end;
            size_t slot;
            size_t relocation_index;

            *handled = true;
            if (current_record->field_count == 0U ||
                current_record < parser->program->records ||
                current_record >= parser->program->records + parser->program->record_count) {
                minic_parser_error(parser,
                                   "backward noncanonical static union designator requires "
                                   "materialized union storage");
                return false;
            }
            canonical_field = &current_record->fields[0];
            if (canonical_field->element_count == 0U || canonical_field->is_flexible_array ||
                !minic_c0_global_initializer_slot_count(
                    parser->program, canonical_field->type, &canonical_element_slots) ||
                (canonical_element_slots != 0U &&
                 canonical_field->element_count > SIZE_MAX / canonical_element_slots) ||
                !minic_c0_global_initializer_slot_count(
                    parser->program, field->type, &selected_element_slots) ||
                (selected_element_slots != 0U &&
                 field->element_count > SIZE_MAX / selected_element_slots)) {
                minic_parser_error(parser, "cannot resolve static union storage shape");
                return false;
            }
            canonical_slots = canonical_field->element_count * canonical_element_slots;
            selected_slots = field->element_count * selected_element_slots;
            if (record_base_slot > SIZE_MAX - total) {
                return false;
            }
            slot_begin = record_base_slot + total;
            if (slot_begin > object->initializer_count ||
                canonical_slots > object->initializer_count - slot_begin) {
                minic_parser_error(parser, "backward static union storage is not materialized");
                return false;
            }
            slot_end = slot_begin + canonical_slots;
            for (slot = slot_begin; slot < slot_end; ++slot) {
                if (object->initializer_values[slot] != 0U) {
                    minic_parser_error(
                        parser,
                        "backward static union member can only replace implicit zero storage");
                    return false;
                }
            }
            for (relocation_index = 0U; relocation_index < object->relocation_count;
                 ++relocation_index) {
                const MinicGlobalRelocation *relocation;

                relocation = &object->relocations[relocation_index];
                if (relocation->location_kind ==
                        MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR &&
                    relocation->location_index >= slot_begin &&
                    relocation->location_index < slot_end) {
                    minic_parser_error(parser,
                                       "backward static union member cannot overwrite symbolic "
                                       "storage");
                    return false;
                }
            }
            /* A designated active member may require more logical initializer
               slots than the union's canonical first member (for example an
               anonymous { min, max } pair over a scalar first member). Growing
               an already-materialized union is safe without shifting any later
               aggregate slot only when this union is the current logical tail.
               Extend that implicit-zero tail and preserve the expanded span in
               union-selection metadata; non-tail growth remains fail-closed. */
            if (selected_slots > canonical_slots) {
                size_t extra_slots;

                if (slot_end != object->initializer_count) {
                    minic_parser_error(
                        parser,
                        "larger backward static union member requires trailing zero storage");
                    return false;
                }
                extra_slots = selected_slots - canonical_slots;
                while (extra_slots-- > 0U) {
                    if (!minic_c0_global_object_add_initializer(
                            parser->program, object_id, 0)) {
                        minic_parser_error(
                            parser, "cannot extend backward static union zero storage");
                        return false;
                    }
                }
                canonical_slots = selected_slots;
                slot_end = slot_begin + canonical_slots;
                object = &parser->program->global_objects[object_id];
            }
            union_record_id = (MinicRecordId)(current_record - parser->program->records);
            if (!minic_c0_global_object_select_union_member_with_span(parser->program,
                                                                      object_id,
                                                                      slot_begin,
                                                                      union_record_id,
                                                                      field_index,
                                                                      canonical_slots)) {
                minic_parser_error(parser, "cannot replace static union active member");
                return false;
            }
            if (depth + 1U == designator->depth) {
                if (!overwrite_static_zero_field_value(parser, object_id, field, slot_begin)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser, "cannot replace static union active member");
                    }
                    return false;
                }
                return true;
            }
            {
                MinicStaticRecordDesignator suffix;
                const MinicRecord *selected_record;
                const MinicRecordField *leaf_field;
                size_t relative_slot;
                size_t suffix_index;

                if (!minic_type_is_record(field->type)) {
                    minic_parser_error(
                        parser,
                        "promoted noncanonical union designator requires a record active member");
                    return false;
                }
                selected_record =
                    minic_c0_program_record(parser->program, field->type.record_id);
                if (selected_record == NULL || !selected_record->is_complete) {
                    minic_parser_error(
                        parser,
                        "promoted noncanonical union designator requires a complete record member");
                    return false;
                }
                (void)memset(&suffix, 0, sizeof(suffix));
                suffix.depth = designator->depth - (depth + 1U);
                for (suffix_index = 0U; suffix_index < suffix.depth; ++suffix_index) {
                    suffix.field_indices[suffix_index] =
                        designator->field_indices[depth + 1U + suffix_index];
                }
                if (!static_record_designator_leaf_slot(parser->program,
                                                        selected_record,
                                                        &suffix,
                                                        &relative_slot,
                                                        &leaf_field) ||
                    slot_begin > SIZE_MAX - relative_slot ||
                    !overwrite_static_zero_field_value(
                        parser, object_id, leaf_field, slot_begin + relative_slot)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "cannot replace promoted static union designator leaf");
                    }
                    return false;
                }
            }
            return true;
        }
        {
            size_t relative;

            if (!minic_c0_global_record_field_initializer_slot(
                    parser->program, current_record, field_index, &relative) ||
                total > SIZE_MAX - relative) {
                return true;
            }
            total += relative;
        }
        if (depth + 1U == designator->depth) {
            return true;
        }
        if (!minic_type_is_record(field->type)) {
            return true;
        }
        current_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (current_record == NULL || !current_record->is_complete) {
            return true;
        }
    }
    return true;
}

static bool append_static_record_designator_value(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  const size_t *field_indices,
                                                  size_t depth) {
    MinicRecord record_snapshot;
    MinicRecordId record_id;
    const MinicRecordField *field;
    size_t field_index;
    size_t field_limit;
    size_t selected_index;

    if (parser == NULL || record == NULL || !record->is_complete || field_indices == NULL ||
        depth == 0U || record < parser->program->records ||
        record >= parser->program->records + parser->program->record_count) {
        return false;
    }
    record_id = (MinicRecordId)(record - parser->program->records);
    record_snapshot = *record;
    record = &record_snapshot;
    field_limit = record->field_count;
    selected_index = field_indices[0];
    if (selected_index >= field_limit) {
        return false;
    }
    if (record->is_union) {
        MinicRecordId union_record_id;
        size_t union_base_slot;

        union_record_id = record_id;
        union_base_slot = parser->program->global_objects[object_id].initializer_count;
        if (!minic_c0_global_object_select_union_member(
                parser->program, object_id, union_base_slot, union_record_id, selected_index)) {
            minic_parser_error(parser, "cannot record static union active member");
            return false;
        }
    } else {
        for (field_index = 0U; field_index < selected_index; ++field_index) {
            if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                minic_parser_error(parser, "cannot zero-fill static record designator prefix");
                return false;
            }
        }
    }
    field = &record->fields[selected_index];
    if (field->element_count == 0U || field->is_flexible_array) {
        minic_parser_error(parser, "unsupported static record designator field");
        return false;
    }
    if (depth == 1U) {
        if (field->element_count == 1U && !field->is_array) {
            if (!minic_parser_parse_static_storage_initializer_value(
                    parser, object_id, field->type)) {
                return false;
            }
        } else if (field->is_array && minic_type_is_char_integer(field->type) &&
                   parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            if (!minic_parser_add_bounded_string_literal_initializer(
                    parser, object_id, field->element_count)) {
                return false;
            }
        } else if (!parse_static_forward_array_initializer(
                       parser, object_id, field->type, field->element_count, false, NULL)) {
            return false;
        }
    } else {
        const MinicRecord *nested_record;

        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            minic_parser_error(
                parser, "nested static record designator path requires scalar record members");
            return false;
        }
        nested_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (nested_record == NULL ||
            !append_static_record_designator_value(
                parser, object_id, nested_record, field_indices + 1U, depth - 1U)) {
            return false;
        }
    }
    if (!record->is_union) {
        for (field_index = selected_index + 1U; field_index < field_limit; ++field_index) {
            if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                minic_parser_error(parser, "cannot zero-fill static record designator suffix");
                return false;
            }
        }
    }
    return true;
}

static bool append_static_record_array_element_designator_value(MinicParser *parser,
                                                                MinicGlobalObjectId object_id,
                                                                const MinicRecordField *field,
                                                                size_t element_index) {
    size_t index;

    if (parser == NULL || field == NULL || !field->is_array || field->is_bit_field ||
        field->is_flexible_array || field->element_count == 0U ||
        element_index >= field->element_count) {
        return false;
    }
    for (index = 0U; index < element_index; ++index) {
        if (!append_static_constant_zero(parser, object_id, field->type)) {
            minic_parser_error(parser, "cannot zero-fill static record array designator prefix");
            return false;
        }
    }
    if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, field->type)) {
        return false;
    }
    for (index = element_index + 1U; index < field->element_count; ++index) {
        if (!append_static_constant_zero(parser, object_id, field->type)) {
            minic_parser_error(parser, "cannot zero-fill static record array designator suffix");
            return false;
        }
    }
    return true;
}

static bool overwrite_static_zero_record_constant(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  size_t record_base_slot);

static bool overwrite_static_zero_record_value(MinicParser *parser,
                                               MinicGlobalObjectId object_id,
                                               MinicType type,
                                               const MinicRecord *record,
                                               size_t record_base_slot);

static bool overwrite_static_zero_array_constant(MinicParser *parser,
                                                 MinicGlobalObjectId object_id,
                                                 MinicType element_type,
                                                 size_t element_count,
                                                 size_t array_base_slot);

static bool overwrite_static_zero_field_value(MinicParser *parser,
                                              MinicGlobalObjectId object_id,
                                              const MinicRecordField *field,
                                              size_t field_base_slot) {
    if (parser == NULL || field == NULL || field->element_count == 0U || field->is_flexible_array) {
        return false;
    }
    if (field->element_count == 1U && !field->is_array) {
        if (minic_type_is_integer(field->type) || minic_type_is_pointer(field->type)) {
            return parse_static_scalar_constant_at(
                parser, object_id, field->type, true, field_base_slot);
        }
        if (minic_type_is_record(field->type)) {
            const MinicRecord *nested_record;

            nested_record = minic_c0_program_record(parser->program, field->type.record_id);
            return nested_record != NULL &&
                   overwrite_static_zero_record_value(
                       parser, object_id, field->type, nested_record, field_base_slot);
        }
    }
    if (field->is_array && minic_type_is_char_integer(field->type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        return minic_parser_replace_zero_bounded_string_literal_initializer(
            parser, object_id, field_base_slot, field->element_count);
    }
    if (field->is_array) {
        return overwrite_static_zero_array_constant(
            parser, object_id, field->type, field->element_count, field_base_slot);
    }
    minic_parser_error(parser, "backward aggregate designator requires a supported zero subobject");
    return false;
}

static bool overwrite_static_zero_array_element(MinicParser *parser,
                                                MinicGlobalObjectId object_id,
                                                MinicType element_type,
                                                size_t element_base_slot) {
    if (minic_type_is_integer(element_type) || minic_type_is_pointer(element_type)) {
        return parse_static_scalar_constant_at(
            parser, object_id, element_type, true, element_base_slot);
    }
    if (minic_type_is_record(element_type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, element_type.record_id);
        return record != NULL && overwrite_static_zero_record_value(
                                     parser, object_id, element_type, record, element_base_slot);
    }
    if (minic_type_is_array(element_type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(parser->program, element_type.array_type_id);
        return array_type != NULL && array_type->element_count != 0U &&
               overwrite_static_zero_array_constant(parser,
                                                    object_id,
                                                    array_type->element_type,
                                                    array_type->element_count,
                                                    element_base_slot);
    }
    minic_parser_error(parser, "unsupported backward static array element type");
    return false;
}

static bool overwrite_static_zero_array_constant(MinicParser *parser,
                                                 MinicGlobalObjectId object_id,
                                                 MinicType element_type,
                                                 size_t element_count,
                                                 size_t array_base_slot) {
    size_t element_slots;
    size_t next_index;

    if (parser == NULL || element_count == 0U ||
        !minic_c0_global_initializer_slot_count(parser->program, element_type, &element_slots) ||
        parser->current.kind != MINIC_TOKEN_LBRACE || !minic_parser_advance(parser)) {
        return false;
    }
    next_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t first;
        size_t last;
        size_t element_base_slot;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!minic_parser_parse_array_designator(parser, element_count, false, &first, &last)) {
                return false;
            }
            if (first != last) {
                minic_parser_error(
                    parser,
                    "GNU range designators in backward static arrays are not supported yet");
                return false;
            }
        } else {
            first = next_index;
            last = first;
        }
        if (first >= element_count ||
            (element_slots != 0U && first > (SIZE_MAX - array_base_slot) / element_slots)) {
            minic_parser_error(parser, "backward static array initializer index is out of range");
            return false;
        }
        element_base_slot = array_base_slot + first * element_slots;
        if (!overwrite_static_zero_array_element(
                parser, object_id, element_type, element_base_slot)) {
            return false;
        }
        next_index = first + 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in backward static array initializer");
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_RBRACE, "expected '}' after backward static array initializer");
}

static bool overwrite_static_zero_record_value(MinicParser *parser,
                                               MinicGlobalObjectId object_id,
                                               MinicType type,
                                               const MinicRecord *record,
                                               size_t record_base_slot) {
    if (parser == NULL || record == NULL || !record->is_complete || !minic_type_is_record(type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicParser probe;
        MinicType explicit_type;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_LPAREN) {
            if (!minic_parser_advance(parser) ||
                !overwrite_static_zero_record_value(
                    parser, object_id, type, record, record_base_slot) ||
                !minic_parser_expect(parser,
                                     MINIC_TOKEN_RPAREN,
                                     "expected ')' after grouped static record initializer")) {
                return false;
            }
            return true;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_type_name(parser, &explicit_type) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' after static compound literal type")) {
            return false;
        }
        if (!minic_type_is_record(explicit_type) ||
            !minic_type_assignment_compatible(type, explicit_type)) {
            minic_parser_error(parser, "static record compound literal type mismatch");
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_LBRACE) {
            minic_parser_error(parser, "static record compound literal requires initializer list");
            return false;
        }
    }
    return overwrite_static_zero_record_constant(parser, object_id, record, record_base_slot);
}

static bool overwrite_static_zero_record_constant(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  const MinicRecord *record,
                                                  size_t record_base_slot) {
    size_t field_index;
    size_t field_limit;

    if (parser == NULL || record == NULL || !record->is_complete ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    field_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        size_t relative_slot;
        size_t field_base_slot;

        if (parser->current.kind == MINIC_TOKEN_DOT) {
            MinicStaticRecordDesignator designator;
            bool handled_union_zero;

            (void)memset(&designator, 0, sizeof(designator));
            if (!parse_static_record_designator_path(parser, record, &designator) ||
                designator.depth != 1U || designator.has_array_index) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser,
                        "backward aggregate initializer requires a direct unambiguous member");
                }
                return false;
            }
            field_index = designator.field_indices[0];
            if (record->is_union && field_index != 0U) {
                handled_union_zero = false;
                if (!try_overwrite_static_zero_noncanonical_union_designator(parser,
                                                                             object_id,
                                                                             record,
                                                                             &designator,
                                                                             record_base_slot,
                                                                             &handled_union_zero) ||
                    !handled_union_zero) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "cannot replace backward static union active member");
                    }
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_COMMA &&
                    !minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                    minic_parser_error(
                        parser, "backward union initializer may initialize only one active member");
                    return false;
                }
                return minic_parser_advance(parser);
            }
        }
        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many backward aggregate initializer fields");
            return false;
        }
        field = &record->fields[field_index];
        if (!minic_c0_global_record_field_initializer_slot(
                parser->program, record, field_index, &relative_slot) ||
            record_base_slot > SIZE_MAX - relative_slot) {
            minic_parser_error(parser, "cannot locate backward aggregate initializer field slot");
            return false;
        }
        field_base_slot = record_base_slot + relative_slot;
        if (!overwrite_static_zero_field_value(parser, object_id, field, field_base_slot)) {
            return false;
        }
        field_index += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in backward aggregate initializer");
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_RBRACE, "expected '}' after backward aggregate initializer");
}

static bool parse_static_union_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicRecord *record) {
    MinicStaticRecordDesignator designator;
    const MinicRecordField *field;
    MinicRecordId record_id;
    size_t selected_index;
    size_t union_base_slot;
    bool has_designator;

    if (parser == NULL || record == NULL || !record->is_complete || !record->is_union ||
        record->field_count == 0U || object_id >= parser->program->global_object_count ||
        record < parser->program->records ||
        record >= parser->program->records + parser->program->record_count ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in union initializer")) {
        return false;
    }
    record_id = (MinicRecordId)(record - parser->program->records);
    union_base_slot = parser->program->global_objects[object_id].initializer_count;
    if (parser->current.kind == MINIC_TOKEN_RBRACE) {
        if (!minic_c0_global_object_select_union_member(
                parser->program, object_id, union_base_slot, record_id, 0U) ||
            !append_static_field_zeros(parser, object_id, &record->fields[0])) {
            return false;
        }
        return minic_parser_advance(parser);
    }

    (void)memset(&designator, 0, sizeof(designator));
    has_designator = parser->current.kind == MINIC_TOKEN_DOT;
    if (has_designator) {
        if (!parse_static_record_designator_path(parser, record, &designator) ||
            designator.depth == 0U) {
            return false;
        }
        selected_index = designator.field_indices[0];
    } else {
        selected_index = 0U;
    }
    if (selected_index >= record->field_count ||
        !minic_c0_global_object_select_union_member(
            parser->program, object_id, union_base_slot, record_id, selected_index)) {
        minic_parser_error(parser, "cannot select static union initializer member");
        return false;
    }
    field = &record->fields[selected_index];
    if (field->element_count == 0U || field->is_flexible_array) {
        minic_parser_error(parser, "unsupported static union initializer member");
        return false;
    }

    if (has_designator && designator.has_array_index) {
        if (designator.depth != 1U || !field->is_array || field->is_bit_field ||
            designator.array_index >= field->element_count ||
            !append_static_record_array_element_designator_value(
                parser, object_id, field, designator.array_index)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "cannot initialize designated static union array member");
            }
            return false;
        }
    } else if (has_designator && designator.depth > 1U) {
        const MinicRecord *nested_record;

        if (field->element_count != 1U || field->is_array || field->is_bit_field ||
            !minic_type_is_record(field->type)) {
            minic_parser_error(parser, "nested static union designator requires a record member");
            return false;
        }
        nested_record = minic_c0_program_record(parser->program, field->type.record_id);
        if (nested_record == NULL ||
            !append_static_record_designator_value(parser,
                                                   object_id,
                                                   nested_record,
                                                   designator.field_indices + 1U,
                                                   designator.depth - 1U)) {
            return false;
        }
    } else if (field->element_count == 1U && !field->is_array) {
        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, field->type)) {
            return false;
        }
    } else if (field->is_array && minic_type_is_char_integer(field->type) &&
               parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_parser_add_bounded_string_literal_initializer(
                parser, object_id, field->element_count)) {
            return false;
        }
    } else if (!parse_static_forward_array_initializer(
                   parser, object_id, field->type, field->element_count, false, NULL)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_RBRACE) {
        minic_parser_error(parser, "union initializer may initialize only one active member");
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_static_record_constant(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         const MinicRecord *record) {
    MinicRecord record_snapshot;
    MinicRecordId record_id;
    size_t field_index;
    size_t field_limit;
    size_t materialized_field_limit;
    size_t record_base_slot;

    if (parser == NULL || record == NULL || !record->is_complete ||
        record < parser->program->records ||
        record >= parser->program->records + parser->program->record_count ||
        object_id >= parser->program->global_object_count) {
        return false;
    }
    record_id = (MinicRecordId)(record - parser->program->records);
    /* program->records is a growable arena. Initializer expressions such as
       sizeof(struct { ... }) may append anonymous records and realloc that arena.
       Snapshot the complete descriptor before recursive expression parsing.
       Its fields storage is independently allocated and immutable after the
       record is completed, so the snapshot remains valid across arena growth. */
    record_snapshot = *record;
    record = &record_snapshot;
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    field_index = 0U;
    materialized_field_limit = 0U;
    record_base_slot = parser->program->global_objects[object_id].initializer_count;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicStaticRecordDesignator designator;
        const MinicRecordField *field;
        bool has_designator;
        bool overwrite_materialized_field;

        has_designator = false;
        (void)memset(&designator, 0, sizeof(designator));
        if (parser->current.kind == MINIC_TOKEN_DOT) {
            size_t designator_index;

            if (!parse_static_record_designator_path(parser, record, &designator)) {
                return false;
            }
            designator_index = designator.field_indices[0];
            while (materialized_field_limit < designator_index) {
                if (!append_static_field_zeros(
                        parser, object_id, &record->fields[materialized_field_limit])) {
                    minic_parser_error(parser,
                                       "cannot zero-fill skipped static record designator fields");
                    return false;
                }
                materialized_field_limit += 1U;
            }
            field_index = designator_index;
            has_designator = true;
        }
        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many nested static record initializers");
            return false;
        }
        field = &record->fields[field_index];
        if (field->is_flexible_array) {
            const MinicGlobalObject *object;
            size_t flexible_element_count;
            bool parsed_flexible_tail;

            object = minic_c0_program_global_object(parser->program, object_id);
            flexible_element_count = 0U;
            if (object == NULL || !minic_type_is_record(object->type) ||
                object->type.record_id != record_id ||
                record->is_union || field_index + 1U != record->field_count ||
                field_index != materialized_field_limit || field->is_bit_field ||
                (has_designator && (designator.depth != 1U || designator.has_array_index)) ||
                parser->current.kind != MINIC_TOKEN_LBRACE ||
                !minic_parser_inspect_array_initializer_extent(parser, &flexible_element_count) ||
                flexible_element_count == 0U ||
                !minic_c0_global_object_set_flexible_array_initializer_count(
                    parser->program, object_id, flexible_element_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser,
                        "GNU static flexible array initializer requires a supported direct tail");
                }
                return false;
            }
            /* Publish the inspected FAM extent before parsing its elements. Aggregate relocation
             * validation resolves a scalar slot through the complete top-level object shape; the
             * flexible tail therefore has to be visible while its transaction is in progress. */
            if (minic_type_is_integer(field->type) || minic_type_is_pointer(field->type)) {
                parsed_flexible_tail = parse_static_scalar_array_transaction(
                    parser, object_id, field->type, flexible_element_count, false);
            } else {
                parsed_flexible_tail = parse_static_forward_array_initializer(
                    parser, object_id, field->type, flexible_element_count, false, NULL);
            }
            if (!parsed_flexible_tail) {
                /* Parsing the translation unit will fail, but restore the object invariant so
                 * diagnostics and cleanup never observe a committed extent without its payload. */
                parser->program->global_objects[object_id].flexible_array_initializer_count = 0U;
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser,
                        "GNU static flexible array initializer requires a supported direct tail");
                }
                return false;
            }
            materialized_field_limit += 1U;
            field_index += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser,
                                   "expected ',' or '}' after static flexible array initializer");
                return false;
            }
            continue;
        }
        if (field->element_count == 0U) {
            minic_parser_error(parser, "unsupported nested static record field");
            return false;
        }
        overwrite_materialized_field = field_index < materialized_field_limit;
        if (has_designator && designator.has_array_index) {
            size_t relative_slot;
            size_t field_base_slot;
            size_t element_base_slot;
            size_t element_slots;

            if (designator.depth != 1U || !field->is_array || field->is_bit_field ||
                field->is_flexible_array || designator.array_index >= field->element_count) {
                minic_parser_error(parser,
                                   "static record array member designator currently requires a "
                                   "direct fixed array field");
                return false;
            }
            if (overwrite_materialized_field) {
                if (!minic_c0_global_record_field_initializer_slot(
                        parser->program, record, field_index, &relative_slot) ||
                    !minic_c0_global_initializer_slot_count(
                        parser->program, field->type, &element_slots) ||
                    record_base_slot > SIZE_MAX - relative_slot) {
                    minic_parser_error(parser,
                                       "cannot locate static record array member designator field");
                    return false;
                }
                field_base_slot = record_base_slot + relative_slot;
                if (element_slots != 0U &&
                    designator.array_index > (SIZE_MAX - field_base_slot) / element_slots) {
                    minic_parser_error(parser,
                                       "static record array member designator slot overflows");
                    return false;
                }
                element_base_slot = field_base_slot + designator.array_index * element_slots;
                if (!overwrite_static_zero_array_element(
                        parser, object_id, field->type, element_base_slot)) {
                    return false;
                }
            } else {
                if (field_index != materialized_field_limit ||
                    !append_static_record_array_element_designator_value(
                        parser, object_id, field, designator.array_index)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "cannot materialize static record array member designator");
                    }
                    return false;
                }
                materialized_field_limit += 1U;
            }
        } else if (has_designator && designator.depth > 1U) {
            if (overwrite_materialized_field) {
                size_t relative_slot;
                size_t slot_index;
                const MinicRecordField *leaf_field;
                bool handled_union_zero;

                {
                    const MinicRecord *identity_record;

                    identity_record = minic_c0_program_record(parser->program, record_id);
                    if (identity_record == NULL ||
                        !try_overwrite_static_zero_noncanonical_union_designator(
                            parser,
                            object_id,
                            identity_record,
                            &designator,
                            record_base_slot,
                            &handled_union_zero)) {
                        return false;
                    }
                }
                if (handled_union_zero) {
                    /* The selected noncanonical union member denotes the same
                     * already-materialized all-zero bytes. No flattened scalar
                     * rewrite is needed or representable. */
                } else {
                    if (!static_record_designator_leaf_slot(
                            parser->program, record, &designator, &relative_slot, &leaf_field) ||
                        record_base_slot > SIZE_MAX - relative_slot) {
                        minic_parser_error(parser,
                                           "cannot locate backward nested static record "
                                           "designator leaf");
                        return false;
                    }
                    slot_index = record_base_slot + relative_slot;
                    if (!overwrite_static_zero_field_value(
                            parser, object_id, leaf_field, slot_index)) {
                        return false;
                    }
                }
            } else {
                const MinicRecord *nested_record;

                if (field_index != materialized_field_limit || field->element_count != 1U ||
                    field->is_array || field->is_bit_field || !minic_type_is_record(field->type)) {
                    minic_parser_error(
                        parser,
                        "forward nested static record designator requires a scalar record field");
                    return false;
                }
                nested_record = minic_c0_program_record(parser->program, field->type.record_id);
                if (nested_record == NULL ||
                    !append_static_record_designator_value(parser,
                                                           object_id,
                                                           nested_record,
                                                           designator.field_indices + 1U,
                                                           designator.depth - 1U)) {
                    return false;
                }
                materialized_field_limit += 1U;
            }
        } else if (overwrite_materialized_field) {
            size_t relative_slot;
            size_t slot_index;

            if (!minic_c0_global_record_field_initializer_slot(
                    parser->program, record, field_index, &relative_slot) ||
                record_base_slot > SIZE_MAX - relative_slot) {
                minic_parser_error(parser, "cannot locate backward static record designator slot");
                return false;
            }
            slot_index = record_base_slot + relative_slot;
            if (!overwrite_static_zero_field_value(parser, object_id, field, slot_index)) {
                return false;
            }
        } else {
            if (field_index != materialized_field_limit) {
                minic_parser_error(parser, "internal error: invalid static record materialization");
                return false;
            }
            if (field->element_count == 1U && !field->is_array) {
                if (!minic_parser_parse_static_storage_initializer_value(
                        parser, object_id, field->type)) {
                    return false;
                }
            } else if (field->is_array && minic_type_is_char_integer(field->type) &&
                       parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
                if (!minic_parser_add_bounded_string_literal_initializer(
                        parser, object_id, field->element_count)) {
                    return false;
                }
            } else if (!parse_static_forward_array_initializer(
                           parser, object_id, field->type, field->element_count, false, NULL)) {
                return false;
            }
            materialized_field_limit += 1U;
        }
        field_index += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in nested static record initializer");
            return false;
        }
    }
    while (materialized_field_limit < field_limit) {
        if (!append_static_field_zeros(
                parser, object_id, &record->fields[materialized_field_limit])) {
            minic_parser_error(parser, "cannot zero-fill nested static record initializer");
            return false;
        }
        materialized_field_limit += 1U;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer");
}

bool minic_parser_parse_static_storage_initializer_value(MinicParser *parser,
                                                         MinicGlobalObjectId object_id,
                                                         MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return parse_static_scalar_constant(parser, object_id, type);
    }
    if (minic_type_is_array(type)) {
        return parse_static_array_constant(
            parser, object_id, minic_c0_program_array_type(parser->program, type.array_type_id));
    }
    if (minic_type_is_record(type)) {
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            MinicParser probe;
            MinicType explicit_type;

            probe = *parser;
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            if (probe.current.kind == MINIC_TOKEN_LPAREN) {
                if (!minic_parser_advance(parser) ||
                    !minic_parser_parse_static_storage_initializer_value(parser, object_id, type) ||
                    !minic_parser_expect(parser,
                                         MINIC_TOKEN_RPAREN,
                                         "expected ')' after grouped static record initializer")) {
                    return false;
                }
                return true;
            }
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_type_name(parser, &explicit_type) ||
                !minic_parser_expect(parser,
                                     MINIC_TOKEN_RPAREN,
                                     "expected ')' after static compound literal type")) {
                return false;
            }
            if (!minic_type_is_record(explicit_type) ||
                !minic_type_assignment_compatible(type, explicit_type)) {
                minic_parser_error(parser, "static record compound literal type mismatch");
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_LBRACE) {
                minic_parser_error(parser,
                                   "static record compound literal requires initializer list");
                return false;
            }
        }
        {
            const MinicRecord *record;

            record = minic_c0_program_record(parser->program, type.record_id);
            if (record == NULL) {
                return false;
            }
            return record->is_union ? parse_static_union_constant(parser, object_id, record)
                                    : parse_static_record_constant(parser, object_id, record);
        }
    }
    minic_parser_error(parser, "unsupported nested static aggregate initializer type");
    return false;
}

static bool
parse_static_nested_record_object(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;

    if (!begin_static_object_definition(parser, type, name_span, &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse nested static record initializer");
        }
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        MinicSourceSpan next_name_span;

        if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected static aggregate declarator after ','");
            return false;
        }
        next_name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            return parse_static_nested_record_object(parser, type, next_name_span);
        }
        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            return parse_static_zero_definition(parser, type, next_name_span);
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            return minic_parser_parse_static_zero_declaration_list_after_head(parser,
                                                                               type,
                                                                               type,
                                                                               next_name_span,
                                                                               NULL,
                                                                               0U,
                                                                               false,
                                                                               0U);
        }
        minic_parser_error(parser,
                           "expected '=', ',' or ';' after static aggregate declarator");
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}

static bool probe_static_array_designator_extent(MinicParser *probe, size_t *first, size_t *last) {
    MinicSemanticSnapshot snapshot;
    MinicC0Program *program;

    if (probe == NULL || first == NULL || last == NULL || probe->program == NULL) {
        return false;
    }
    program = probe->program;
    snapshot = minic_semantic_snapshot_capture(program);
    if (!minic_parser_parse_array_designator(probe, 0U, true, first, last)) {
        return false;
    }
    if (!minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, program)) {
        minic_parser_error(probe,
                           "inferred aggregate array designator probe requires a "
                           "side-effect-free integer constant expression");
        return false;
    }
    return true;
}

bool minic_parser_inspect_array_initializer_extent(MinicParser *parser, size_t *element_count) {
    MinicParser probe;
    size_t extent;
    size_t next_index;

    if (parser == NULL || element_count == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    extent = 0U;
    next_index = 0U;
    while (probe.current.kind != MINIC_TOKEN_RBRACE) {
        size_t first;
        size_t last;
        size_t brace_depth;
        size_t parenthesis_depth;
        size_t bracket_depth;

        first = next_index;
        last = next_index;
        if (probe.current.kind == MINIC_TOKEN_LBRACKET &&
            !probe_static_array_designator_extent(&probe, &first, &last)) {
            return false;
        }
        if (last == SIZE_MAX) {
            minic_parser_error(&probe, "inferred aggregate array initializer extent overflows");
            return false;
        }
        next_index = last + 1U;
        if (next_index > extent) {
            extent = next_index;
        }

        brace_depth = 0U;
        parenthesis_depth = 0U;
        bracket_depth = 0U;
        while (probe.current.kind != MINIC_TOKEN_EOF) {
            if (brace_depth == 0U && parenthesis_depth == 0U && bracket_depth == 0U &&
                (probe.current.kind == MINIC_TOKEN_COMMA ||
                 probe.current.kind == MINIC_TOKEN_RBRACE)) {
                break;
            }
            switch (probe.current.kind) {
            case MINIC_TOKEN_LBRACE:
                brace_depth += 1U;
                break;
            case MINIC_TOKEN_RBRACE:
                if (brace_depth == 0U) {
                    return false;
                }
                brace_depth -= 1U;
                break;
            case MINIC_TOKEN_LPAREN:
                parenthesis_depth += 1U;
                break;
            case MINIC_TOKEN_RPAREN:
                if (parenthesis_depth == 0U) {
                    return false;
                }
                parenthesis_depth -= 1U;
                break;
            case MINIC_TOKEN_LBRACKET:
                bracket_depth += 1U;
                break;
            case MINIC_TOKEN_RBRACKET:
                if (bracket_depth == 0U) {
                    return false;
                }
                bracket_depth -= 1U;
                break;
            default:
                break;
            }
            if (!minic_parser_advance(&probe)) {
                return false;
            }
        }
        if (probe.current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            if (probe.current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (probe.current.kind != MINIC_TOKEN_RBRACE) {
            return false;
        }
    }
    *element_count = extent;
    return true;
}

static bool static_record_array_declaration_compatible(const MinicC0Program *program,
                                                       MinicType existing_type,
                                                       MinicType element_type,
                                                       size_t declared_count,
                                                       bool declared_incomplete) {
    const MinicArrayType *existing_array;

    if (program == NULL || !minic_type_is_array(existing_type)) {
        return false;
    }
    existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
    if (existing_array == NULL || !minic_type_equal(existing_array->element_type, element_type)) {
        return false;
    }
    if (declared_incomplete ||
        (existing_array->element_count == 0U && !existing_array->is_zero_length)) {
        return true;
    }
    return !existing_array->is_zero_length && existing_array->element_count == declared_count;
}

static bool parse_static_record_array(MinicParser *parser,
                                      MinicType element_type,
                                      MinicSourceSpan name_span,
                                      char *section_name,
                                      size_t section_capacity,
                                      size_t *section_name_length,
                                      bool *has_section,
                                      size_t *explicit_alignment) {
    const MinicRecord *record;
    MinicType nested_element_type;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId existing_id;
    size_t declared_count;
    bool inferred_bound;
    bool multidimensional;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {
        return false;
    }
    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union) {
        minic_parser_error(parser, "static record array requires a complete struct type");
        return false;
    }
    /* GNU empty structs are complete zero-size object types. Arrays of them
       likewise occupy zero bytes but remain valid semantic objects; the target
       DataLayout already carries zero-size record behavior. */

    declared_count = 0U;
    inferred_bound = false;
    multidimensional = false;
    nested_element_type = element_type;
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &declared_count)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        bool nested_is_array;

        if (inferred_bound) {
            minic_parser_error(
                parser, "inferred multi-dimensional static record arrays are not supported yet");
            return false;
        }
        nested_is_array = false;
        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, false, &nested_element_type, &nested_is_array) ||
            !nested_is_array) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "cannot build nested static record array declarator type");
            }
            return false;
        }
        multidimensional = true;
    }
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }

    existing_id = minic_parser_find_global_object_entity(parser, name_span);
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;

        if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
            MinicType tentative_element_type;

            tentative_element_type = multidimensional ? nested_element_type : element_type;
            if ((inferred_bound && !minic_c0_program_add_incomplete_array_type(
                                       parser->program, tentative_element_type, &object_type)) ||
                (!inferred_bound &&
                 !minic_c0_program_add_array_type(
                     parser->program, tentative_element_type, declared_count, &object_type)) ||
                !minic_c0_program_add_tentative_global_object(parser->program,
                                                              parser->source +
                                                                  name_span.begin.offset,
                                                              minic_parser_span_length(name_span),
                                                              object_type,
                                                              true,
                                                              minic_type_is_const(element_type),
                                                              &object_id)) {
                minic_parser_error(parser,
                                   "cannot create static record array tentative definition");
                return false;
            }
        } else {
            existing = minic_c0_program_global_object(parser->program, existing_id);
            existing_array =
                existing != NULL && minic_type_is_array(existing->type)
                    ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                    : NULL;
            if (existing == NULL || existing_array == NULL || !existing->is_internal ||
                !static_record_array_declaration_compatible(parser->program,
                                                            existing->type,
                                                            element_type,
                                                            declared_count,
                                                            inferred_bound) ||
                (!inferred_bound && existing_array->element_count == 0U &&
                 !existing_array->is_zero_length &&
                 !minic_c0_program_complete_array_type(
                     parser->program, existing->type, declared_count)) ||
                !minic_c0_global_object_merge_tentative(parser->program, existing_id)) {
                minic_parser_error(parser, "conflicting static record array tentative definition");
                return false;
            }
            object_id = existing_id;
        }
        if (!apply_static_object_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment)) {
            minic_parser_error(parser, "cannot persist static record array declaration metadata");
            return false;
        }
        return minic_parser_advance(parser);
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||
        parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot inspect static record array initializer");
        }
        return false;
    }
    if (inferred_bound) {
        if (!minic_parser_inspect_array_initializer_extent(parser, &declared_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot infer static record array initializer extent");
            }
            return false;
        }

    }

    if (existing_id != MINIC_GLOBAL_OBJECT_INVALID) {
        MinicGlobalObject *existing;
        const MinicArrayType *existing_array;

        if (multidimensional) {
            minic_parser_error(
                parser, "multi-dimensional static record redeclarations are not supported yet");
            return false;
        }
        existing = &parser->program->global_objects[existing_id];
        existing_array =
            minic_type_is_array(existing->type)
                ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                : NULL;
        if (existing_array == NULL || !existing->is_internal || !existing->is_tentative ||
            !static_record_array_declaration_compatible(
                parser->program, existing->type, element_type, declared_count, false) ||
            (existing_array->element_count == 0U && !existing_array->is_zero_length &&
             (declared_count == 0U
                  ? !minic_c0_program_complete_zero_length_array_type(
                        parser->program, existing->type)
                  : !minic_c0_program_complete_array_type(
                        parser->program, existing->type, declared_count))) ||
            !minic_c0_global_object_begin_definition(parser->program, existing_id)) {
            minic_parser_error(parser, "conflicting static record array definition");
            return false;
        }
        object_id = existing_id;
        object_type = parser->program->global_objects[object_id].type;
    } else {
        if ((declared_count == 0U
                 ? !minic_c0_program_add_zero_length_array_type(
                       parser->program,
                       multidimensional ? nested_element_type : element_type,
                       &object_type)
                 : !minic_c0_program_add_array_type(
                       parser->program,
                       multidimensional ? nested_element_type : element_type,
                       declared_count,
                       &object_type)) ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                true,
                                                minic_type_is_const(element_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create static record array definition");
            return false;
        }
    }
    if (!apply_static_object_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment) ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse static record array initializer");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static record array");
}
static bool parse_static_record(MinicParser *parser,
                                MinicType type,
                                MinicSourceSpan name_span,
                                char *section_name,
                                size_t section_capacity,
                                size_t *section_name_length,
                                bool *has_section,
                                size_t *explicit_alignment) {
    const MinicRecord *record;

    record = minic_c0_program_record(parser->program, type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "static record global requires a complete record type");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        return parse_static_record_array(parser,
                                         type,
                                         name_span,
                                         section_name,
                                         section_capacity,
                                         section_name_length,
                                         has_section,
                                         explicit_alignment);
    }
    return parse_static_nested_record_object(parser, type, name_span);
}

static bool parse_extern_function_pointer_object_declarator(MinicParser *parser,
                                                            MinicType return_type,
                                                            MinicSourceSpan *name_span,
                                                            MinicType *object_type) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || object_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(parser, true, true, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser,
                           "variadic extern function pointer objects are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, object_type)) {
        minic_parser_error(parser, "cannot build extern function pointer object type");
        return false;
    }
    *name_span = declarator.name_span;
    return true;
}

static bool parse_extern_object_declarator(MinicParser *parser,
                                           MinicType base_type,
                                           MinicSourceSpan *name_span,
                                           MinicType *object_type) {
    if (parser == NULL || name_span == NULL || object_type == NULL ||
        !minic_parser_parse_pointer_declarator(parser, base_type, object_type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        return parse_extern_function_pointer_object_declarator(
            parser, *object_type, name_span, object_type);
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected extern object name");
        return false;
    }
    *name_span = parser->current.span;
    return minic_parser_advance(parser);
}

static bool merge_extern_object_declaration(MinicParser *parser,
                                            MinicGlobalObjectId object_id,
                                            MinicType declared_type,
                                            const char *section_name,
                                            size_t section_name_length,
                                            bool has_section,
                                            size_t explicit_alignment,
                                            MinicSymbolVisibility visibility,
                                            bool has_visibility) {
    MinicDeclarationExternalObjectAttributes attributes;
    MinicDeclarationExternalObjectMergeStatus status;

    if (parser == NULL || parser->program == NULL) {
        return false;
    }
    attributes.section_name = section_name;
    attributes.section_name_length = section_name_length;
    attributes.explicit_alignment = explicit_alignment;
    attributes.visibility = visibility;
    attributes.has_section = has_section;
    attributes.has_visibility = has_visibility;
    status = minic_declaration_merge_external_object(
        parser->program, object_id, declared_type, &attributes);
    switch (status) {
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_OK:
        return true;
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_TYPE_CONFLICT:
        minic_parser_error(parser, "conflicting extern object redeclaration");
        return false;
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_ATTRIBUTE_CONFLICT:
        minic_parser_error(parser, "conflicting extern object redeclaration attributes");
        return false;
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_INVALID:
        minic_parser_error(parser, "invalid extern object redeclaration");
        return false;
    case MINIC_DECLARATION_EXTERNAL_OBJECT_MERGE_COMMIT_FAILED:
    default:
        minic_parser_error(parser, "cannot commit extern object redeclaration");
        return false;
    }
}

bool minic_parser_declare_block_scope_extern_object(MinicParser *parser,
                                                    MinicSourceSpan name_span,
                                                    MinicType object_type,
                                                    MinicGlobalObjectId *object_id) {
    MinicGlobalObjectId existing_id;

    if (parser == NULL || object_id == NULL || minic_type_is_void(object_type) ||
        minic_type_is_function(object_type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid block-scope extern object type");
        }
        return false;
    }
    existing_id = minic_parser_find_global_object_entity(parser, name_span);
    if (existing_id != MINIC_GLOBAL_OBJECT_INVALID) {
        if (!merge_extern_object_declaration(parser,
                                             existing_id,
                                             object_type,
                                             NULL,
                                             0U,
                                             false,
                                             0U,
                                             MINIC_SYMBOL_VISIBILITY_DEFAULT,
                                             false)) {
            return false;
        }
        *object_id = existing_id;
        return true;
    }
    {
        MinicDeclarationExternalObjectAttributes attributes;
        MinicDeclarationExternalObjectCreateStatus create_status;

        (void)memset(&attributes, 0, sizeof(attributes));
        attributes.visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
        create_status =
            minic_declaration_create_external_object(parser->program,
                                                     parser->source + name_span.begin.offset,
                                                     minic_parser_span_length(name_span),
                                                     object_type,
                                                     minic_type_is_const(object_type),
                                                     false,
                                                     true,
                                                     &attributes,
                                                     object_id);
        if (create_status != MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_OK) {
            minic_parser_error(parser, "cannot declare block-scope extern object");
            return false;
        }
    }
    return true;
}

bool minic_parser_parse_extern_global_after_head(MinicParser *parser,
                                                 MinicType base_type,
                                                 MinicType first_object_type,
                                                 MinicSourceSpan first_name_span,
                                                 const char *section_name,
                                                 size_t section_name_length,
                                                 bool has_section,
                                                 size_t shared_explicit_alignment,
                                                 MinicSymbolVisibility visibility,
                                                 bool has_visibility) {
    bool first_declarator;

    if (parser == NULL) {
        return false;
    }
    first_declarator = true;
    for (;;) {
        MinicGlobalObjectId object_id;
        MinicSourceSpan name_span;
        MinicType object_type;
        char declarator_section_name[256];
        size_t declarator_section_name_length;
        size_t declarator_explicit_alignment;
        bool declarator_has_section;
        MinicSymbolVisibility declarator_visibility;
        bool declarator_has_visibility;
        bool declarator_is_weak;
        MinicGlobalObjectId declarator_alias_target;
        bool object_was_existing;
        bool is_array;
        MinicType declarator_element_type;
        size_t array_type_begin;

        declarator_section_name_length = section_name_length;
        declarator_explicit_alignment = shared_explicit_alignment;
        declarator_has_section = has_section;
        declarator_visibility = visibility;
        declarator_has_visibility = has_visibility;
        declarator_is_weak = false;
        declarator_alias_target = MINIC_GLOBAL_OBJECT_INVALID;
        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));
        if (has_section) {
            if (section_name == NULL ||
                section_name_length + 1U > sizeof(declarator_section_name)) {
                minic_parser_error(parser, "invalid shared GNU section attribute");
                return false;
            }
            (void)memcpy(declarator_section_name, section_name, section_name_length + 1U);
        }

        if (first_declarator) {
            name_span = first_name_span;
            object_type = first_object_type;
            first_declarator = false;
        } else if (!parse_extern_object_declarator(parser, base_type, &name_span, &object_type)) {
            return false;
        }
        declarator_element_type = object_type;
        if (!minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata_and_alias(
                parser,
                declarator_section_name,
                sizeof(declarator_section_name),
                &declarator_section_name_length,
                &declarator_has_section,
                &declarator_explicit_alignment,
                &declarator_visibility,
                &declarator_has_visibility,
                &declarator_is_weak,
                &declarator_alias_target)) {
            return false;
        }
        if (minic_type_is_function(object_type)) {
            minic_parser_error(parser, "unsupported extern object type");
            return false;
        }
        array_type_begin = parser->program->array_type_count;
        if (!minic_parser_parse_array_declarator_suffix(
                parser, object_type, true, &object_type, &is_array) ||
            !minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata_and_alias(
                parser,
                declarator_section_name,
                sizeof(declarator_section_name),
                &declarator_section_name_length,
                &declarator_has_section,
                &declarator_explicit_alignment,
                &declarator_visibility,
                &declarator_has_visibility,
                &declarator_is_weak,
                &declarator_alias_target)) {
            return false;
        }

        object_id = minic_parser_find_global_object_entity(parser, name_span);
        object_was_existing = object_id != MINIC_GLOBAL_OBJECT_INVALID;
        if (object_was_existing) {
            if (!merge_extern_object_declaration(parser,
                                                 object_id,
                                                 object_type,
                                                 declarator_section_name,
                                                 declarator_section_name_length,
                                                 declarator_has_section,
                                                 declarator_explicit_alignment,
                                                 declarator_visibility,
                                                 declarator_has_visibility)) {
                return false;
            }
            parser->program->array_type_count = array_type_begin;
        } else {
            MinicDeclarationExternalObjectAttributes attributes;
            MinicDeclarationExternalObjectCreateStatus create_status;

            attributes.section_name = declarator_section_name;
            attributes.section_name_length = declarator_section_name_length;
            attributes.explicit_alignment = declarator_explicit_alignment;
            attributes.visibility = declarator_visibility;
            attributes.has_section = declarator_has_section;
            attributes.has_visibility = declarator_has_visibility;
            create_status = minic_declaration_create_external_object(
                parser->program,
                parser->source + name_span.begin.offset,
                minic_parser_span_length(name_span),
                object_type,
                minic_type_is_const(declarator_element_type),
                declarator_is_weak,
                false,
                &attributes,
                &object_id);
            if (create_status != MINIC_DECLARATION_EXTERNAL_OBJECT_CREATE_OK) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot declare extern object");
                }
                return false;
            }
        }
        if (!minic_declaration_mark_file_scope_object(parser->program, object_id)) {
            minic_parser_error(parser, "cannot promote extern object to file scope");
            return false;
        }
        if (declarator_alias_target != MINIC_GLOBAL_OBJECT_INVALID &&
            !minic_c0_global_object_set_alias(
                parser->program, object_id, declarator_alias_target)) {
            minic_parser_error(parser, "invalid GNU object alias declaration");
            return false;
        }
        if (object_was_existing && declarator_is_weak &&
            !minic_c0_global_object_set_weak(parser->program, object_id, true)) {
            minic_parser_error(parser, "GNU weak requires external object linkage");
            return false;
        }

        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after extern object declaration");
}

bool minic_parser_parse_extern_global(MinicParser *parser) {
    MinicSourceSpan first_name_span;
    MinicType base_type;
    MinicType first_object_type;
    char section_name[256];
    size_t section_name_length;
    size_t explicit_alignment;
    bool has_section;

    section_name_length = 0U;
    explicit_alignment = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       sizeof(section_name),
                                                       &section_name_length,
                                                       &has_section,
                                                       &explicit_alignment) ||
        !parse_extern_object_declarator(parser, base_type, &first_name_span, &first_object_type)) {
        return false;
    }
    return minic_parser_parse_extern_global_after_head(parser,
                                                       base_type,
                                                       first_object_type,
                                                       first_name_span,
                                                       section_name,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment,
                                                       MINIC_SYMBOL_VISIBILITY_DEFAULT,
                                                       false);
}

static bool parse_static_pointer_array(MinicParser *parser,
                                       MinicType element_type,
                                       MinicSourceSpan name_span,
                                       char *section_name,
                                       size_t section_capacity,
                                       size_t *section_name_length,
                                       bool *has_section,
                                       size_t *explicit_alignment) {
    MinicType object_type;
    MinicType declared_object_type;
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId existing_id;
    size_t element_count;
    bool inferred_bound;

    element_count = 0U;
    inferred_bound = false;
    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_pointer(element_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser) || !minic_c0_program_add_incomplete_array_type(
                                                 parser->program, element_type, &object_type)) {
            return false;
        }
    } else {
        size_t parsed_element_count;
        bool is_zero_length;

        parsed_element_count = 0U;
        is_zero_length = false;
        if (!minic_parser_parse_record_array_bound(
                parser, &parsed_element_count, &is_zero_length)) {
            return false;
        }
        element_count = is_zero_length ? 0U : parsed_element_count;
        if ((is_zero_length && !minic_c0_program_add_zero_length_array_type(
                                   parser->program, element_type, &object_type)) ||
            (!is_zero_length && !minic_c0_program_add_array_type(
                                    parser->program, element_type, element_count, &object_type))) {
            minic_parser_error(parser, "cannot build static pointer array type");
            return false;
        }
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static pointer arrays are not supported yet");
        return false;
    }
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }

    declared_object_type = object_type;
    existing_id = minic_parser_find_global_object_entity(parser, name_span);
    object_id = MINIC_GLOBAL_OBJECT_INVALID;
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;
        const MinicArrayType *declared_array;
        bool discard_declared;

        if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
            if (!minic_c0_program_add_tentative_global_object(parser->program,
                                                              parser->source +
                                                                  name_span.begin.offset,
                                                              minic_parser_span_length(name_span),
                                                              object_type,
                                                              true,
                                                              minic_type_is_const(element_type),
                                                              &object_id)) {
                minic_parser_error(parser,
                                   "cannot create static pointer array tentative definition");
                return false;
            }
        } else {
            existing = minic_c0_program_global_object(parser->program, existing_id);
            existing_array =
                existing != NULL && minic_type_is_array(existing->type)
                    ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                    : NULL;
            declared_array =
                minic_c0_program_array_type(parser->program, object_type.array_type_id);
            discard_declared =
                object_type.array_type_id + 1U == parser->program->array_type_count &&
                (existing == NULL || existing->type.array_type_id != object_type.array_type_id);
            if (existing == NULL || existing_array == NULL || declared_array == NULL ||
                !existing->is_internal ||
                !minic_type_equal(existing_array->element_type, declared_array->element_type) ||
                existing_array->element_count != declared_array->element_count ||
                !minic_c0_global_object_merge_tentative(parser->program, existing_id)) {
                minic_parser_error(parser, "conflicting static pointer array tentative definition");
                return false;
            }
            object_id = existing_id;
            if (discard_declared &&
                !minic_c0_program_discard_last_array_type(parser->program, object_type)) {
                minic_parser_error(parser,
                                   "cannot retire transient static pointer array declaration");
                return false;
            }
        }
        if (!apply_static_object_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment)) {
            minic_parser_error(parser, "cannot persist static pointer array metadata");
            return false;
        }
        return minic_parser_advance(parser);
    }

    if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                true,
                                                minic_type_is_const(element_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create static pointer array object");
            return false;
        }
    } else {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;
        const MinicArrayType *declared_array;
        size_t declared_count;
        bool discard_declared;

        existing = minic_c0_program_global_object(parser->program, existing_id);
        existing_array =
            existing != NULL && minic_type_is_array(existing->type)
                ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                : NULL;
        declared_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        declared_count = declared_array == NULL ? 0U : declared_array->element_count;
        discard_declared =
            object_type.array_type_id + 1U == parser->program->array_type_count &&
            (existing == NULL || existing->type.array_type_id != object_type.array_type_id);
        if (existing == NULL || existing_array == NULL || declared_array == NULL ||
            !existing->is_internal || !existing->is_tentative ||
            !minic_type_equal(existing_array->element_type, declared_array->element_type) ||
            (existing_array->element_count != 0U && declared_count != 0U &&
             existing_array->element_count != declared_count) ||
            (existing_array->element_count == 0U && declared_count != 0U &&
             !minic_c0_program_complete_array_type(
                 parser->program, existing->type, declared_count)) ||
            !minic_c0_global_object_begin_definition(parser->program, existing_id)) {
            minic_parser_error(parser, "conflicting static pointer array definition");
            return false;
        }
        object_id = existing_id;
        object_type = parser->program->global_objects[object_id].type;
        existing_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        if (existing_array == NULL) {
            return false;
        }
        element_count = existing_array->element_count;
        inferred_bound = element_count == 0U && !existing_array->is_zero_length;
        if (discard_declared &&
            !minic_c0_program_discard_last_array_type(parser->program, declared_object_type)) {
            minic_parser_error(parser, "cannot retire transient static pointer array definition");
            return false;
        }
    }

    if (!apply_static_object_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment)) {
        minic_parser_error(parser, "cannot persist static pointer array metadata");
        return false;
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_EQUAL, "expected '=' after static pointer array")) {
        return false;
    }
    {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        if (array_type == NULL) {
            minic_parser_error(parser, "invalid static pointer array type");
            return false;
        }
        if (array_type->is_zero_length) {
            if (!minic_parser_expect(
                    parser, MINIC_TOKEN_LBRACE, "expected '{' for zero-length array initializer") ||
                !minic_parser_expect(
                    parser, MINIC_TOKEN_RBRACE, "zero-length array initializer must be empty")) {
                return false;
            }
        } else if (!parse_static_scalar_array_transaction(
                       parser, object_id, element_type, element_count, inferred_bound)) {
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static pointer array");
}

static bool static_object_type_is_read_only(const MinicC0Program *program, MinicType type) {
    const MinicArrayType *array_type;

    if (!minic_type_is_array(type)) {
        return minic_type_is_const(type);
    }
    array_type = minic_c0_program_array_type(program, type.array_type_id);
    return array_type != NULL && static_object_type_is_read_only(program, array_type->element_type);
}

static bool parse_static_zero_definition(MinicParser *parser,
                                         MinicType object_type,
                                         MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    const MinicGlobalObject *existing;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
         !minic_type_is_record(object_type) && !minic_type_is_array(object_type))) {
        return false;
    }
    if (!minic_c0_type_is_complete_object(parser->program, object_type)) {
        minic_parser_error(parser, "static object requires a complete object type");
        return false;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_tentative_global_object(
                parser->program,
                parser->source + name_span.begin.offset,
                minic_parser_span_length(name_span),
                object_type,
                true,
                static_object_type_is_read_only(parser->program, object_type),
                &object_id)) {
            minic_parser_error(parser, "cannot create static tentative definition");
            return false;
        }
    } else {
        existing = minic_c0_program_global_object(parser->program, object_id);
        if (existing == NULL || !existing->is_internal ||
            !minic_type_equal(existing->type, object_type) ||
            !minic_c0_global_object_merge_tentative(parser->program, object_id)) {
            minic_parser_error(parser, "conflicting static tentative definition");
            return false;
        }
    }
    return minic_parser_advance(parser);
}

static bool parse_static_inferred_integer_array(MinicParser *parser,
                                                MinicType element_type,
                                                MinicSourceSpan name_span,
                                                char *section_name,
                                                size_t section_capacity,
                                                size_t *section_name_length,
                                                bool *has_section,
                                                size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_integer(element_type) || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred static integer array") ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment) ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !apply_static_object_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array") ||
        !parse_static_scalar_array_transaction(parser, object_id, element_type, 0U, true)) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse inferred static integer array");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after inferred static integer array");
}

static bool parse_static_inferred_char_array(MinicParser *parser,
                                             MinicType element_type,
                                             MinicSourceSpan name_span,
                                             char *section_name,
                                             size_t section_capacity,
                                             size_t *section_name_length,
                                             bool *has_section,
                                             size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t element_count;

    if (parser == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL ||
        !minic_type_is_char_integer(element_type) || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred static character array") ||
        !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment) ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array")) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin inferred static character array");
        }
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_parser_add_string_literal_initializer(parser, object_id, &element_count) ||
            !minic_c0_program_complete_array_type(parser->program, object_type, element_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot initialize inferred static character array");
            }
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        if (!parse_static_scalar_array_transaction(parser, object_id, element_type, 0U, true)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(
                    parser, "cannot initialize inferred static character array from scalar list");
            }
            return false;
        }
    } else {
        minic_parser_error(
            parser,
            "inferred static character array requires a string or braced scalar initializer");
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static character array");
}

bool minic_parser_parse_static_zero_declaration_list_after_head(MinicParser *parser,
                                                                MinicType base_type,
                                                                MinicType first_object_type,
                                                                MinicSourceSpan first_name_span,
                                                                const char *shared_section_name,
                                                                size_t shared_section_name_length,
                                                                bool shared_has_section,
                                                                size_t shared_explicit_alignment) {
    bool first_declarator;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_COMMA ||
        (shared_has_section && (shared_section_name == NULL || shared_section_name_length == 0U ||
                                shared_section_name_length >= 256U))) {
        return false;
    }
    first_declarator = true;
    for (;;) {
        MinicGlobalObjectId object_id;
        MinicSourceSpan name_span;
        MinicType object_type;
        char section_name[256];
        size_t section_name_length;
        size_t explicit_alignment;
        bool has_section;

        section_name_length = shared_section_name_length;
        explicit_alignment = shared_explicit_alignment;
        has_section = shared_has_section;
        (void)memset(section_name, 0, sizeof(section_name));
        if (shared_has_section) {
            (void)memcpy(section_name, shared_section_name, shared_section_name_length);
            section_name[shared_section_name_length] = '\0';
        }

        if (first_declarator) {
            object_type = first_object_type;
            name_span = first_name_span;
            first_declarator = false;
        } else {
            if (!minic_parser_parse_pointer_declarator(parser, base_type, &object_type) ||
                parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "expected static object declarator after ','");
                }
                return false;
            }
            name_span = parser->current.span;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }

        if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           section_name,
                                                           sizeof(section_name),
                                                           &section_name_length,
                                                           &has_section,
                                                           &explicit_alignment)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA &&
            parser->current.kind != MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(
                parser,
                "static zero-definition declaration list currently supports declarations only");
            return false;
        }
        if ((!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
             !minic_type_is_record(object_type)) ||
            (minic_type_is_record(object_type) &&
             !minic_parser_require_complete_object_type(
                 parser, object_type, "static object requires a complete record type"))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "unsupported static zero-definition declarator type");
            }
            return false;
        }
        object_id = minic_parser_find_global_object_entity(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
            if (!minic_c0_program_add_tentative_global_object(
                    parser->program,
                    parser->source + name_span.begin.offset,
                    minic_parser_span_length(name_span),
                    object_type,
                    true,
                    static_object_type_is_read_only(parser->program, object_type),
                    &object_id)) {
                minic_parser_error(parser, "cannot create static tentative declarator");
                return false;
            }
        } else {
            const MinicGlobalObject *existing;

            existing = minic_c0_program_global_object(parser->program, object_id);
            if (existing == NULL || !existing->is_internal ||
                !minic_type_equal(existing->type, object_type) ||
                !minic_c0_global_object_merge_tentative(parser->program, object_id)) {
                minic_parser_error(parser, "conflicting static tentative declarator");
                return false;
            }
        }
        if (!apply_static_object_metadata(parser,
                                      object_id,
                                      section_name,
                                      section_name_length,
                                      has_section,
                                      explicit_alignment)) {
            minic_parser_error(parser, "cannot persist static tentative declarator metadata");
            return false;
        }

        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            return minic_parser_advance(parser);
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
}

static bool parse_preformed_static_array_definition(MinicParser *parser,
                                                    MinicType object_type,
                                                    MinicSourceSpan name_span,
                                                    const char *section_name,
                                                    size_t section_name_length,
                                                    bool has_section,
                                                    size_t explicit_alignment) {
    const MinicArrayType *array_type;
    MinicGlobalObjectId object_id;

    if (parser == NULL || !minic_type_is_array(object_type) ||
        parser->current.kind != MINIC_TOKEN_EQUAL) {
        if (parser != NULL) {
            minic_parser_error(parser, "pre-formed static array definition requires '='");
        }
        return false;
    }
    array_type = minic_c0_program_array_type(parser->program, object_type.array_type_id);
    if (array_type == NULL || (array_type->element_count == 0U && array_type->is_zero_length)) {
        minic_parser_error(parser, "invalid pre-formed static array definition type");
        return false;
    }
    if (!minic_c0_program_add_global_object(
            parser->program,
            parser->source + name_span.begin.offset,
            minic_parser_span_length(name_span),
            object_type,
            true,
            static_object_type_is_read_only(parser->program, object_type),
            &object_id) ||
        !apply_static_object_metadata(parser,
                                      object_id,
                                      section_name,
                                      section_name_length,
                                      has_section,
                                      explicit_alignment) ||
        !minic_parser_advance(parser)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin pre-formed static array definition");
        }
        return false;
    }

    array_type = minic_c0_program_array_type(parser->program, object_type.array_type_id);
    if (array_type == NULL) {
        minic_parser_error(parser, "invalid pre-formed static array descriptor");
        return false;
    }
    if (minic_type_is_char_integer(array_type->element_type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t string_count;
        bool infer_bound;

        infer_bound = array_type->element_count == 0U && !array_type->is_zero_length;
        if (infer_bound) {
            if (!minic_parser_add_string_literal_initializer(parser, object_id, &string_count) ||
                !minic_c0_program_complete_array_type(parser->program, object_type, string_count)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot infer pre-formed character array extent");
                }
                return false;
            }
        } else if (!minic_parser_add_bounded_string_literal_initializer(
                       parser, object_id, array_type->element_count)) {
            return false;
        }
    } else if (!minic_parser_parse_static_storage_initializer_value(
                   parser, object_id, object_type)) {
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after pre-formed static array definition");
}

bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType element_type,
                                                 MinicSourceSpan name_span,
                                                 char *section_name,
                                                 size_t section_capacity,
                                                 size_t *section_name_length,
                                                 bool *has_section,
                                                 size_t *explicit_alignment) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId existing_object_id;
    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
         !minic_type_is_record(element_type) && !minic_type_is_array(element_type))) {
        if (parser != NULL) {
            minic_parser_error(parser, "unsupported static global object type");
        }
        return false;
    }
    existing_object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return parse_static_zero_definition(parser, element_type, name_span);
    }
    if (existing_object_id != MINIC_GLOBAL_OBJECT_INVALID &&
        (minic_type_is_array(element_type) || parser->current.kind == MINIC_TOKEN_LBRACKET)) {
        const MinicGlobalObject *existing;

        existing = minic_c0_program_global_object(parser->program, existing_object_id);
        if (existing == NULL || !existing->is_internal || !existing->is_tentative ||
            !minic_type_is_array(existing->type)) {
            minic_parser_error(parser, "duplicate global object");
            return false;
        }
    }
    if (minic_type_is_array(element_type)) {
        return parse_preformed_static_array_definition(parser,
                                                       element_type,
                                                       name_span,
                                                       section_name,
                                                       *section_name_length,
                                                       *has_section,
                                                       *explicit_alignment);
    }
    if (minic_type_is_record(element_type)) {
        return parse_static_record(parser,
                                   element_type,
                                   name_span,
                                   section_name,
                                   section_capacity,
                                   section_name_length,
                                   has_section,
                                   explicit_alignment);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parse_static_scalar(parser, element_type, name_span);
    }
    if (minic_type_is_pointer(element_type)) {
        return parse_static_pointer_array(parser,
                                          element_type,
                                          name_span,
                                          section_name,
                                          section_capacity,
                                          section_name_length,
                                          has_section,
                                          explicit_alignment);
    }
    if (!minic_type_is_integer(element_type) && !minic_type_is_record(element_type)) {
        minic_parser_error(parser,
                           "static array requires an integer, pointer, or record element type");
        return false;
    }
    {
        MinicParser probe;
        bool incomplete_multidimensional;

        incomplete_multidimensional = false;
        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            incomplete_multidimensional = probe.current.kind == MINIC_TOKEN_LBRACKET;
            if (!incomplete_multidimensional) {
                if (minic_type_is_char_integer(element_type)) {
                    return parse_static_inferred_char_array(parser,
                                                            element_type,
                                                            name_span,
                                                            section_name,
                                                            section_capacity,
                                                            section_name_length,
                                                            has_section,
                                                            explicit_alignment);
                }
                return parse_static_inferred_integer_array(parser,
                                                           element_type,
                                                           name_span,
                                                           section_name,
                                                           section_capacity,
                                                           section_name_length,
                                                           has_section,
                                                           explicit_alignment);
            }
        }
        {
            bool is_array;

            if (!minic_parser_parse_array_declarator_suffix(
                    parser, element_type, incomplete_multidimensional, &object_type, &is_array) ||
                !is_array ||
                !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                               section_name,
                                                               section_capacity,
                                                               section_name_length,
                                                               has_section,
                                                               explicit_alignment)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot build fixed static array type");
                }
                return false;
            }
        }
    }
    if (existing_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
        const MinicGlobalObject *existing;
        const MinicArrayType *existing_array;
        const MinicArrayType *declared_array;
        MinicType transient_type;
        bool discard_transient;

        existing = minic_c0_program_global_object(parser->program, existing_object_id);
        existing_array =
            existing != NULL && minic_type_is_array(existing->type)
                ? minic_c0_program_array_type(parser->program, existing->type.array_type_id)
                : NULL;
        declared_array = minic_c0_program_array_type(parser->program, object_type.array_type_id);
        transient_type = object_type;
        discard_transient = existing != NULL &&
                            existing->type.array_type_id != object_type.array_type_id &&
                            object_type.array_type_id + 1U == parser->program->array_type_count;
        if (existing == NULL || existing_array == NULL || declared_array == NULL ||
            !existing->is_internal || !existing->is_tentative ||
            !minic_type_equal(existing_array->element_type, declared_array->element_type) ||
            (existing_array->element_count != 0U && declared_array->element_count != 0U &&
             existing_array->element_count != declared_array->element_count) ||
            (existing_array->element_count == 0U && declared_array->element_count != 0U &&
             !minic_c0_program_complete_array_type(
                 parser->program, existing->type, declared_array->element_count)) ||
            !minic_c0_global_object_begin_definition(parser->program, existing_object_id)) {
            minic_parser_error(parser, "conflicting static array definition");
            return false;
        }
        object_id = existing_object_id;
        object_type = parser->program->global_objects[object_id].type;
        if (discard_transient &&
            !minic_c0_program_discard_last_array_type(parser->program, transient_type)) {
            minic_parser_error(parser, "cannot retire transient static array definition type");
            return false;
        }
    } else if (!minic_c0_program_add_global_object(parser->program,
                                                   parser->source + name_span.begin.offset,
                                                   minic_parser_span_length(name_span),
                                                   object_type,
                                                   true,
                                                   minic_type_is_const(element_type),
                                                   &object_id)) {
        minic_parser_error(parser, "cannot add fixed static array object");
        return false;
    }
    if (!apply_static_object_metadata(parser,
                                      object_id,
                                      section_name,
                                      *section_name_length,
                                      *has_section,
                                      *explicit_alignment)) {
        minic_parser_error(parser, "cannot persist fixed static array metadata");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
            minic_parser_error(parser, "cannot zero-initialize fixed static array object");
            return false;
        }
        return minic_parser_advance(parser);
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array") ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse fixed static array initializer");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static array initializer");
}

bool minic_parser_parse_static_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    char section_name[256];
    size_t section_name_length;
    size_t explicit_alignment;
    bool has_section;

    section_name_length = 0U;
    explicit_alignment = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    if (parser == NULL ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_name(parser, &object_type)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected global object name");
        return false;
    }
    name_span = parser->current.span;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (!minic_parser_parse_static_global_after_head(parser,
                                                     object_type,
                                                     name_span,
                                                     section_name,
                                                     sizeof(section_name),
                                                     &section_name_length,
                                                     &has_section,
                                                     &explicit_alignment)) {
        return false;
    }
    object_id = minic_parser_find_global_object_entity(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        !apply_static_object_metadata(parser,
                                      object_id,
                                      section_name,
                                      section_name_length,
                                      has_section,
                                      explicit_alignment)) {
        minic_parser_error(parser, "cannot persist static object metadata");
        return false;
    }
    return true;
}
