#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

old = '''    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        minic_parser_error(parser, "static local initializers are not supported yet");
        return false;
    }
    if (!minic_parser_require_complete_object_type(
            parser, declared_type, "static local array requires a complete element type")) {
'''
new = '''    if (parser->current.kind == MINIC_TOKEN_EQUAL &&
        (bound_count != 1U || !minic_type_is_integer(declared_type))) {
        minic_parser_error(parser,
                           "initialized static local array currently requires one integer dimension");
        return false;
    }
    if (!minic_parser_require_complete_object_type(
            parser, declared_type, "static local array requires a complete element type")) {
'''
if text.count(old) != 1:
    raise SystemExit(f"static fixed initializer precheck: expected 1 match, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            object_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot add zero-initialized static local object");
        return false;
    }
    return minic_parser_bind_static_local(parser, name_span, object_id);
'''
new = r'''    if (!minic_c0_program_add_global_object(parser->program,
                                            symbol_name,
                                            (size_t)symbol_length,
                                            object_type,
                                            true,
                                            minic_type_is_const(declared_type),
                                            &object_id)) {
        minic_parser_error(parser, "cannot add static local array object");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        size_t initializer_count;

        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_LBRACE, "expected '{' in static local array initializer")) {
            return false;
        }
        initializer_count = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            int64_t parsed;

            if (!minic_parser_parse_integer_constant_expression(parser, &parsed)) {
                return false;
            }
            if (parsed < INT_MIN || parsed > INT_MAX) {
                minic_parser_error(parser,
                                   "static local integer array initializer is out of supported range");
                return false;
            }
            if (initializer_count >= bounds[0]) {
                minic_parser_error(parser, "too many static local integer array initializers");
                return false;
            }
            if (!minic_c0_global_object_add_initializer(
                    parser->program, object_id, (int)parsed)) {
                minic_parser_error(parser, "cannot record static local integer array initializer");
                return false;
            }
            initializer_count += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected ',' or '}' in static local array initializer");
                return false;
            }
        }
        if (initializer_count == 0U ||
            !minic_parser_expect(parser,
                                 MINIC_TOKEN_RBRACE,
                                 "expected '}' after static local array initializer")) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "static local integer array requires at least one initializer");
            }
            return false;
        }
    } else if (!minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot zero-initialize static local array object");
        return false;
    }
    return minic_parser_bind_static_local(parser, name_span, object_id);
'''
if text.count(old) != 1:
    raise SystemExit(f"static fixed initializer storage: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged fixed-bound static local integer arrays with brace initializers")
