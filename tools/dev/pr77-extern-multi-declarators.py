#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_global.c")
text = path.read_text()
start_marker = "bool minic_parser_parse_extern_global(MinicParser *parser) {"
end_marker = "static bool\nparse_static_pointer_array"
start = text.find(start_marker)
end = text.find(end_marker, start + len(start_marker)) if start >= 0 else -1
if start < 0 or end < 0 or text.find(start_marker, start + 1) >= 0:
    raise SystemExit("extern declarator list: cannot uniquely locate parse_extern_global")

replacement = r'''bool minic_parser_parse_extern_global(MinicParser *parser) {
    MinicType base_type;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }

    for (;;) {
        MinicGlobalObjectId object_id;
        MinicSourceSpan name_span;
        MinicType object_type;
        bool is_array;

        if (!minic_parser_parse_pointer_declarator(parser, base_type, &object_type)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            if (!parse_extern_function_pointer_object_declarator(
                    parser, object_type, &name_span, &object_type)) {
                return false;
            }
        } else {
            if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected extern object name");
                return false;
            }
            name_span = parser->current.span;
            if (!minic_parser_advance(parser)) {
                return false;
            }
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

        is_array = false;
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            size_t element_count;
            MinicType array_type;

            is_array = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
                if (!minic_c0_program_add_incomplete_array_type(
                        parser->program, object_type, &array_type) ||
                    !minic_parser_advance(parser)) {
                    minic_parser_error(parser, "cannot declare incomplete extern array");
                    return false;
                }
            } else {
                if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
                    !minic_c0_program_add_array_type(
                        parser->program, object_type, element_count, &array_type)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser, "cannot declare extern array");
                    }
                    return false;
                }
            }
            object_type = array_type;
        }

        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                false,
                                                is_array
                                                    ? minic_type_is_const(
                                                          parser->program->array_types
                                                              [object_type.array_type_id]
                                                                  .element_type)
                                                    : minic_type_is_const(object_type),
                                                &object_id) ||
            !minic_c0_global_object_set_extern(parser->program, object_id)) {
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

'''
path.write_text(text[:start] + replacement + text[end:])
print("staged extern object declarator lists with per-declarator pointer/function-pointer/array shape")
