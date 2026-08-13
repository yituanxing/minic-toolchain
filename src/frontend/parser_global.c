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
        if (binding != NULL && binding->name_length == name_length &&
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

static bool parse_zero_initializer(MinicParser *parser, MinicType type) {
    if (minic_type_is_integer(type)) {
        int value;

        if (!minic_parser_parse_integer_value(parser, &value)) {
            return false;
        }
        if (value != 0) {
            minic_parser_error(parser, "static zero initializer requires integer zero");
            return false;
        }
        return true;
    }
    if (minic_type_is_pointer(type)) {
        return minic_parser_parse_zero_pointer_constant(parser);
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;

        record = minic_c0_program_record(parser->program, type.record_id);
        if (record == NULL || !record->is_complete) {
            minic_parser_error(parser, "static record initializer requires a complete record type");
            return false;
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
            return false;
        }

        field_index = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            const MinicRecordField *field;

            if (field_index >= record->field_count) {
                minic_parser_error(parser, "too many static record initializers");
                return false;
            }
            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count != 1U ||
                (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type) &&
                 !minic_type_is_record(field->type))) {
                minic_parser_error(parser, "static zero record initializer requires scalar fields");
                return false;
            }
            if (!parse_zero_initializer(parser, field->type)) {
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
                minic_parser_error(parser, "expected ',' or '}' in record initializer");
                return false;
            }
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer");
    }

    minic_parser_error(parser, "unsupported static zero initializer type");
    return false;
}

static bool function_designator_type(MinicParser *parser,
                                     MinicFunctionId function_id,
                                     MinicType *pointer_type) {
    const MinicFunction *function;
    MinicType function_type;

    function = minic_c0_program_function(parser->program, function_id);
    if (function == NULL || function->is_variadic ||
        !minic_c0_program_add_function_type(parser->program,
                                            function->return_type,
                                            function->parameter_types,
                                            function->parameter_count,
                                            &function_type) ||
        !minic_type_pointer_to(function_type, pointer_type)) {
        return false;
    }
    return true;
}

static bool type_is_function_pointer(MinicType type) {
    MinicType pointee;

    return minic_type_pointee(type, &pointee) && minic_type_is_function(pointee);
}

static bool static_object_address_relocation_target(const MinicC0Program *program,
                                                    MinicExpressionId expression_id,
                                                    MinicGlobalObjectId *target_object_id) {
    const MinicExpression *expression;
    const MinicExpression *addressed;
    MinicGlobalObjectId object_id;

    if (program == NULL || target_object_id == NULL) {
        return false;
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
    if (addressed == NULL) {
        return false;
    }
    if (addressed->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        object_id = addressed->value.global_object_id;
    } else if (addressed->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        const MinicExpression *index;
        const MinicGlobalObject *object;

        base = minic_c0_program_expression(program, addressed->value.subscript.base);
        index = minic_c0_program_expression(program, addressed->value.subscript.index);
        if (base == NULL || base->kind != MINIC_EXPRESSION_GLOBAL_OBJECT || index == NULL ||
            index->kind != MINIC_EXPRESSION_INTEGER || !minic_type_is_integer(index->type) ||
            index->value.integer_value != 0) {
            return false;
        }
        object_id = base->value.global_object_id;
        object = minic_c0_program_global_object(program, object_id);
        if (object == NULL || !minic_type_is_array(object->type)) {
            return false;
        }
    } else {
        return false;
    }
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
        minic_c0_program_global_object(program, object_id) == NULL) {
        return false;
    }
    *target_object_id = object_id;
    return true;
}

typedef struct MinicStaticObjectRelocationTarget {
    MinicGlobalObjectId object_id;
    size_t member_indices[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t member_depth;
} MinicStaticObjectRelocationTarget;

typedef struct MinicStaticPointerInitializer {
    bool has_relocation;
    uint64_t bits;
    MinicStaticObjectRelocationTarget relocation_target;
} MinicStaticPointerInitializer;

static bool static_object_address_relocation_path(const MinicC0Program *program,
                                                  MinicExpressionId expression_id,
                                                  MinicStaticObjectRelocationTarget *target) {
    const MinicExpression *expression;
    const MinicExpression *addressed;
    size_t reverse_path[MINIC_GLOBAL_RELOCATION_MAX_MEMBER_DEPTH];
    size_t depth;
    size_t index;

    if (program == NULL || target == NULL) {
        return false;
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
    initializer->relocation_target.object_id = MINIC_GLOBAL_OBJECT_INVALID;
    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        minic_parser_error(parser, "static pointer initializer type mismatch");
        return false;
    }
    if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, expression_id)) {
        return true;
    }
    if (static_object_address_relocation_path(
            parser->program, expression_id, &initializer->relocation_target)) {
        initializer->has_relocation = true;
        return true;
    }
    if (static_pointer_integer_constant_bits(parser, expression_id, &initializer->bits)) {
        return true;
    }
    minic_parser_error(parser,
                       "static pointer initializer requires null, symbolic address, or explicit "
                       "integer-to-pointer constant cast");
    return false;
}

static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;

    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            type,
                                            true,
                                            minic_type_is_const(type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin static scalar initializer");
        }
        return false;
    }

    if (minic_type_is_integer(type)) {
        int value;

        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            minic_parser_error(parser, "expected integer constant expression");
            return false;
        }
        if (!minic_parser_parse_integer_initializer_value(parser, type, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static integer initializer");
            }
            return false;
        }
    } else if (minic_type_is_pointer(type)) {
        if (type_is_function_pointer(type) && parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
            MinicFunctionId function_id;
            MinicType designator_type;

            function_id = minic_parser_find_function(parser, parser->current.span);
            if (function_id == MINIC_FUNCTION_INVALID ||
                !function_designator_type(parser, function_id, &designator_type) ||
                !minic_type_assignment_compatible(type, designator_type)) {
                minic_parser_error(parser, "static function pointer initializer type mismatch");
                return false;
            }
            if (!minic_parser_advance(parser) ||
                !minic_c0_global_object_add_function_relocation(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                    0U,
                    function_id) ||
                !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                minic_parser_error(parser, "cannot record static function pointer initializer");
                return false;
            }
        } else {
            MinicExpressionId initializer_id;
            MinicGlobalObjectId target_object_id;

            if (!minic_parser_parse_expression(parser, &initializer_id, 0U)) {
                return false;
            }
            if (!minic_c0_assignment_compatible(parser->program, type, initializer_id)) {
                minic_parser_error(parser, "static pointer initializer type mismatch");
                return false;
            }
            if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                    minic_parser_error(parser, "cannot record static null-pointer initializer");
                    return false;
                }
            } else if (static_object_address_relocation_target(
                           parser->program, initializer_id, &target_object_id)) {
                if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
                    !minic_c0_global_object_add_object_relocation(
                        parser->program,
                        object_id,
                        MINIC_GLOBAL_RELOCATION_LOCATION_SCALAR,
                        0U,
                        target_object_id)) {
                    minic_parser_error(parser, "cannot record static object-address relocation");
                    return false;
                }
            } else {
                uint64_t pointer_bits;

                if (!static_pointer_integer_constant_bits(parser, initializer_id, &pointer_bits) ||
                    !minic_c0_global_object_add_initializer_bits(
                        parser->program, object_id, pointer_bits)) {
                    minic_parser_error(
                        parser,
                        "static pointer initializer requires null, symbolic address, "
                        "or explicit integer-to-pointer constant cast");
                    return false;
                }
            }
        }
    } else {
        minic_parser_error(parser, "unsupported static scalar type");
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}

static bool static_record_has_direct_function_pointer(const MinicRecord *record) {
    size_t field_index;

    if (record == NULL) {
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        MinicType pointee;

        field = &record->fields[field_index];
        if (field->element_count == 1U && minic_type_is_pointer(field->type) &&
            minic_type_pointee(field->type, &pointee) && minic_type_is_function(pointee)) {
            return true;
        }
    }
    return false;
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
        if (record == NULL || !record->is_complete || record->field_count == 0U) {
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

static bool
parse_static_constant_value(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type);

static bool
parse_static_scalar_constant(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type) {
    bool braced;

    braced = parser->current.kind == MINIC_TOKEN_LBRACE;
    if (braced && !minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(type)) {
        uint64_t parsed_bits;

        if (!minic_parser_parse_integer_initializer_bits(parser, type, &parsed_bits) ||
            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, parsed_bits)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static aggregate integer initializer");
            }
            return false;
        }
    } else if (minic_type_is_pointer(type)) {
        MinicStaticPointerInitializer initializer;
        size_t slot_index;

        slot_index = parser->program->global_objects[object_id].initializer_count;
        if (!parse_static_pointer_initializer(parser, type, &initializer)) {
            return false;
        }
        if (initializer.has_relocation) {
            if (!minic_c0_global_object_add_initializer_bits(parser->program, object_id, 0U) ||
                !minic_c0_global_object_add_object_relocation_path(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR,
                    slot_index,
                    initializer.relocation_target.object_id,
                    initializer.relocation_target.member_indices,
                    initializer.relocation_target.member_depth)) {
                minic_parser_error(parser, "cannot record nested static object relocation");
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

static bool parse_static_array_constant(MinicParser *parser,
                                        MinicGlobalObjectId object_id,
                                        const MinicArrayType *array_type) {
    size_t element_index;

    if (array_type == NULL || array_type->element_count == 0U ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in array initializer")) {
        return false;
    }
    element_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (element_index >= array_type->element_count) {
            minic_parser_error(parser, "too many nested static array initializers");
            return false;
        }
        if (!parse_static_constant_value(parser, object_id, array_type->element_type)) {
            return false;
        }
        element_index += 1U;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in nested static array initializer");
            return false;
        }
    }
    while (element_index < array_type->element_count) {
        if (!append_static_constant_zero(parser, object_id, array_type->element_type)) {
            minic_parser_error(parser, "cannot zero-fill nested static array initializer");
            return false;
        }
        element_index += 1U;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after array initializer");
}

static bool parse_static_record_constant(MinicParser *parser,
                                         MinicGlobalObjectId object_id,
                                         const MinicRecord *record) {
    size_t field_index;
    size_t field_limit;

    if (record == NULL || !record->is_complete || record->field_count == 0U ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        return false;
    }
    field_limit = record->is_union ? 1U : record->field_count;
    field_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;
        size_t element_index;

        if (parser->current.kind == MINIC_TOKEN_DOT) {
            MinicRecordFieldPath field_path;
            MinicSourceSpan designator_span;
            size_t designator_index;

            if (record->is_union) {
                minic_parser_error(parser, "nested static union designators are not supported yet");
                return false;
            }
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
            if (designator_index < field_index) {
                minic_parser_error(parser, "static record designator cannot move backward in v0");
                return false;
            }
            while (field_index < designator_index) {
                if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
                    minic_parser_error(parser,
                                       "cannot zero-fill skipped static record designator fields");
                    return false;
                }
                field_index += 1U;
            }
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
        if (field->element_count == 1U) {
            if (!parse_static_constant_value(parser, object_id, field->type)) {
                return false;
            }
        } else {
            if (!minic_parser_expect(
                    parser, MINIC_TOKEN_LBRACE, "expected '{' in record field array initializer")) {
                return false;
            }
            element_index = 0U;
            while (parser->current.kind != MINIC_TOKEN_RBRACE) {
                if (element_index >= field->element_count ||
                    !parse_static_constant_value(parser, object_id, field->type)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser, "too many record field array initializers");
                    }
                    return false;
                }
                element_index += 1U;
                if (parser->current.kind == MINIC_TOKEN_COMMA) {
                    if (!minic_parser_advance(parser)) {
                        return false;
                    }
                    if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                        break;
                    }
                } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                    minic_parser_error(parser,
                                       "expected ',' or '}' in record field array initializer");
                    return false;
                }
            }
            while (element_index < field->element_count) {
                if (!append_static_constant_zero(parser, object_id, field->type)) {
                    return false;
                }
                element_index += 1U;
            }
            if (!minic_parser_expect(parser,
                                     MINIC_TOKEN_RBRACE,
                                     "expected '}' after record field array initializer")) {
                return false;
            }
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
    while (field_index < field_limit) {
        if (!append_static_field_zeros(parser, object_id, &record->fields[field_index])) {
            minic_parser_error(parser, "cannot zero-fill nested static record initializer");
            return false;
        }
        field_index += 1U;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer");
}

static bool
parse_static_constant_value(MinicParser *parser, MinicGlobalObjectId object_id, MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return parse_static_scalar_constant(parser, object_id, type);
    }
    if (minic_type_is_array(type)) {
        return parse_static_array_constant(
            parser, object_id, minic_c0_program_array_type(parser->program, type.array_type_id));
    }
    if (minic_type_is_record(type)) {
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            MinicType explicit_type;

            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_type_name(parser, &explicit_type) ||
                !minic_parser_expect(parser,
                                     MINIC_TOKEN_RPAREN,
                                     "expected ')' after static compound literal type")) {
                return false;
            }
            if (!minic_type_equal(type, explicit_type)) {
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

    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            type,
                                            true,
                                            minic_type_is_const(type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !parse_static_constant_value(parser, object_id, type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot parse nested static record initializer");
        }
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}

static bool ensure_static_record_base_value(MinicParser *parser,
                                            MinicGlobalObjectId object_id,
                                            size_t field_index,
                                            int value) {
    MinicGlobalObject *object;

    if (parser == NULL || object_id >= parser->program->global_object_count) {
        return false;
    }
    object = &parser->program->global_objects[object_id];
    while (object->initializer_count < field_index) {
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
            return false;
        }
        object = &parser->program->global_objects[object_id];
    }
    return object->initializer_count == field_index &&
           minic_c0_global_object_add_initializer(parser->program, object_id, value);
}

static bool parse_static_record_field_initializer(MinicParser *parser,
                                                  MinicGlobalObjectId object_id,
                                                  size_t field_index,
                                                  const MinicRecordField *field) {
    MinicType pointee_type;
    bool function_pointer_field;

    if (field == NULL || field->element_count != 1U) {
        minic_parser_error(parser, "unsupported static record initializer field");
        return false;
    }
    function_pointer_field = minic_type_is_pointer(field->type) &&
                             minic_type_pointee(field->type, &pointee_type) &&
                             minic_type_is_function(pointee_type);
    if (function_pointer_field && parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicFunctionId function_id;
        MinicType designator_type;

        function_id = minic_parser_find_function(parser, parser->current.span);
        if (function_id == MINIC_FUNCTION_INVALID ||
            !function_designator_type(parser, function_id, &designator_type)) {
            minic_parser_error(parser, "static function initializer requires a declared function");
            return false;
        }
        if (!minic_type_assignment_compatible(field->type, designator_type)) {
            minic_parser_error(parser, "static function initializer type does not match field");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            (parser->program->global_objects[object_id].initializer_count != 0U &&
             !ensure_static_record_base_value(parser, object_id, field_index, 0)) ||
            !minic_c0_global_object_add_function_relocation(
                parser->program,
                object_id,
                MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,
                field_index,
                function_id)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static function relocation");
            }
            return false;
        }
        return true;
    }
    if (minic_type_is_pointer(field->type) && !function_pointer_field) {
        MinicExpressionId initializer_id;
        MinicGlobalObjectId target_object_id;

        if (!minic_parser_parse_expression(parser, &initializer_id, 0U)) {
            return false;
        }
        if (!minic_c0_assignment_compatible(parser->program, field->type, initializer_id)) {
            minic_parser_error(parser, "static record pointer initializer type mismatch");
            return false;
        }
        if (minic_c0_expression_is_null_pointer_constant_v0(parser->program, initializer_id)) {
            return parser->program->global_objects[object_id].initializer_count == 0U ||
                   ensure_static_record_base_value(parser, object_id, field_index, 0);
        }
        if (!static_object_address_relocation_target(
                parser->program, initializer_id, &target_object_id)) {
            minic_parser_error(parser,
                               "static record pointer initializer requires a null or zero-addend "
                               "object address constant");
            return false;
        }
        if ((parser->program->global_objects[object_id].initializer_count != 0U &&
             !ensure_static_record_base_value(parser, object_id, field_index, 0)) ||
            !minic_c0_global_object_add_object_relocation(
                parser->program,
                object_id,
                MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD,
                field_index,
                target_object_id)) {
            minic_parser_error(parser, "cannot record static record object relocation");
            return false;
        }
        return true;
    }
    if (minic_type_is_integer(field->type)) {
        int value;

        if (!minic_parser_parse_integer_initializer_value(parser, field->type, &value)) {
            return false;
        }
        if (value == 0 && parser->program->global_objects[object_id].initializer_count == 0U) {
            return true;
        }
        return ensure_static_record_base_value(parser, object_id, field_index, value);
    }
    if (!parse_zero_initializer(parser, field->type)) {
        return false;
    }
    return parser->program->global_objects[object_id].initializer_count == 0U ||
           ensure_static_record_base_value(parser, object_id, field_index, 0);
}

static bool static_record_array_append_value(int **values,
                                             size_t *value_count,
                                             size_t *value_capacity,
                                             int value) {
    int *resized;
    size_t new_capacity;

    if (values == NULL || value_count == NULL || value_capacity == NULL) {
        return false;
    }
    if (*value_count == *value_capacity) {
        new_capacity = *value_capacity == 0U ? 16U : *value_capacity * 2U;
        if (new_capacity < *value_capacity || new_capacity > SIZE_MAX / sizeof(**values)) {
            return false;
        }
        resized = (int *)realloc(*values, new_capacity * sizeof(**values));
        if (resized == NULL) {
            return false;
        }
        *values = resized;
        *value_capacity = new_capacity;
    }
    (*values)[*value_count] = value;
    *value_count += 1U;
    return true;
}

static bool
parse_static_record_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span) {
    const MinicRecord *record;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    int *values;
    size_t value_count;
    size_t value_capacity;
    size_t element_count;
    size_t declared_count;
    bool inferred_bound;
    bool success;
    size_t field_index;

    record = minic_c0_program_record(parser->program, element_type.record_id);
    if (record == NULL || !record->is_complete || record->is_union || record->field_count == 0U) {
        minic_parser_error(parser, "static record array requires a complete non-empty struct type");
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
            !minic_type_is_integer(field->type)) {
            minic_parser_error(
                parser, "static record array currently requires direct scalar integer fields");
            return false;
        }
    }

    values = NULL;
    value_count = 0U;
    value_capacity = 0U;
    element_count = 0U;
    declared_count = 0U;
    inferred_bound = false;
    success = false;

    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        goto done;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser)) {
            goto done;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &declared_count)) {
        goto done;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static record arrays are not supported yet");
        goto done;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static record array") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LBRACE, "expected '{' in record array initializer")) {
        goto done;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (!inferred_bound && element_count >= declared_count) {
            minic_parser_error(parser, "too many static record array initializers");
            goto done;
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_LBRACE, "expected '{' before record array element")) {
            goto done;
        }

        field_index = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            int64_t parsed_value;

            if (field_index >= record->field_count) {
                minic_parser_error(parser, "too many fields in static record array element");
                goto done;
            }
            if (!minic_parser_parse_integer_constant_expression(parser, &parsed_value)) {
                goto done;
            }
            if (parsed_value < INT_MIN || parsed_value > INT_MAX) {
                minic_parser_error(parser,
                                   "static record array initializer is out of supported range");
                goto done;
            }
            if (!static_record_array_append_value(
                    &values, &value_count, &value_capacity, (int)parsed_value)) {
                minic_parser_error(parser,
                                   "out of memory while recording record array initializer");
                goto done;
            }
            field_index += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    goto done;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected ',' or '}' in record array element");
                goto done;
            }
        }
        while (field_index < record->field_count) {
            if (!static_record_array_append_value(&values, &value_count, &value_capacity, 0)) {
                minic_parser_error(parser, "out of memory while zero-filling record array element");
                goto done;
            }
            field_index += 1U;
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_RBRACE, "expected '}' after record array element")) {
            goto done;
        }
        element_count += 1U;

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' after record array element");
            goto done;
        }
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after static record array initializer")) {
        goto done;
    }
    if (element_count == 0U) {
        minic_parser_error(parser, "static record array requires at least one initializer");
        goto done;
    }
    if (inferred_bound) {
        declared_count = element_count;
    } else {
        while (element_count < declared_count) {
            for (field_index = 0U; field_index < record->field_count; ++field_index) {
                if (!static_record_array_append_value(&values, &value_count, &value_capacity, 0)) {
                    minic_parser_error(parser, "out of memory while zero-filling record array");
                    goto done;
                }
            }
            element_count += 1U;
        }
    }
    if (record->field_count > SIZE_MAX / declared_count ||
        value_count != record->field_count * declared_count) {
        minic_parser_error(parser, "invalid static record array initializer shape");
        goto done;
    }

    if (!minic_c0_program_add_array_type(
            parser->program, element_type, declared_count, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id)) {
        minic_parser_error(parser, "cannot create static record array object");
        goto done;
    }
    for (field_index = 0U; field_index < value_count; ++field_index) {
        if (!minic_c0_global_object_add_initializer(
                parser->program, object_id, values[field_index])) {
            minic_parser_error(parser, "cannot record static record array initializer value");
            goto done;
        }
    }
    success = minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static record array");

done:
    free(values);
    return success;
}

static bool parse_static_record(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    const MinicRecord *record;
    size_t field_index;

    record = minic_c0_program_record(parser->program, type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "static record global requires a complete record type");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        return parse_static_record_array(parser, type, name_span);
    }
    if (!static_record_has_direct_function_pointer(record)) {
        return parse_static_nested_record_object(parser, type, name_span);
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            type,
                                            true,
                                            minic_type_is_const(type),
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin static record initializer");
        }
        return false;
    }

    field_index = 0U;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        const MinicRecordField *field;

        if (field_index >= record->field_count) {
            minic_parser_error(parser, "too many static record initializers");
            return false;
        }
        field = minic_c0_record_field(record, field_index);
        if (!parse_static_record_field_initializer(parser, object_id, field_index, field)) {
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
            minic_parser_error(parser, "expected ',' or '}' in record initializer");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer")) {
        return false;
    }
    if (parser->program->global_objects[object_id].initializer_count != 0U) {
        while (parser->program->global_objects[object_id].initializer_count < record->field_count) {
            size_t next_field;

            next_field = parser->program->global_objects[object_id].initializer_count;
            if (!ensure_static_record_base_value(parser, object_id, next_field, 0)) {
                minic_parser_error(parser, "cannot complete static record base initializer");
                return false;
            }
        }
        if (parser->program->global_objects[object_id].initializer_count != record->field_count) {
            minic_parser_error(parser, "invalid mixed static record initializer shape");
            return false;
        }
    } else if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot record static record initializer");
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
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

static bool extern_object_types_compatible(const MinicC0Program *program,
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
        !extern_object_types_compatible(
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

static bool merge_extern_array_composite_type(MinicC0Program *program,
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
    if (!extern_object_types_compatible(program, existing_element, declared_element) ||
        !merge_extern_array_composite_type(program, existing_element, declared_element)) {
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
    if (!extern_object_types_compatible(parser->program, object->type, declared_type)) {
        minic_parser_error(parser, "conflicting extern object redeclaration");
        return false;
    }
    if (minic_type_is_array(object->type) &&
        !merge_extern_array_composite_type(parser->program, object->type, declared_type)) {
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
        bool is_array;
        MinicType declarator_element_type;
        size_t array_type_begin;

        declarator_section_name_length = section_name_length;
        declarator_explicit_alignment = shared_explicit_alignment;
        declarator_has_section = has_section;
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
        if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           declarator_section_name,
                                                           sizeof(declarator_section_name),
                                                           &declarator_section_name_length,
                                                           &declarator_has_section,
                                                           &declarator_explicit_alignment)) {
            return false;
        }
        if (minic_type_is_function(object_type)) {
            minic_parser_error(parser, "unsupported extern object type");
            return false;
        }
        array_type_begin = parser->program->array_type_count;
        if (!minic_parser_parse_array_declarator_suffix(
                parser, object_type, true, &object_type, &is_array) ||
            !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           declarator_section_name,
                                                           sizeof(declarator_section_name),
                                                           &declarator_section_name_length,
                                                           &declarator_has_section,
                                                           &declarator_explicit_alignment)) {
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
                                                 visibility,
                                                 has_visibility)) {
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
                   (has_visibility && !minic_c0_global_object_set_visibility(
                                          parser->program, object_id, visibility))) {
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

static bool
parse_static_pointer_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span) {
    MinicGlobalObjectId *targets;
    MinicType object_type;
    MinicType string_pointer_type;
    MinicGlobalObjectId object_id;
    size_t target_count;
    size_t target_capacity;
    size_t element_count;
    bool inferred_bound;
    bool success;

    targets = NULL;
    target_count = 0U;
    target_capacity = 0U;
    element_count = 0U;
    inferred_bound = false;
    success = false;

    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACKET, "expected '['")) {
        goto done;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        if (!minic_parser_advance(parser)) {
            goto done;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count)) {
        goto done;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static pointer arrays are not supported yet");
        goto done;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{'")) {
        goto done;
    }
    if (!minic_type_pointer_to(minic_type_char(), &string_pointer_type)) {
        minic_parser_error(parser, "cannot build string pointer type");
        goto done;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicGlobalObjectId target_id;

        target_id = MINIC_GLOBAL_OBJECT_INVALID;
        if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            MinicType literal_type;
            MinicSourceSpan literal_span;

            if (!minic_type_assignment_compatible(element_type, string_pointer_type) ||
                !minic_parser_create_string_literal_object(
                    parser, &target_id, &literal_type, &literal_span)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(
                        parser, "string literal does not match static pointer array element");
                }
                goto done;
            }
            (void)literal_type;
            (void)literal_span;
        } else if (!minic_parser_parse_null_pointer_constant_expression(parser, element_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "static pointer array scalar initializer must be null");
            }
            goto done;
        }

        if (!inferred_bound && target_count >= element_count) {
            minic_parser_error(parser, "too many global array initializers");
            goto done;
        }
        if (target_count == target_capacity) {
            size_t new_capacity;
            MinicGlobalObjectId *resized;

            new_capacity = target_capacity == 0U ? 8U : target_capacity * 2U;
            if (new_capacity < target_capacity || new_capacity > SIZE_MAX / sizeof(*targets)) {
                minic_parser_error(parser, "too many static pointer initializers");
                goto done;
            }
            resized = (MinicGlobalObjectId *)realloc(targets, new_capacity * sizeof(*targets));
            if (resized == NULL) {
                minic_parser_error(parser, "out of memory while recording pointer initializers");
                goto done;
            }
            targets = resized;
            target_capacity = new_capacity;
        }
        targets[target_count] = target_id;
        target_count += 1U;

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                goto done;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in initializer");
            goto done;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'")) {
        goto done;
    }
    if (target_count == 0U) {
        minic_parser_error(parser, "static pointer array requires at least one initializer");
        goto done;
    }
    if (inferred_bound) {
        element_count = target_count;
    }

    if (!minic_c0_program_add_array_type(
            parser->program, element_type, element_count, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot create static pointer array object");
        goto done;
    }
    {
        size_t index;

        for (index = 0U; index < target_count; ++index) {
            if (targets[index] != MINIC_GLOBAL_OBJECT_INVALID &&
                !minic_c0_global_object_add_object_relocation(
                    parser->program,
                    object_id,
                    MINIC_GLOBAL_RELOCATION_LOCATION_ARRAY_ELEMENT,
                    index,
                    targets[index])) {
                minic_parser_error(parser, "cannot record static object relocation");
                goto done;
            }
        }
    }
    success =
        minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");

done:
    free(targets);
    return success;
}

static bool parse_static_zero_definition(MinicParser *parser,
                                         MinicType object_type,
                                         MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
         !minic_type_is_record(object_type))) {
        return false;
    }
    if (minic_type_is_record(object_type) &&
        !minic_parser_require_complete_object_type(
            parser, object_type, "static object requires a complete record type")) {
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(object_type),
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot create zero-initialized static object");
        return false;
    }
    return minic_parser_advance(parser);
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
    size_t bounds[8];
    size_t bound_count;
    size_t expected_count;
    size_t index;

    bound_count = 0U;
    expected_count = 1U;
    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
         !minic_type_is_record(element_type))) {
        if (parser != NULL) {
            minic_parser_error(parser, "unsupported static global object type");
        }
        return false;
    }
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
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

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return parse_static_zero_definition(parser, element_type, name_span);
    }
    if (minic_type_is_record(element_type)) {
        return parse_static_record(parser, element_type, name_span);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parse_static_scalar(parser, element_type, name_span);
    }
    if (minic_type_is_pointer(element_type)) {
        return parse_static_pointer_array(parser, element_type, name_span);
    }
    if (!minic_type_is_integer(element_type) || !minic_type_is_const(element_type)) {
        minic_parser_error(parser, "static global arrays currently require const integer elements");
        return false;
    }
    if (minic_type_is_char_integer(element_type)) {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            return parse_static_inferred_char_array(parser,
                                                    element_type,
                                                    name_span,
                                                    section_name,
                                                    section_capacity,
                                                    section_name_length,
                                                    has_section,
                                                    explicit_alignment);
        }
    }

    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
            minic_parser_error(parser, "at most eight array dimensions are supported");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
            return false;
        }
        if (expected_count > SIZE_MAX / bounds[bound_count]) {
            minic_parser_error(parser, "global array element count overflows");
            return false;
        }
        expected_count *= bounds[bound_count];
        bound_count += 1U;
    }
    if (bound_count == 0U) {
        minic_parser_error(parser, "static global object requires a fixed array declarator");
        return false;
    }

    object_type = element_type;
    for (index = bound_count; index > 0U; --index) {
        if (!minic_c0_program_add_array_type(
                parser->program, object_type, bounds[index - 1U], &object_type)) {
            minic_parser_error(parser, "out of memory while building global array type");
            return false;
        }
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            true,
                                            &object_id)) {
        minic_parser_error(parser, "cannot add global object");
        return false;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{'")) {
        return false;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        int value;
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL || object->initializer_count >= expected_count) {
            minic_parser_error(parser, "too many global array initializers");
            return false;
        }
        if (!minic_parser_parse_integer_value(parser, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "out of memory while adding initializer");
            }
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in initializer");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'")) {
        return false;
    }

    {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, object_id);
        while (object != NULL && object->initializer_count < expected_count) {
            if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
                minic_parser_error(parser, "out of memory while zero-filling initializer");
                return false;
            }
            object = minic_c0_program_global_object(parser->program, object_id);
        }
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
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
