#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_typedef.c")
text = path.read_text()

old = r'''bool minic_parser_parse_enum_definition(MinicParser *parser) {
    int next_value;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_ENUM, "expected keyword 'enum'")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected enum tag");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' after enum tag")) {
        return false;
    }

    next_value = 0;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicSourceSpan name_span;
        int value;

        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected enumerator name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        value = next_value;
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            if (!minic_parser_advance(parser) || !parse_enum_integer_value(parser, &value)) {
                return false;
            }
        }
        if (!minic_parser_bind_enum_constant(parser, name_span, value)) {
            return false;
        }
        if (value == INT_MAX) {
            next_value = INT_MAX;
        } else {
            next_value = value + 1;
        }

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' after enumerator");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after enum definition")) {
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after enum definition");
}
'''
new = r'''static bool parse_enum_definition_specifier(MinicParser *parser) {
    int next_value;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_ENUM, "expected keyword 'enum'")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' after enum specifier")) {
        return false;
    }

    next_value = 0;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicSourceSpan name_span;
        int value;

        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected enumerator name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        value = next_value;
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            if (!minic_parser_advance(parser) || !parse_enum_integer_value(parser, &value)) {
                return false;
            }
        }
        if (!minic_parser_bind_enum_constant(parser, name_span, value)) {
            return false;
        }
        next_value = value == INT_MAX ? INT_MAX : value + 1;

        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' after enumerator");
            return false;
        }
    }
    return minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after enum definition");
}

bool minic_parser_parse_enum_definition(MinicParser *parser) {
    return parse_enum_definition_specifier(parser) &&
           minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after enum definition");
}
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected enum definition block count={text.count(old)}")
text = text.replace(old, new, 1)

old = r'''    bool is_function_pointer;
    bool is_record_definition;

    bound_count = 0U;
    is_function_pointer = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'") ||
        !typedef_starts_record_definition(parser, &is_record_definition)) {
        return false;
    }
    if (is_record_definition) {
        if (!minic_parser_parse_record_definition_specifier(parser, &aliased_type)) {
            return false;
        }
    } else {
'''
new = r'''    bool is_enum_definition;
    bool is_function_pointer;
    bool is_record_definition;

    bound_count = 0U;
    is_function_pointer = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'")) {
        return false;
    }
    is_enum_definition = parser->current.kind == MINIC_TOKEN_KW_ENUM;
    if (!typedef_starts_record_definition(parser, &is_record_definition)) {
        return false;
    }
    if (is_enum_definition) {
        if (!parse_enum_definition_specifier(parser)) {
            return false;
        }
        aliased_type = minic_type_int();
    } else if (is_record_definition) {
        if (!minic_parser_parse_record_definition_specifier(parser, &aliased_type)) {
            return false;
        }
    } else {
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected typedef dispatch block count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged reusable enum definition specifiers and typedef enum")
