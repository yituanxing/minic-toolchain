#include "frontend/parser_internal.h"

#include <string.h>

bool minic_parser_parse_parenthesized_function_declarator(
    MinicParser *parser,
    bool require_name,
    bool require_pointer,
    MinicParsedFunctionDeclarator *declarator) {
    if (parser == NULL || declarator == NULL) {
        return false;
    }

    (void)memset(declarator, 0, sizeof(*declarator));
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function declarator")) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        declarator->pointer_depth += 1U;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (require_pointer && declarator->pointer_depth == 0U) {
        minic_parser_error(parser, "function declarator requires pointer indirection");
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        declarator->name_span = parser->current.span;
        declarator->has_name = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (require_name) {
        minic_parser_error(parser, "expected function declarator name");
        return false;
    }

    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function declarator") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function parameter list") ||
        !minic_parser_parse_parameter_list(parser,
                                           NULL,
                                           declarator->parameter_types,
                                           &declarator->parameter_count,
                                           false,
                                           &declarator->is_variadic) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function parameter list")) {
        return false;
    }
    return true;
}

bool minic_parser_parse_array_declarator_suffix(MinicParser *parser,
                                                MinicType element_type,
                                                bool allow_incomplete_outermost,
                                                MinicType *declarator_type,
                                                bool *is_array) {
    size_t bounds[8];
    size_t bound_count;
    size_t dimension;
    bool outermost_incomplete;
    MinicType type;

    if (parser == NULL || declarator_type == NULL || is_array == NULL) {
        return false;
    }
    *declarator_type = element_type;
    *is_array = false;
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return true;
    }

    bound_count = 0U;
    outermost_incomplete = false;
    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
            minic_parser_error(parser, "array declarator supports at most eight dimensions");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
            if (!allow_incomplete_outermost || bound_count != 0U) {
                minic_parser_error(parser, "only the outermost array dimension may be incomplete");
                return false;
            }
            outermost_incomplete = true;
            bounds[bound_count] = 0U;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (!minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
            return false;
        }
        bound_count += 1U;
    }

    type = element_type;
    dimension = bound_count;
    while (dimension > 0U) {
        dimension -= 1U;
        if (dimension == 0U && outermost_incomplete) {
            if (!minic_c0_program_add_incomplete_array_type(parser->program, type, &type)) {
                minic_parser_error(parser, "cannot build incomplete array declarator type");
                return false;
            }
        } else if (!minic_c0_program_add_array_type(
                       parser->program, type, bounds[dimension], &type)) {
            minic_parser_error(parser, "cannot build array declarator type");
            return false;
        }
    }
    *declarator_type = type;
    *is_array = true;
    return true;
}

bool minic_parser_build_function_declarator_type(MinicParser *parser,
                                                 MinicType return_type,
                                                 const MinicParsedFunctionDeclarator *declarator,
                                                 MinicType *declarator_type) {
    MinicType function_type;
    size_t pointer_depth;

    if (parser == NULL || declarator == NULL || declarator_type == NULL ||
        declarator->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
        return false;
    }
    if (!minic_c0_program_add_function_type(parser->program,
                                            return_type,
                                            declarator->parameter_types,
                                            declarator->parameter_count,
                                            &function_type)) {
        return false;
    }

    pointer_depth = declarator->pointer_depth;
    while (pointer_depth > 0U) {
        if (!minic_type_pointer_to(function_type, &function_type)) {
            return false;
        }
        pointer_depth -= 1U;
    }
    *declarator_type = function_type;
    return true;
}
