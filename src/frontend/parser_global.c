#include "frontend/parser_internal.h"

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

static bool parse_zero_pointer_constant(MinicParser *parser) {
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
            return parse_zero_pointer_constant(parser);
        }
        if (!parse_zero_pointer_constant(parser) ||
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
        return parse_zero_pointer_constant(parser);
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
        int value;

        if (!minic_parser_parse_integer_value(parser, &value) ||
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
                    parser->program, object_id, 0U, function_id) ||
                !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
                minic_parser_error(parser, "cannot record static function pointer initializer");
                return false;
            }
        } else if (!parse_zero_pointer_constant(parser) ||
                   !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
            return false;
        }
    } else {
        minic_parser_error(parser, "unsupported static scalar type");
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
        minic_parser_error(parser, "static record array globals are not supported");
        return false;
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

bool minic_parser_parse_extern_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType object_type;
    MinicGlobalObjectId object_id;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_name(parser, &object_type)) {
        return false;
    }
    if (minic_type_is_void(object_type) || minic_type_is_function(object_type) ||
        minic_type_is_array(object_type)) {
        minic_parser_error(parser, "unsupported extern object type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected extern object name");
        return false;
    }
    name_span = parser->current.span;
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            false,
                                            minic_type_is_const(object_type),
                                            &object_id) ||
        !minic_c0_global_object_set_extern(parser->program, object_id) ||
        !minic_parser_advance(parser)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot declare extern object");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after extern object declaration");
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
        } else if (!parse_zero_pointer_constant(parser)) {
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

bool minic_parser_parse_static_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType element_type;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t bounds[8];
    size_t bound_count;
    size_t expected_count;
    size_t index;

    bound_count = 0U;
    expected_count = 1U;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_name(parser, &element_type)) {
        return false;
    }
    if (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
        !minic_type_is_record(element_type)) {
        minic_parser_error(parser, "unsupported static global object type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected global object name");
        return false;
    }

    name_span = parser->current.span;
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
    if (!minic_parser_advance(parser)) {
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
