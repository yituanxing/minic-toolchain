#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

marker = "bool minic_parser_parse_parameter_list(MinicParser *parser,\n"
if text.count(marker) != 1:
    raise SystemExit("unexpected parameter-list function marker")

helper = r'''static bool parse_function_pointer_parameter_declarator(MinicParser *parser,
                                                        MinicType return_type,
                                                        MinicSourceSpan *name_span,
                                                        bool *has_name,
                                                        MinicType *parameter_type,
                                                        bool require_name) {
    MinicType nested_parameter_types[8];
    MinicType function_type;
    size_t nested_parameter_count;
    size_t pointer_depth;
    bool is_variadic;

    if (parser == NULL || name_span == NULL || has_name == NULL || parameter_type == NULL) {
        return false;
    }
    *has_name = false;
    nested_parameter_count = 0U;
    pointer_depth = 0U;
    is_variadic = false;
    (void)memset(nested_parameter_types, 0, sizeof(nested_parameter_types));

    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function pointer parameter")) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        pointer_depth += 1U;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (pointer_depth == 0U) {
        minic_parser_error(parser, "function pointer parameter requires '*'");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        *name_span = parser->current.span;
        *has_name = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (require_name) {
        minic_parser_error(parser, "expected function pointer parameter name");
        return false;
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function pointer parameter") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function pointer parameter list") ||
        !minic_parser_parse_parameter_list(parser,
                                           NULL,
                                           nested_parameter_types,
                                           &nested_parameter_count,
                                           false,
                                           &is_variadic) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function pointer parameter list")) {
        return false;
    }
    if (is_variadic) {
        minic_parser_error(parser, "variadic function pointer parameters are not supported yet");
        return false;
    }
    if (!minic_c0_program_add_function_type(parser->program,
                                            return_type,
                                            nested_parameter_types,
                                            nested_parameter_count,
                                            &function_type)) {
        minic_parser_error(parser, "cannot build function pointer parameter type");
        return false;
    }
    while (pointer_depth > 0U) {
        if (!minic_type_pointer_to(function_type, &function_type)) {
            minic_parser_error(parser, "function pointer parameter depth is unsupported");
            return false;
        }
        pointer_depth -= 1U;
    }
    *parameter_type = function_type;
    return true;
}

'''
text = text.replace(marker, helper + marker, 1)

old = r'''    for (;;) {
        MinicType parameter_type;

        if (*parameter_count >= 8U) {
            minic_parser_error(parser, "at most eight parameters are supported");
            return false;
        }
        if (!minic_parser_parse_type_name(parser, &parameter_type)) {
            return false;
        }
        if (minic_type_is_void(parameter_type)) {
            if (*parameter_count == 0U && parser->current.kind == MINIC_TOKEN_RPAREN) {
                return true;
            }
            minic_parser_error(parser, "parameter type cannot be bare void");
            return false;
        }

        if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
            if (parameter_name_spans != NULL) {
                parameter_name_spans[*parameter_count] = parser->current.span;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (require_names) {
            minic_parser_error(parser, "expected parameter name");
            return false;
        }

        parameter_types[*parameter_count] = parameter_type;
'''

new = r'''    for (;;) {
        MinicSourceSpan declarator_name_span;
        MinicType parameter_type;
        bool declarator_has_name;
        bool is_function_pointer_parameter;

        if (*parameter_count >= 8U) {
            minic_parser_error(parser, "at most eight parameters are supported");
            return false;
        }
        if (!minic_parser_parse_type_name(parser, &parameter_type)) {
            return false;
        }
        (void)memset(&declarator_name_span, 0, sizeof(declarator_name_span));
        declarator_has_name = false;
        is_function_pointer_parameter = parser->current.kind == MINIC_TOKEN_LPAREN;
        if (is_function_pointer_parameter &&
            !parse_function_pointer_parameter_declarator(parser,
                                                         parameter_type,
                                                         &declarator_name_span,
                                                         &declarator_has_name,
                                                         &parameter_type,
                                                         require_names)) {
            return false;
        }
        if (!is_function_pointer_parameter && minic_type_is_void(parameter_type)) {
            if (*parameter_count == 0U && parser->current.kind == MINIC_TOKEN_RPAREN) {
                return true;
            }
            minic_parser_error(parser, "parameter type cannot be bare void");
            return false;
        }

        if (is_function_pointer_parameter) {
            if (declarator_has_name && parameter_name_spans != NULL) {
                parameter_name_spans[*parameter_count] = declarator_name_span;
            }
        } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
            if (parameter_name_spans != NULL) {
                parameter_name_spans[*parameter_count] = parser->current.span;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (require_names) {
            minic_parser_error(parser, "expected parameter name");
            return false;
        }

        parameter_types[*parameter_count] = parameter_type;
'''

if text.count(old) != 1:
    raise SystemExit("unexpected parameter-list body")
path.write_text(text.replace(old, new, 1))
print("staged direct function-pointer parameter declarators")
