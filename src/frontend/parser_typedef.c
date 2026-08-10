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
    bool is_function_pointer;

    bound_count = 0U;
    is_function_pointer = false;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'")) {
        return false;
    }
    {
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
