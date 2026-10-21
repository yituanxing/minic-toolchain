#ifndef MINIC_FRONTEND_PARSER_GNU_INLINE_ASM_H
#define MINIC_FRONTEND_PARSER_GNU_INLINE_ASM_H
#include "frontend/parser_internal.h"

/* GNU extended inline asm statement parsing: operands, clobbers and labels.
 * The statement dispatcher only identifies and delegates the grammar. */
bool minic_parser_gnu_inline_asm_starts(const MinicParser *parser);
bool minic_parser_gnu_inline_asm_statement(MinicParser *parser);
#endif
