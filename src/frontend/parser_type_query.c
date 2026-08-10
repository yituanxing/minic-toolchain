#include "frontend/parser_internal.h"

#include <stdint.h>

bool minic_parser_parse_alignof_type_value(MinicParser *parser,
                                           int64_t *value,
                                           MinicSourceSpan *span) {
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicType measured_type;
    size_t measured_size;
    size_t measured_alignment;

    if (parser == NULL || value == NULL || parser->current.kind != MINIC_TOKEN_KW_ALIGNOF) {
        if (parser != NULL) {
            minic_parser_error(parser, "expected alignof type query");
        }
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after alignof") ||
        !minic_parser_parse_type_name(parser, &measured_type)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after alignof type");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_data_layout_type(parser->data_layout,
                                parser->program,
                                measured_type,
                                &measured_size,
                                &measured_alignment) ||
        measured_alignment > (size_t)INT64_MAX) {
        minic_parser_error(parser, "alignof requires a complete object type");
        return false;
    }
    (void)measured_size;
    *value = (int64_t)measured_alignment;
    if (span != NULL) {
        span->begin = begin;
        span->end = end;
    }
    return minic_parser_advance(parser);
}
