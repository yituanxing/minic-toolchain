#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

MinicTypeAliasId minic_parser_find_type_alias(const MinicParser *parser,
                                              MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    if (minic_parser_name_bound(parser, name_span)) {
        return MINIC_TYPE_ALIAS_INVALID;
    }

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->type_alias_count; ++index) {
        const MinicTypeAlias *alias;

        alias = minic_c0_program_type_alias(parser->program, index);
        if (alias != NULL && alias->name_length == name_length &&
            memcmp(alias->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_TYPE_ALIAS_INVALID;
}

bool minic_parser_find_enum_constant(const MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     int *value) {
    size_t index;

    if (parser == NULL) {
        return false;
    }
    for (index = parser->enum_constant_count; index > 0U; --index) {
        const MinicParserEnumConstant *constant;

        constant = &parser->enum_constants[index - 1U];
        if (minic_parser_span_equals(parser, name_span, constant->name_span)) {
            if (value != NULL) {
                *value = constant->value;
            }
            return true;
        }
    }
    return false;
}

bool minic_parser_bind_enum_constant(MinicParser *parser, MinicSourceSpan name_span, int value) {
    MinicParserEnumConstant *resized;
    size_t new_capacity;

    if (parser == NULL || minic_parser_find_enum_constant(parser, name_span, NULL)) {
        if (parser != NULL) {
            minic_parser_error(parser, "duplicate enumerator name");
        }
        return false;
    }
    if (parser->enum_constant_count == parser->enum_constant_capacity) {
        new_capacity =
            parser->enum_constant_capacity == 0U ? 16U : parser->enum_constant_capacity * 2U;
        if (new_capacity < parser->enum_constant_capacity ||
            new_capacity > SIZE_MAX / sizeof(*parser->enum_constants)) {
            minic_parser_error(parser, "too many enum constants");
            return false;
        }
        resized = (MinicParserEnumConstant *)realloc(
            parser->enum_constants, new_capacity * sizeof(*parser->enum_constants));
        if (resized == NULL) {
            minic_parser_error(parser, "out of memory while binding enum constant");
            return false;
        }
        parser->enum_constants = resized;
        parser->enum_constant_capacity = new_capacity;
    }
    parser->enum_constants[parser->enum_constant_count].name_span = name_span;
    parser->enum_constants[parser->enum_constant_count].value = value;
    parser->enum_constant_count += 1U;
    return true;
}

void minic_parser_destroy_enum_constants(MinicParser *parser) {
    if (parser == NULL) {
        return;
    }
    free(parser->enum_constants);
    parser->enum_constants = NULL;
    parser->enum_constant_count = 0U;
    parser->enum_constant_capacity = 0U;
}

static bool parse_enum_integer_value(MinicParser *parser, int *value) {
    bool negative;
    int parsed;

    negative = parser->current.kind == MINIC_TOKEN_MINUS;
    if (negative && !minic_parser_advance(parser)) {
        return false;
    }
    if (!minic_parser_parse_integer_value(parser, &parsed)) {
        return false;
    }
    *value = negative ? -parsed : parsed;
    return true;
}

bool minic_parser_parse_enum_definition(MinicParser *parser) {
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

static bool typedef_starts_record_definition(MinicParser *parser, bool *is_definition) {
    MinicParser probe;

    if (parser == NULL || is_definition == NULL) {
        return false;
    }
    *is_definition = false;
    if (parser->current.kind != MINIC_TOKEN_KW_STRUCT &&
        parser->current.kind != MINIC_TOKEN_KW_UNION) {
        return true;
    }

    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_LBRACE) {
        *is_definition = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        return true;
    }
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *is_definition = probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

static bool parse_function_pointer_typedef(MinicParser *parser,
                                           MinicType return_type,
                                           MinicSourceSpan *name_span,
                                           MinicType *aliased_type) {
    MinicType parameter_types[8];
    MinicType function_type;
    size_t parameter_count;
    size_t pointer_depth;
    bool is_variadic;

    if (parser == NULL || name_span == NULL || aliased_type == NULL) {
        return false;
    }
    parameter_count = 0U;
    pointer_depth = 0U;
    is_variadic = false;
    (void)memset(parameter_types, 0, sizeof(parameter_types));

    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function pointer typedef")) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        pointer_depth += 1U;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (pointer_depth == 0U) {
        minic_parser_error(parser, "function pointer typedef requires '*'");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected function pointer typedef name");
        return false;
    }
    *name_span = parser->current.span;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function pointer typedef name") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function pointer parameters") ||
        !minic_parser_parse_parameter_list(
            parser, NULL, parameter_types, &parameter_count, false, &is_variadic) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function pointer parameters")) {
        return false;
    }
    if (is_variadic) {
        minic_parser_error(parser, "variadic function pointer typedefs are not supported yet");
        return false;
    }
    if (!minic_c0_program_add_function_type(
            parser->program, return_type, parameter_types, parameter_count, &function_type)) {
        minic_parser_error(parser, "cannot build function pointer typedef type");
        return false;
    }
    while (pointer_depth > 0U) {
        if (!minic_type_pointer_to(function_type, &function_type)) {
            minic_parser_error(parser, "function pointer typedef depth is unsupported");
            return false;
        }
        pointer_depth -= 1U;
    }
    *aliased_type = function_type;
    return true;
}

bool minic_parser_parse_typedef(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType aliased_type;
    MinicTypeAliasId alias_id;
    size_t bounds[8];
    size_t bound_count;
    bool is_function_pointer;
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
        MinicType base_type;

        if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
            !minic_parser_parse_pointer_declarator(parser, base_type, &aliased_type)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            if (!parse_function_pointer_typedef(parser, aliased_type, &name_span, &aliased_type)) {
                return false;
            }
            is_function_pointer = true;
        }
    }
    if (minic_type_is_void(aliased_type)) {
        minic_parser_error(parser, "typedef cannot name bare void");
        return false;
    }
    if (!is_function_pointer) {
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected typedef name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (minic_parser_find_type_alias(parser, name_span) != MINIC_TYPE_ALIAS_INVALID) {
        minic_parser_error(parser, "duplicate typedef name");
        return false;
    }

    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (is_function_pointer) {
            minic_parser_error(parser, "function pointer typedef arrays are not supported yet");
            return false;
        }
        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
            minic_parser_error(parser, "at most eight array dimensions are supported");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
            return false;
        }
        bound_count += 1U;
    }

    while (bound_count > 0U) {
        bound_count -= 1U;
        if (!minic_c0_program_add_array_type(
                parser->program, aliased_type, bounds[bound_count], &aliased_type)) {
            minic_parser_error(parser, "out of memory while building typedef array type");
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(parser, "expected ';' after typedef");
        return false;
    }
    if (!minic_c0_program_add_type_alias(parser->program,
                                         parser->source + name_span.begin.offset,
                                         minic_parser_span_length(name_span),
                                         aliased_type,
                                         &alias_id)) {
        minic_parser_error(parser, "out of memory while adding typedef");
        return false;
    }
    return minic_parser_advance(parser);
}
