#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


marker = '''bool minic_parser_parse_static_global(MinicParser *parser) {
'''
helper = r'''static bool parse_static_inferred_char_array(MinicParser *parser,
                                              MinicType element_type,
                                              MinicSourceSpan name_span) {
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t element_count;

    if (parser == NULL || !minic_type_is_char_integer(element_type) ||
        !minic_type_is_const(element_type) || parser->current.kind != MINIC_TOKEN_LBRACKET ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(parser,
                            MINIC_TOKEN_RBRACKET,
                            "expected ']' in inferred static character array") ||
        !minic_c0_program_add_incomplete_array_type(parser->program, element_type, &object_type) ||
        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            true,
                                            &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after static array")) {
        if (parser != NULL && parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot begin inferred static character array");
        }
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
        !minic_parser_add_string_literal_initializer(parser, object_id, &element_count) ||
        !minic_c0_program_complete_array_type(parser->program, object_type, element_count)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "inferred static character array requires a string literal initializer");
        }
        return false;
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after static character array");
}

'''
replace_once(
    "src/frontend/parser_global.c",
    marker,
    helper + marker,
    "static inferred char helper anchor",
)

old = '''    if (!minic_type_is_integer(element_type) || !minic_type_is_const(element_type)) {
        minic_parser_error(parser, "static global arrays currently require const integer elements");
        return false;
    }

    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
'''
new = '''    if (!minic_type_is_integer(element_type) || !minic_type_is_const(element_type)) {
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
'''
replace_once(
    "src/frontend/parser_global.c",
    old,
    new,
    "static inferred char dispatch",
)

print("staged inferred static const char arrays from string literals")
