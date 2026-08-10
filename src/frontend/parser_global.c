#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,
                                                    MinicSourceSpan name_span) {
    MinicGlobalObjectId static_local_id;
    size_t name_length;
    size_t index;

    static_local_id = minic_parser_find_static_local(parser, name_span);
    if (static_local_id != MINIC_GLOBAL_OBJECT_INVALID) {
        return static_local_id;
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
        int64_t constant_value;
        int value;

        if (!minic_parser_parse_integer_constant_expression(parser, &constant_value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(
                    parser, "static integer initializer requires an integer constant expression");
            }
            return false;
        }
        if (constant_value < INT_MIN || constant_value > INT_MAX) {
            minic_parser_error(parser, "static integer initializer is out of supported range");
            return false;
        }
        value = (int)constant_value;
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            minic_parser_error(parser, "cannot record static integer initializer");
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
                    parser->program, object_id, 0U, function_id) ||
                !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                minic_parser_error(parser, "cannot record static function pointer initializer");
                return false;
            }
        } else if (!minic_parser_parse_zero_pointer_constant(parser) ||
                   !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
            return false;
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
        int64_t parsed;

        if (!minic_parser_parse_integer_constant_expression(parser, &parsed) || parsed < INT_MIN ||
            parsed > INT_MAX ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, (int)parsed)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "static aggregate integer initializer is out of range");
            }
            return false;
        }
    } else if (minic_type_is_pointer(type)) {
        if (!minic_parser_parse_zero_pointer_constant(parser) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static null-pointer initializer");
            }
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
            !minic_c0_global_object_add_function_relocation(
                parser->program, object_id, field_index, function_id)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static function relocation");
            }
            return false;
        }
        return true;
    }
    return parse_zero_initializer(parser, field->type);
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
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer") ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot record static record initializer");
        }
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

bool minic_parser_parse_extern_global_after_head(MinicParser *parser,
                                                 MinicType base_type,
                                                 MinicType first_object_type,
                                                 MinicSourceSpan first_name_span,
                                                 const char *section_name,
                                                 size_t section_name_length,
                                                 bool has_section,
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
        bool declarator_has_section;
        bool is_array;
        MinicType declarator_element_type;

        declarator_section_name_length = section_name_length;
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
        if (!minic_parser_parse_gnu_section_attribute(parser,
                                                      declarator_section_name,
                                                      sizeof(declarator_section_name),
                                                      &declarator_section_name_length,
                                                      &declarator_has_section)) {
            return false;
        }
        if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||
            minic_type_is_array(object_type)) {
            minic_parser_error(parser, "unsupported extern object type");
            return false;
        }
        if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
            minic_parser_error(parser, "duplicate global object");
            return false;
        }

        if (!minic_parser_parse_array_declarator_suffix(
                parser, object_type, true, &object_type, &is_array) ||
            !minic_parser_parse_gnu_section_attribute(parser,
                                                      declarator_section_name,
                                                      sizeof(declarator_section_name),
                                                      &declarator_section_name_length,
                                                      &declarator_has_section)) {
            return false;
        }

        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                false,
                                                minic_type_is_const(declarator_element_type),
                                                &object_id) ||
            !minic_c0_global_object_set_extern(parser->program, object_id) ||
            (declarator_has_section &&
             !minic_c0_global_object_set_section(parser->program,
                                                 object_id,
                                                 declarator_section_name,
                                                 declarator_section_name_length)) ||
            (has_visibility &&
             !minic_c0_global_object_set_visibility(parser->program, object_id, visibility))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot declare extern object");
            }
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
    bool has_section;

    section_name_length = 0U;
    has_section = false;
    (void)memset(section_name, 0, sizeof(section_name));
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_section_attribute(
            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||
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
        } else if (!minic_parser_parse_zero_pointer_constant(parser)) {
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
                    parser->program, object_id, index, targets[index])) {
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
                                             MinicSourceSpan name_span) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t element_count;

    if (parser == NULL || !minic_type_is_char_integer(element_type) ||
        !minic_type_is_const(element_type) || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' in inferred static character array") ||
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

bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType element_type,
                                                 MinicSourceSpan name_span) {
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
            return parse_static_inferred_char_array(parser, element_type, name_span);
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
    return minic_parser_parse_static_global_after_head(parser, object_type, name_span);
}
