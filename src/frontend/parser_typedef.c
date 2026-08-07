#include "frontend/parser_internal.h"

#include <string.h>

MinicTypeAliasId minic_parser_find_type_alias(const MinicParser *parser,
                                              MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

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

static bool typedef_starts_record_definition(MinicParser *parser, bool *is_definition) {
    MinicParser probe;

    if (parser == NULL || is_definition == NULL) {
        return false;
    }
    *is_definition = false;
    if (parser->current.kind != MINIC_TOKEN_KW_STRUCT) {
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

bool minic_parser_parse_typedef(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType aliased_type;
    MinicTypeAliasId alias_id;
    size_t bounds[8];
    size_t bound_count;
    bool is_record_definition;

    bound_count = 0U;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_TYPEDEF, "expected keyword 'typedef'") ||
        !typedef_starts_record_definition(parser, &is_record_definition)) {
        return false;
    }
    if (is_record_definition) {
        if (!minic_parser_parse_record_definition_specifier(parser, &aliased_type)) {
            return false;
        }
    } else if (!minic_parser_parse_type_name(parser, &aliased_type)) {
        return false;
    }
    if (minic_type_is_void(aliased_type)) {
        minic_parser_error(parser, "typedef cannot name bare void");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected typedef name");
        return false;
    }

    name_span = parser->current.span;
    if (minic_parser_find_type_alias(parser, name_span) != MINIC_TYPE_ALIAS_INVALID) {
        minic_parser_error(parser, "duplicate typedef name");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
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
