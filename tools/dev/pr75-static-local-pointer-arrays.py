#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
helper_marker = "static bool parse_inferred_static_local_array(MinicParser *parser,\n"
helper_start = text.find(helper_marker)
if helper_start < 0:
    raise SystemExit("cannot locate inferred static-local array helper")
start = text.find("    if (parser->current.kind == MINIC_TOKEN_LBRACE) {\n", helper_start)
end_marker = '''    minic_parser_error(parser,
                       "inferred static local array requires a string or brace initializer");
'''
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate inferred static-local brace initializer branch")

replacement = r'''    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        char symbol_name[96];
        size_t initializer_count;
        int symbol_length;
        bool is_pointer_array;

        is_pointer_array = minic_type_is_pointer(element_type);
        if (!minic_type_is_integer(element_type) && !is_pointer_array) {
            minic_parser_error(parser,
                               "brace-initialized inferred static array requires integer or pointer elements");
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
            (is_pointer_array &&
             !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) ||
            !minic_parser_advance(parser)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot begin inferred static local array");
            }
            return false;
        }

        initializer_count = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            if (is_pointer_array) {
                MinicGlobalObjectId target_id;
                bool has_relocation;

                target_id = MINIC_GLOBAL_OBJECT_INVALID;
                has_relocation = false;
                if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
                    MinicSourceSpan string_span;
                    MinicType string_type;
                    MinicType source_pointer_type;
                    const MinicArrayType *string_array;

                    if (!minic_parser_create_string_literal_object(
                            parser, &target_id, &string_type, &string_span)) {
                        return false;
                    }
                    string_array =
                        minic_c0_program_array_type(parser->program, string_type.array_type_id);
                    if (string_array == NULL || !minic_type_is_array(string_type) ||
                        !minic_type_pointer_to(
                            string_array->element_type, &source_pointer_type) ||
                        !minic_type_assignment_compatible(element_type, source_pointer_type)) {
                        minic_parser_error(parser,
                                           "static local pointer array string initializer type mismatch");
                        return false;
                    }
                    (void)string_span;
                    has_relocation = true;
                } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
                    const MinicGlobalObject *target;
                    MinicType source_pointer_type;

                    target_id = minic_parser_find_global_object(parser, parser->current.span);
                    target = target_id == MINIC_GLOBAL_OBJECT_INVALID
                                 ? NULL
                                 : minic_c0_program_global_object(parser->program, target_id);
                    if (target == NULL) {
                        minic_parser_error(parser,
                                           "static local pointer array initializer requires a known object");
                        return false;
                    }
                    if (minic_type_is_array(target->type)) {
                        const MinicArrayType *target_array;

                        target_array =
                            minic_c0_program_array_type(parser->program, target->type.array_type_id);
                        if (target_array == NULL ||
                            !minic_type_pointer_to(
                                target_array->element_type, &source_pointer_type)) {
                            minic_parser_error(parser,
                                               "cannot decay static pointer array initializer object");
                            return false;
                        }
                    } else if (!minic_type_pointer_to(target->type, &source_pointer_type)) {
                        minic_parser_error(parser,
                                           "cannot take address of static pointer array initializer object");
                        return false;
                    }
                    if (!minic_type_assignment_compatible(element_type, source_pointer_type) ||
                        !minic_parser_advance(parser)) {
                        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                            minic_parser_error(
                                parser, "static local pointer array object initializer type mismatch");
                        }
                        return false;
                    }
                    has_relocation = true;
                } else {
                    int64_t parsed;

                    if (!minic_parser_parse_integer_constant_expression(parser, &parsed) ||
                        parsed != 0) {
                        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                            minic_parser_error(
                                parser, "static local pointer array scalar initializer must be null");
                        }
                        return false;
                    }
                }
                if (has_relocation &&
                    !minic_c0_global_object_add_object_relocation(
                        parser->program, object_id, initializer_count, target_id)) {
                    minic_parser_error(parser,
                                       "cannot record static local pointer array relocation");
                    return false;
                }
            } else {
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
                minic_parser_error(parser, "cannot finalize inferred static local array");
            }
            return false;
        }
        return true;
    }

'''
path.write_text(text[:start] + replacement + text[end:])
print("staged inferred static local pointer arrays with object relocations")
