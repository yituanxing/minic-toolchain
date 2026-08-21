#ifndef MINIC_FRONTEND_DECLARATOR_H
#define MINIC_FRONTEND_DECLARATOR_H

#include <stdbool.h>
#include <stddef.h>

#define MINIC_ARRAY_DECLARATOR_MAX_DIMENSIONS 8U

typedef struct MinicArrayDeclaratorSyntax {
    size_t bounds[MINIC_ARRAY_DECLARATOR_MAX_DIMENSIONS];
    size_t dimension_count;
    unsigned int zero_length_mask;
    bool outermost_incomplete;
} MinicArrayDeclaratorSyntax;

struct MinicParser;

/*
 * Parse array declarator syntax without committing a semantic type to Program.
 * The caller decides when the transient syntax should be materialized by Sema.
 */
bool minic_parser_parse_array_declarator_syntax(struct MinicParser *parser,
                                                bool allow_incomplete_outermost,
                                                MinicArrayDeclaratorSyntax *declarator);

#endif
