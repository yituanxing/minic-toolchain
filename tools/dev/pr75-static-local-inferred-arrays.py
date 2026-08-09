#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
start_marker = "static bool parse_inferred_static_local_string_array(MinicParser *parser,\n"
end_marker = "static bool parse_static_local_array_declarator(MinicParser *parser, MinicType base_type) {\n"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate inferred static-local array helper")

helper = r'''static bool parse_inferred_static_local_array(MinicParser *parser,
                                              MinicType element_type,
                                              MinicSourceSpan name_span) {
    const MinicArrayType *literal_array;
    MinicGlobalObjectId object_id;
    MinicSourceSpan literal_span;
    MinicType literal_type;
    MinicType object_type;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'") ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after inferred array")) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_type_is_char_integer(element_type)) {
            minic_parser_error(parser,
                               "string initializer requires a character static local array");
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

    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        char symbol_name[96];
        size_t initializer_count;
        int symbol_length;

        if (!minic_type_is_integer(element_type)) {
            minic_parser_error(parser,
                               "brace-initialized inferred static array requires integer elements");
            return false;
        }
        symbol_length = snprintf(symbol_name,
                                 sizeof(symbol_name),
                                 "__minic_static_local_%zu_%zu",
                                 (size_t)parser->current_function,
                                 parser->program->global_object_count);
        if (symbol_length <= 0 || (size_t)symbol_length >= sizeof(symbol_name) ||
            !minic_c0_program_add_incomplete_array_type(
                parser->program, element_type, &object_type) ||
            !minic_c0_program_add_global_object(parser->program,
                                                symbol_name,
                                                (size_t)symbol_length,
                                                object_type,
                                                true,
                                                minic_type_is_const(element_type),
                                                &object_id) ||
            !minic_parser_advance(parser)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot begin inferred static local integer array");
            }
            return false;
        }

        initializer_count = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            MinicExpressionId value_id;
            int value;

            if (!minic_parser_parse_expression(parser, &value_id, 1U) ||
                !static_record_integer_constant(parser->program, value_id, &value) ||
                !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "static local array requires integer constant initializers");
                }
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
                                 "expected '}' after static local array initializer") ||
            !minic_c0_program_complete_array_type(
                parser->program, object_type, initializer_count) ||
            !minic_parser_bind_static_local(parser, name_span, object_id)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot finalize inferred static local integer array");
            }
            return false;
        }
        return true;
    }

    minic_parser_error(parser,
                       "inferred static local array requires a string or brace initializer");
    return false;
}

'''
text = text[:start] + helper + text[end:]
old = "            return parse_inferred_static_local_string_array(parser, declared_type, name_span);"
new = "            return parse_inferred_static_local_array(parser, declared_type, name_span);"
if text.count(old) != 1:
    raise SystemExit(f"inferred static-local dispatch: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged inferred static local integer arrays with brace initializers")
