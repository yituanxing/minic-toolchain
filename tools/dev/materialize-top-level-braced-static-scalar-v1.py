#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_global.c"
text = path.read_text()

old = '''static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;

    if (!begin_static_object_definition(parser, type, name_span, &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
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
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
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
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}
'''
new = '''static bool parse_static_scalar(MinicParser *parser, MinicType type, MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    bool braced;

    if (!begin_static_object_definition(parser, type, name_span, &object_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
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
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
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
        if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE,
                                 "expected '}' after static scalar initializer")) {
            return false;
        }
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}
'''
if text.count(old) != 1:
    raise SystemExit(f"top-level static scalar function count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
