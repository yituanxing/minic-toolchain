#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    bool has_explicit_pointer_cast;
    uint64_t bits;
    MinicFunctionId function_id;
    MinicStaticObjectRelocationTarget relocation_target;
} MinicStaticPointerInitializer;

typedef struct MinicStaticArraySlot {
    uint64_t integer_bits;
    MinicStaticPointerInitializer pointer_initializer;
} MinicStaticArraySlot;

static bool static_object_address_relocation_path(const MinicParser *parser,
                                                  MinicExpressionId expression_id,
                                                  MinicStaticObjectRelocationTarget *target) {
    const MinicC0Program *program;
    const MinicExpression *expression;
    const MinicExpression *addressed;
    size_t reverse_path[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t depth;
    size_t index;

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
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    addressed = minic_c0_program_expression(program, expression->value.unary.operand);
    depth = 0U;
    while (addressed != NULL && addressed->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;

        if (depth == MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH) {
            return false;
        }
        reverse_path[depth] = addressed->value.member.field_index;
        depth += 1U;
        base = minic_c0_program_expression(program, addressed->value.member.base);
        if (base != NULL && base->kind == MINIC_EXPRESSION_ADDRESS_OF) {
            addressed = minic_c0_program_expression(program, base->value.unary.operand);
        } else {
            addressed = base;
        }
    }
    if (addressed == NULL || addressed->kind != MINIC_EXPRESSION_GLOBAL_OBJECT) {
        return false;
    }
    target->object_id = addressed->value.global_object_id;
    if (target->object_id >= program->global_object_count) {
        return false;
    }
    target->member_depth = depth;
    target->byte_addend = 0;
    for (index = 0U; index < depth; ++index) {
        target->member_indices[index] = reverse_path[depth - index - 1U];
    }
    return true;
}

static bool static_pointer_integer_constant_bits(const MinicParser *parser,
                                                 MinicExpressionId expression_id,
                                                 uint64_t *bits) {
    const MinicExpression *expression;
    const MinicExpression *operand;
    MinicConstValue constant;
    int64_t signed_value;
    const MinicDataLayout *layout;
    unsigned int pointer_bits;

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
            parser->program, parser->target_info, expression->value.unary.operand, &constant) ||
        !minic_const_value_as_int64(
            parser->program, parser->target_info, &constant, &signed_value)) {
        return false;
    }
    layout = minic_target_info_data_layout(parser->target_info);
    if (layout == NULL || layout->pointer_size == 0U || layout->pointer_size > 8U) {
        return false;
    }
    pointer_bits = (unsigned int)(layout->pointer_size * 8U);
    *bits = (uint64_t)signed_value;
    if (pointer_bits < 64U) {
        *bits &= (UINT64_C(1) << pointer_bits) - UINT64_C(1);
    }
    return true;
}

static bool parse_static_pointer_initializer(MinicParser *parser,
                                             MinicType target_type,
                                             MinicStaticPointerInitializer *initializer) {
    MinicExpressionId expression_id;

    if (parser == NULL || initializer == NULL || !minic_type_is_pointer(target_type) ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    (void)memset(initializer, 0, sizeof(*initializer));
    initializer->function_id = MINIC_FUNCTION_INVALID;
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
    initializer->has_explicit_pointer_cast =
        static_pointer_expression_has_explicit_cast(parser->program, expression_id);
    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        minic_parser_error(parser, "static pointer initializer type mismatch");
        return false;
    }
    if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, expression_id)) {
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

static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;

    if (!begin_static_object_definition(parser, type, name_span, &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin static scalar initializer");
        }
        return false;
    }

    if (minic_type_is_integer(type)) {
        uint64_t bits;

        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            minic_parser_error(parser, "expected integer constant expression");
            return false;
        }
        if (!minic_parser_parse_integer_initializer_bits(parser, type, &bits) ||
            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, bits)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static integer initializer");
            }
            return false;
        }
    } else if (minic_type_is_pointer(type)) {
        MinicStaticPointerInitializer initializer;

        if (!parse_static_pointer_initializer(parser, type, &initializer)) {
            return false;
        }
        if (initializer.has_relocation) {
            bool recorded;

            if (initializer.relocation_is_function) {
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
            if (!recorded ||
                !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                minic_parser_error(parser, "cannot record static symbolic pointer relocation");
                return false;
            }
        } else if (!minic_c0_global_object_add_initializer_bits(
                       parser->program, object_id, initializer.bits)) {
            minic_parser_error(parser, "cannot record static pointer constant bits");
            return false;
        }
    } else {
        minic_parser_error(parser, "unsupported static scalar type");
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
    /* A flexible array member participates in the record type and alignment,
     * but contributes no scalar initializer slot to the fixed object extent. */
    if (field->is_flexible_array) {
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
        uint64_t parsed_bits;

        if (!minic_parser_parse_integer_initializer_bits(parser, type, &parsed_bits)) {
            return false;
        }
        if (overwrite) {
            if (!minic_c0_global_object_replace_zero_initializer_bits(
                    parser->program, object_id, overwrite_slot, parsed_bits)) {
                minic_parser_error(
                    parser,
                    "backward static record designator can only replace an implicit scalar zero");
                return false;
            }
        } else if (!minic_c0_global_object_add_initializer_bits(
                       parser->program, object_id, parsed_bits)) {
            minic_parser_error(parser, "cannot record static aggregate integer initializer");
            return false;
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

            if (!overwrite &&
                !minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U)) {
                minic_parser_error(parser, "cannot reserve nested static relocation slot");
                return false;
            }
            if (initializer.relocation_is_function) {
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
            if (!minic_c0_global_object_replace_zero_initializer_bits(
                    parser->program, object_id, overwrite_slot, initializer.bits)) {
                minic_parser_error(
                    parser,
                    "backward static record designator can only replace an implicit scalar zero");
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
    if (initializer->relocation_is_function) {
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
    return recorded;
}

static bool materialize_static_array_slots(MinicParser *parser,
                                           MinicGlobalObjectId object_id,
                                           MinicType element_type,
                                           const MinicStaticArraySlot *slots,
                                           size_t slot_count) {
    size_t index;

    if (parser == NULL || slots == NULL ||
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
        } else if (!materialize_static_pointer_array_slot(
                       parser, object_id, index, &slots[index].pointer_initializer)) {
            minic_parser_error(parser, "cannot materialize static pointer array slot");
            return false;
        }
    }
    return true;
}

static bool parse_static_scalar_array_transaction(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  MinicType element_type,
                                                  size_t element_count,
                                                  bool infer_bound) {
    MinicStaticArraySlot *slots;
    const MinicGlobalObject *object;
    size_t capacity;
    size_t extent;
    size_t next_index;
    bool success;

    slots = NULL;
    capacity = 0U;
    extent = infer_bound ? 0U : element_count;
    next_index = 0U;
    success = false;
    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) ||
        (!infer_bound && element_count == 0U) || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid static scalar array initializer");
        }
        goto done;
    }
    if (!infer_bound && !grow_static_array_slots(parser, &slots, &capacity, element_count)) {
        goto done;
    }
    if (!minic_parser_advance(parser)) {
        goto done;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicStaticArraySlot value;
        size_t first;
        size_t last;
        size_t required;
        size_t index;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!minic_parser_parse_array_designator(
                    parser, element_count, infer_bound, &first, &last)) {
                goto done;
            }
        } else {
            first = next_index;
            last = first;
            if (!infer_bound && first >= element_count) {
                minic_parser_error(parser, "too many nested static array initializers");
                goto done;
            }
        }
        if (last == SIZE_MAX) {
            minic_parser_error(parser, "static array designator extent overflows");
            goto done;
        }
        required = last + 1U;
        if (infer_bound) {
            if (!grow_static_array_slots(parser, &slots, &capacity, required)) {
                goto done;
            }
            if (required > extent) {
                extent = required;
            }
        }
        if (!parse_static_array_scalar_slot(parser, element_type, &value)) {
            goto done;
        }
        for (index = first;; ++index) {
            slots[index] = value;
            if (index == last) {
                break;
            }
        }
        next_index = required;
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
    if (infer_bound) {
        if (extent == 0U) {
            minic_parser_error(parser, "cannot infer static array bound from an empty initializer");
            goto done;
        }
        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL || !minic_type_is_array(object->type) ||
            !minic_c0_program_complete_array_type(parser->program, object->type, extent)) {
            minic_parser_error(parser, "cannot complete inferred static array type");
            goto done;
        }
    }
    if (!materialize_static_array_slots(parser, object_id, element_type, slots, extent)) {
        goto done;
    }
    success = true;

done:
    free(slots);
    return success;
}

static bool parse_static_forward_array_initializer(MinicParser *parser,
                                                   MinicGlobalObjectId object_id,
                                                   MinicType element_type,
                                                   size_t element_count,
                                                   bool infer_bound,
                                                   size_t *parsed_extent) {
    size_t next_index;
    size_t extent;

    if (parser == NULL || object_id >= parser->program->global_object_count ||
        (!infer_bound && element_count == 0U) || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid static aggregate array initializer");
        }
        return false;
    }
    next_index = 0U;
    extent = infer_bound ? 0U : element_count;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        size_t first;
        size_t last;

        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!minic_parser_parse_array_designator(
                    parser, element_count, infer_bound, &first, &last)) {
                return false;
            }
            if (last != first) {
                minic_parser_error(
                    parser,
                    "GNU range designators for aggregate static arrays are not supported yet");
                return false;
            }
            if (first < next_index) {
                minic_parser_error(
                    parser, "backward static aggregate array designator is not supported yet");
                return false;
            }
        } else {
            first = next_index;
            if (!infer_bound && first >= element_count) {
                minic_parser_error(parser, "too many nested static array initializers");
                return false;
            }
        }
        while (next_index < first) {
            if (!append_static_constant_zero(parser, object_id, element_type)) {
                minic_parser_error(parser, "cannot zero-fill skipped static array element");
                return false;
            }
            next_index += 1U;
        }
        if (!minic_parser_parse_static_storage_initializer_value(parser, object_id, element_type)) {
            return false;
        }
        if (first == SIZE_MAX) {
            minic_parser_error(parser, "static array initializer extent overflows");
            return false;
        }
        next_index = first + 1U;
        if (next_index > extent) {
            extent = next_index;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in static array initializer");
            return false;
        }
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after static array initializer")) {
        return false;
    }
    if (infer_bound) {
        if (extent == 0U) {
            minic_parser_error(parser, "cannot infer static array bound from an empty initializer");
            return false;
        }
    } else {
        while (next_index < element_count) {
            if (!append_static_constant_zero(parser, object_id, element_type)) {
                minic_parser_error(parser, "cannot zero-fill static array initializer tail");
                return false;
            }
            next_index += 1U;
        }
    }
    if (parsed_extent != NULL) {
        *parsed_extent = extent;
    }
    return true;
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
        minic_parser_error(parser, "invalid zero-length static array initializer type");
        return false;
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
            !minic_c0_program_complete_array_type(parser->program, object->type, parsed_extent)) {
            minic_parser_error(parser, "cannot complete inferred static aggregate array type");
            return false;
        }
    }
    return true;
}

static bool parse_static_record_constant(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         const MinicRecord *record) {
    size_t field_index;
    size_t field_limit;
    size_t materialized_field_limit;
    size_t record_base_slot;

    if (record == NULL || !record->is_complete ||
        object_id >= parser->program->global_object_count ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    field_index = 0U;
    materialized_field_limit = 0U;
    record_base_slot = parser->program->global_objects[object_id].initializer_count;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        bool overwrite_materialized_field;

        if (parser->current.kind == MINIC_TOKEN_DOT) {
            MinicRecordFieldPath field_path;
            MinicSourceSpan designator_span;
            size_t designator_index;

            if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "expected member name after '.' in initializer");
                }
                return false;
            }
            designator_span = parser->current.span;
            if (!minic_parser_find_record_field_path(
                    parser, record, designator_span, &field_path) ||
                !field_path.found || field_path.ambiguous || field_path.depth != 1U) {
                minic_parser_error(parser,
                                   "static record designator requires a direct unambiguous member");
                return false;
            }
            designator_index = field_path.field_indices[0];
            if (record->is_union && designator_index != 0U) {
                minic_parser_error(
                    parser,
                    "nested static union designator requires the representable first member");
                return false;
            }
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
            if (!minic_parser_advance(parser) ||
                !minic_parser_expect(
                    parser, MINIC_TOKEN_EQUAL, "expected '=' after static record designator")) {
                return false;
            }
        }
        if (field_index >= field_limit) {
            minic_parser_error(parser, "too many nested static record initializers");
            return false;
        }
        field = &record->fields[field_index];
        if (field->element_count == 0U || field->is_flexible_array) {
            minic_parser_error(parser, "unsupported nested static record field");
            return false;
        }
        overwrite_materialized_field = field_index < materialized_field_limit;
        if (overwrite_materialized_field) {
            size_t relative_slot;
            size_t slot_index;

            if (field->element_count != 1U ||
                (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type)) ||
                !minic_c0_global_record_field_initializer_slot(
                    parser->program, record, field_index, &relative_slot) ||
                record_base_slot > SIZE_MAX - relative_slot) {
                minic_parser_error(
                    parser,
                    "backward static record designator currently requires a direct scalar field");
                return false;
            }
            slot_index = record_base_slot + relative_slot;
            if (!parse_static_scalar_constant_at(
                    parser, object_id, field->type, true, slot_index)) {
                return false;
            }
        } else {
            if (field_index != materialized_field_limit) {
                minic_parser_error(parser, "internal error: invalid static record materialization");
                return false;
            }
            if (field->element_count == 1U) {
                if (!minic_parser_parse_static_storage_initializer_value(
                        parser, object_id, field->type)) {
                    return false;
                }
            } else if (minic_type_is_char_integer(field->type) &&
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
        return parse_static_record_constant(
            parser, object_id, minic_c0_program_record(parser->program, type.record_id));
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
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}

static bool probe_static_array_designator_extent(MinicParser *probe, size_t *first, size_t *last) {
    MinicC0Program before;
    MinicC0Program *program;

    if (probe == NULL || first == NULL || last == NULL || probe->program == NULL) {
        return false;
    }
    program = probe->program;
    before = *program;
    if (!minic_parser_parse_array_designator(probe, 0U, true, first, last)) {
        return false;
    }
    if (program->local_count != before.local_count ||
        program->cleanup_context_count != before.cleanup_context_count ||
        program->statement_count != before.statement_count ||
        program->inline_asm_count != before.inline_asm_count ||
        program->file_asm_count != before.file_asm_count ||
        program->block_count != before.block_count ||
        program->function_count != before.function_count ||
        program->record_count != before.record_count ||
        program->array_type_count != before.array_type_count ||
        program->function_type_count != before.function_type_count ||
        program->type_alias_count != before.type_alias_count ||
        program->enum_count != before.enum_count ||
        program->enumerator_count != before.enumerator_count ||
        program->global_object_count != before.global_object_count ||
        program->fixed_register_binding_count != before.fixed_register_binding_count) {
        minic_parser_error(probe,
                           "inferred aggregate array designator probe requires a side-effect-free "
                           "integer constant expression");
        return false;
    }
    program->expression_count = before.expression_count;
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

static bool
parse_static_record_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span) {
    const MinicRecord *record;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t declared_count;
    bool inferred_bound;

    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        minic_parser_error(parser, "static record array requires a complete non-empty struct type");
        return false;
    }

    declared_count = 0U;
    inferred_bound = false;
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
        minic_parser_error(parser, "multi-dimensional static record arrays are not supported yet");
        return false;
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
        if (declared_count == 0U) {
            minic_parser_error(parser,
                               "cannot infer static record array bound from an empty initializer");
            return false;
        }
    }

    if (!minic_c0_program_add_array_type(
            parser->program, element_type, declared_count, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_parser_parse_static_storage_initializer_value(parser, object_id, object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse static record array initializer");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static record array");
}

static bool parse_static_record(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    const MinicRecord *record;

    record = minic_c0_program_record(parser->program, type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "static record global requires a complete record type");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        return parse_static_record_array(parser, type, name_span);
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

bool minic_parser_external_object_types_compatible(const MinicC0Program *program,
                                                   MinicType existing_type,
                                                   MinicType declared_type) {
    const MinicArrayType *existing_array;
    const MinicArrayType *declared_array;

    if (minic_type_equal(existing_type, declared_type)) {
        return true;
    }
    if (program == NULL || !minic_type_is_array(existing_type) ||
        !minic_type_is_array(declared_type)) {
        return false;
    }
    existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
    declared_array = minic_c0_program_array_type(program, declared_type.array_type_id);
    if (existing_array == NULL || declared_array == NULL ||
        !minic_parser_external_object_types_compatible(
            program, existing_array->element_type, declared_array->element_type)) {
        return false;
    }
    if ((existing_array->element_count == 0U && !existing_array->is_zero_length) ||
        (declared_array->element_count == 0U && !declared_array->is_zero_length)) {
        return true;
    }
    return existing_array->is_zero_length == declared_array->is_zero_length &&
           existing_array->element_count == declared_array->element_count;
}

bool minic_parser_merge_external_array_composite_type(MinicC0Program *program,
                                                      MinicType existing_type,
                                                      MinicType declared_type) {
    const MinicArrayType *existing_array;
    const MinicArrayType *declared_array;
    MinicType existing_element;
    MinicType declared_element;
    size_t declared_count;

    if (minic_type_equal(existing_type, declared_type)) {
        return true;
    }
    if (program == NULL || !minic_type_is_array(existing_type) ||
        !minic_type_is_array(declared_type)) {
        return minic_type_equal(existing_type, declared_type);
    }
    existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
    declared_array = minic_c0_program_array_type(program, declared_type.array_type_id);
    if (existing_array == NULL || declared_array == NULL) {
        return false;
    }
    existing_element = existing_array->element_type;
    declared_element = declared_array->element_type;
    declared_count = declared_array->element_count;
    if (!minic_parser_external_object_types_compatible(
            program, existing_element, declared_element) ||
        !minic_parser_merge_external_array_composite_type(
            program, existing_element, declared_element)) {
        return false;
    }
    existing_array = minic_c0_program_array_type(program, existing_type.array_type_id);
    if (existing_array == NULL) {
        return false;
    }
    if (existing_array->element_count == 0U && !existing_array->is_zero_length) {
        if (declared_array->is_zero_length) {
            return minic_c0_program_complete_zero_length_array_type(program, existing_type);
        }
        if (declared_count != 0U) {
            return minic_c0_program_complete_array_type(program, existing_type, declared_count);
        }
        return true;
    }
    if (declared_count == 0U && !declared_array->is_zero_length) {
        return true;
    }
    return existing_array->is_zero_length == declared_array->is_zero_length &&
           existing_array->element_count == declared_count;
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
    MinicGlobalObject *object;

    if (parser == NULL || parser->program == NULL ||
        object_id >= parser->program->global_object_count) {
        return false;
    }
    object = &parser->program->global_objects[object_id];
    if (!minic_parser_external_object_types_compatible(
            parser->program, object->type, declared_type)) {
        minic_parser_error(parser, "conflicting extern object redeclaration");
        return false;
    }
    if (minic_type_is_array(object->type) && !minic_parser_merge_external_array_composite_type(
                                                 parser->program, object->type, declared_type)) {
        minic_parser_error(parser, "conflicting extern object array redeclaration");
        return false;
    }
    if ((has_section && !minic_c0_global_object_set_section(
                            parser->program, object_id, section_name, section_name_length)) ||
        (explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                         parser->program, object_id, explicit_alignment)) ||
        (has_visibility &&
         !minic_c0_global_object_set_visibility(parser->program, object_id, visibility))) {
        minic_parser_error(parser, "conflicting extern object redeclaration attributes");
        return false;
    }
    return true;
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
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            false,
                                            minic_type_is_const(object_type),
                                            object_id) ||
        !minic_c0_global_object_set_extern(parser->program, *object_id)) {
        minic_parser_error(parser, "cannot declare block-scope extern object");
        return false;
    }
    parser->program->global_objects[*object_id].is_block_scope_extern_only = true;
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
        bool is_array;
        MinicType declarator_element_type;
        size_t array_type_begin;

        declarator_section_name_length = section_name_length;
        declarator_explicit_alignment = shared_explicit_alignment;
        declarator_has_section = has_section;
        declarator_visibility = visibility;
        declarator_has_visibility = has_visibility;
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
        if (!minic_parser_parse_gnu_object_attribute_lists_with_visibility(
                parser,
                declarator_section_name,
                sizeof(declarator_section_name),
                &declarator_section_name_length,
                &declarator_has_section,
                &declarator_explicit_alignment,
                &declarator_visibility,
                &declarator_has_visibility)) {
            return false;
        }
        if (minic_type_is_function(object_type)) {
            minic_parser_error(parser, "unsupported extern object type");
            return false;
        }
        array_type_begin = parser->program->array_type_count;
        if (!minic_parser_parse_array_declarator_suffix(
                parser, object_type, true, &object_type, &is_array) ||
            !minic_parser_parse_gnu_object_attribute_lists_with_visibility(
                parser,
                declarator_section_name,
                sizeof(declarator_section_name),
                &declarator_section_name_length,
                &declarator_has_section,
                &declarator_explicit_alignment,
                &declarator_visibility,
                &declarator_has_visibility)) {
            return false;
        }

        object_id = minic_parser_find_global_object_entity(parser, name_span);
        if (object_id != MINIC_GLOBAL_OBJECT_INVALID) {
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
        } else if (!minic_c0_program_add_extern_global_object(
                       parser->program,
                       parser->source + name_span.begin.offset,
                       minic_parser_span_length(name_span),
                       object_type,
                       minic_type_is_const(declarator_element_type),
                       &object_id) ||
                   (declarator_has_section &&
                    !minic_c0_global_object_set_section(parser->program,
                                                        object_id,
                                                        declarator_section_name,
                                                        declarator_section_name_length)) ||
                   (declarator_explicit_alignment != 0U &&
                    !minic_c0_global_object_set_explicit_alignment(
                        parser->program, object_id, declarator_explicit_alignment)) ||
                   (declarator_has_visibility &&
                    !minic_c0_global_object_set_visibility(
                        parser->program, object_id, declarator_visibility))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot declare extern object");
            }
            return false;
        }
        parser->program->global_objects[object_id].is_block_scope_extern_only = false;

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
    MinicGlobalObjectId object_id;
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
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
               !minic_c0_program_add_array_type(
                   parser->program, element_type, element_count, &object_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot build static pointer array type");
        }
        return false;
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
                                                       explicit_alignment) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        (*has_section && !minic_c0_global_object_set_section(
                             parser->program, object_id, section_name, *section_name_length)) ||
        (*explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                          parser->program, object_id, *explicit_alignment))) {
        minic_parser_error(parser, "cannot create static pointer array object");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (inferred_bound) {
            minic_parser_error(parser, "incomplete static pointer array requires an initializer");
            return false;
        }
        return minic_c0_global_object_set_zero_initialized(parser->program, object_id) &&
               minic_parser_advance(parser);
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_EQUAL, "expected '=' after static pointer array") ||
        !parse_static_scalar_array_transaction(
            parser, object_id, element_type, element_count, inferred_bound)) {
        return false;
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
        (*has_section && !minic_c0_global_object_set_section(
                             parser->program, object_id, section_name, *section_name_length)) ||
        (*explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                          parser->program, object_id, *explicit_alignment)) ||
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
        !minic_type_is_char_integer(element_type) || !minic_type_is_const(element_type) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET || !minic_parser_advance(parser) ||
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
                                            true,
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array")) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin inferred static character array");
        }
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
        !minic_parser_add_string_literal_initializer(parser, object_id, &element_count) ||
        !minic_c0_program_complete_array_type(parser->program, object_type, element_count)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(
                parser, "inferred static character array requires a string literal initializer");
        }
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
        if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                true,
                                                minic_type_is_const(object_type),
                                                &object_id) ||
            !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
            (has_section && !minic_c0_global_object_set_section(
                                parser->program, object_id, section_name, section_name_length)) ||
            (explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                             parser->program, object_id, explicit_alignment))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot create static zero-definition declarator");
            }
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
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
    if (minic_type_is_array(element_type)) {
        minic_parser_error(parser, "pre-formed static array initializer is not supported yet");
        return false;
    }
    if (minic_type_is_record(element_type) && parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parse_static_record(parser, element_type, name_span);
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

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (minic_type_is_record(element_type)) {
                return parse_static_record(parser, element_type, name_span);
            }
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
                parser, element_type, false, &object_type, &is_array) ||
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
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id)) {
        minic_parser_error(parser, "cannot add fixed static array object");
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
        (has_section && !minic_c0_global_object_set_section(
                            parser->program, object_id, section_name, section_name_length)) ||
        (explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                         parser->program, object_id, explicit_alignment))) {
        minic_parser_error(parser, "cannot persist static object metadata");
        return false;
    }
    return true;
}
