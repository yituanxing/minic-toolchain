#ifndef MINIC_FRONTEND_PARSER_BUILTIN_MEMORY_H
#define MINIC_FRONTEND_PARSER_BUILTIN_MEMORY_H

#include "frontend/parser_internal.h"

/* Decode a recognized GNU memory builtin into the canonical C library call
 * expression. The caller owns primary dispatch and subsequent postfix parse. */
bool minic_parser_parse_builtin_memory(MinicParser *parser,
                                       MinicExpressionId *expression_id);

#endif
