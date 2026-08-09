#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

old = '''static bool parse_external_pointer_definition(MinicParser *parser,
                                              MinicType object_type,
                                              MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId target_id;
    MinicSourceSpan literal_span;
    MinicType literal_type;
    MinicType literal_pointer_type;
    const MinicArrayType *literal_array;
    MinicGlobalObject *object;

    if (parser == NULL || !minic_type_is_pointer(object_type) ||
        parser->current.kind != MINIC_TOKEN_EQUAL) {
        minic_parser_error(parser, "unsupported external object definition");
        return false;
    }

    object_id = minic_parser_find_global_object(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                false,
                                                minic_type_is_const(object_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external object definition");
            return false;
        }
    } else {
        object = &parser->program->global_objects[object_id];
        if (!object->is_extern || !minic_type_equal(object->type, object_type) ||
            object->initializer_count != 0U || object->function_relocation_count != 0U ||
            object->object_relocation_count != 0U || object->is_zero_initialized) {
            minic_parser_error(parser, "conflicting external object definition");
            return false;
        }
        object->is_extern = false;
    }

    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
        !minic_parser_create_string_literal_object(
            parser, &target_id, &literal_type, &literal_span)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser,
                               "external pointer definition requires a string literal initializer");
        }
        return false;
    }
    literal_array = minic_c0_program_array_type(parser->program, literal_type.array_type_id);
    if (literal_array == NULL || !minic_type_is_array(literal_type) ||
        !minic_type_pointer_to(literal_array->element_type, &literal_pointer_type) ||
        !minic_type_assignment_compatible(object_type, literal_pointer_type) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
        !minic_c0_global_object_add_object_relocation(parser->program, object_id, 0U, target_id)) {
        minic_parser_error(parser, "external pointer initializer type mismatch");
        return false;
    }
    (void)literal_span;
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
}
'''

new = '''static bool parse_external_object_definition(MinicParser *parser,
                                             MinicType object_type,
                                             MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId target_id;
    MinicSourceSpan literal_span;
    MinicType literal_type;
    MinicType literal_pointer_type;
    const MinicArrayType *literal_array;
    MinicGlobalObject *object;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_EQUAL ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type))) {
        minic_parser_error(parser, "unsupported external object definition");
        return false;
    }

    object_id = minic_parser_find_global_object(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                false,
                                                minic_type_is_const(object_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external object definition");
            return false;
        }
    } else {
        object = &parser->program->global_objects[object_id];
        if (!object->is_extern || !minic_type_equal(object->type, object_type) ||
            object->initializer_count != 0U || object->function_relocation_count != 0U ||
            object->object_relocation_count != 0U || object->is_zero_initialized) {
            minic_parser_error(parser, "conflicting external object definition");
            return false;
        }
        object->is_extern = false;
    }

    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(object_type)) {
        int value;

        if (!minic_parser_parse_integer_value(parser, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "cannot record external integer initializer");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
    }

    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
        !minic_parser_create_string_literal_object(
            parser, &target_id, &literal_type, &literal_span)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
            minic_parser_error(parser,
                               "external pointer definition requires a string literal initializer");
        }
        return false;
    }
    literal_array = minic_c0_program_array_type(parser->program, literal_type.array_type_id);
    if (literal_array == NULL || !minic_type_is_array(literal_type) ||
        !minic_type_pointer_to(literal_array->element_type, &literal_pointer_type) ||
        !minic_type_assignment_compatible(object_type, literal_pointer_type) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
        !minic_c0_global_object_add_object_relocation(parser->program, object_id, 0U, target_id)) {
        minic_parser_error(parser, "external pointer initializer type mismatch");
        return false;
    }
    (void)literal_span;
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
}
'''

if text.count(old) != 1:
    raise SystemExit("unexpected external pointer definition helper")
text = text.replace(old, new, 1)
old_call = "        return parse_external_pointer_definition(parser, return_type, name_span);\n"
new_call = "        return parse_external_object_definition(parser, return_type, name_span);\n"
if text.count(old_call) != 1:
    raise SystemExit("unexpected external object definition call")
path.write_text(text.replace(old_call, new_call, 1))
print("staged external integer scalar definitions alongside existing pointer relocation definitions")
