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

bool minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return false;
    }
    for (index = parser->enum_tag_count; index > 0U; --index) {
        if (minic_parser_span_equals(parser, name_span, parser->enum_tags[index - 1U].name_span)) {
            return true;
        }
    }
    return false;
}

bool minic_parser_bind_enum_tag(MinicParser *parser, MinicSourceSpan name_span) {
    MinicParserEnumTag *resized;
    size_t new_capacity;

    if (parser == NULL || minic_parser_find_enum_tag(parser, name_span)) {
        if (parser != NULL) {
            minic_parser_error(parser, "duplicate enum tag");
        }
        return false;
    }
    if (parser->enum_tag_count == parser->enum_tag_capacity) {
        new_capacity = parser->enum_tag_capacity == 0U ? 8U : parser->enum_tag_capacity * 2U;
        if (new_capacity < parser->enum_tag_capacity ||
            new_capacity > SIZE_MAX / sizeof(*parser->enum_tags)) {
            minic_parser_error(parser, "too many enum tags");
            return false;
        }
        resized = (MinicParserEnumTag *)realloc(parser->enum_tags,
                                                new_capacity * sizeof(*parser->enum_tags));
        if (resized == NULL) {
            minic_parser_error(parser, "out of memory while binding enum tag");
            return false;
        }
        parser->enum_tags = resized;
        parser->enum_tag_capacity = new_capacity;
    }
    parser->enum_tags[parser->enum_tag_count].name_span = name_span;
    parser->enum_tag_count += 1U;
    return true;
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
    free(parser->enum_tags);
    parser->enum_tags = NULL;
    parser->enum_tag_count = 0U;
    parser->enum_tag_capacity = 0U;
}

static bool parse_enum_integer_value(MinicParser *parser, int *value) {
    int64_t parsed;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &parsed)) {
        return false;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        minic_parser_error(parser, "enum constant expression is out of int range");
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool parse_enum_definition_specifier(MinicParser *parser) {
    MinicSourceSpan tag_span;
    int next_value;
    bool has_tag;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_ENUM, "expected keyword 'enum'")) {
        return false;
    }
    (void)memset(&tag_span, 0, sizeof(tag_span));
    has_tag = false;
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        tag_span = parser->current.span;
        has_tag = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' after enum specifier") ||
        (has_tag && !minic_parser_bind_enum_tag(parser, tag_span))) {
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
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || aliased_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(parser, true, false, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer typedefs are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, aliased_type)) {
        minic_parser_error(parser, "cannot build function pointer typedef type");
        return false;
    }
    *name_span = declarator.name_span;
    return true;
}

static bool typedef_token_text_equals(const MinicParser *parser, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(text) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, text, length) == 0;
}

static bool parse_redundant_typedef_alignment(MinicParser *parser, MinicType aliased_type) {
    int64_t alignment;

    if (!typedef_token_text_equals(parser, "__attribute__") &&
        !typedef_token_text_equals(parser, "__attribute")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after typedef __attribute__") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '((' in typedef __attribute__")) {
        return false;
    }
    if (!minic_parser_current_attribute_is(
            parser, MINIC_ATTRIBUTE_ALIGNED, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "unsupported GNU typedef attribute");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after typedef aligned") ||
        !minic_parser_parse_integer_value64(parser, &alignment) || alignment <= 0 ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after typedef alignment") ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in typedef attribute") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected second ')' in typedef attribute")) {
        if (parser != NULL && parser->diagnostic != NULL &&
            parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "typedef alignment must be a positive integer");
        }
        return false;
    }

    /* This discovery bridge is deliberately narrower than GCC's full attributed-type
       semantics.  Linux first reaches aligned(16) on GNU __int128, whose natural RV64
       alignment is already 16.  Accept that semantics-preserving spelling, but reject
       any alignment that would alter the type.  Permanent support belongs in the
       Declarator/AttributeSet + Target DataLayout architecture rather than silently
       discarding an ABI-affecting attribute here. */
    if (!minic_type_is_int128_integer(aliased_type) || alignment != 16) {
        minic_parser_error(parser,
                           "non-redundant GNU typedef alignment requires attributed-type support");
        return false;
    }
    return true;
}

bool minic_parser_parse_typedef(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType aliased_type;
    MinicTypeAliasId alias_id;
    size_t bounds[8];
    size_t bound_count;
    bool is_enum_definition;
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
    if (!parse_redundant_typedef_alignment(parser, aliased_type)) {
        return false;
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
