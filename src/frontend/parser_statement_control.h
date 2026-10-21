#ifndef MINIC_FRONTEND_PARSER_STATEMENT_CONTROL_H
#define MINIC_FRONTEND_PARSER_STATEMENT_CONTROL_H
#include "frontend/parser_internal.h"

/* Private label/asm-goto and GNU extended asm grammar owners. */
bool minic_parser_labels_ensure_context(MinicParser *parser);
bool minic_parser_labels_identifier_equals(const MinicParser *parser, MinicSourceSpan span, const char *text, size_t text_length);
bool minic_parser_labels_is_goto(const MinicParser *parser);
bool minic_parser_labels_is_local_keyword(const MinicParser *parser);
bool minic_parser_labels_parse_local_declaration(MinicParser *parser);
bool minic_parser_labels_parse_goto(MinicParser *parser);
bool minic_parser_labels_starts_label(MinicParser *parser);
bool minic_parser_labels_parse_label(MinicParser *parser, bool allow_declaration);
bool minic_parser_inline_asm_starts(const MinicParser *parser);
bool minic_parser_inline_asm_parse_statement(MinicParser *parser);

#endif
