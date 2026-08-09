#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

marker = "static bool parse_static_local_array_declarator(MinicParser *parser, MinicType base_type) {\n"
if text.count(marker) != 1:
    raise SystemExit("unexpected static-local declarator marker")

helper = r'''static bool parse_inferred_static_local_string_array(MinicParser *parser,
                                                     MinicType element_type,
                                                     MinicSourceSpan name_span) {
    const MinicArrayType *literal_array;
    MinicGlobalObjectId object_id;
    MinicSourceSpan literal_span;
    MinicType literal_type;
    MinicType object_type;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_type_is_char_integer(element_type)) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after inferred array") ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "inferred static local char array requires a string literal");
        }
        return false;
    }
    if (!minic_parser_create_string_literal_object(
            parser, &object_id, &literal_type, &literal_span)) {
        return false;
    }
    literal_array = minic_c0_program_array_type(parser->program, literal_type.array_type_id);
    if (literal_array == NULL || !minic_type_is_array(literal_type) ||
        !minic_c0_program_add_array_type(
            parser->program, element_type, literal_array->element_count, &object_type)) {
        minic_parser_error(parser, "cannot infer static local string array type");
        return false;
    }

    /* The literal helper owns decoding and byte initialization. Re-type that internal
       object to the declaration's qualified char element type, then bind the source-level
       static-local name to the same storage object. */
    parser->program->global_objects[object_id].type = object_type;
    parser->program->global_objects[object_id].is_read_only = minic_type_is_const(element_type);
    return minic_parser_bind_static_local(parser, name_span, object_id);
}

'''
text = text.replace(marker, helper + marker, 1)

old = r'''    bound_count = 0U;
    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
'''
new = r'''    bound_count = 0U;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            return parse_inferred_static_local_string_array(parser, declared_type, name_span);
        }
    }
    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
'''
if text.count(old) != 1:
    raise SystemExit("unexpected static-local bound loop")
path.write_text(text.replace(old, new, 1))
print("staged inferred static local char arrays from string literals")
