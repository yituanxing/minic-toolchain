#ifndef MINIC_FRONTEND_PARSER_LABELS_H
#define MINIC_FRONTEND_PARSER_LABELS_H

#include "frontend/parser_internal.h"

/* Private source-language label/goto grammar and forward-reference owner.
 * The general statement dispatcher keeps only token routing. */
bool minic_parser_label_context_ensure(MinicParser *parser);
bool minic_parser_current_identifier_is_goto(const MinicParser *parser);
bool minic_parser_current_identifier_is_local_label_keyword(const MinicParser *parser);
bool minic_parser_parse_gnu_local_label_declaration(MinicParser *parser);
bool minic_parser_parse_goto(MinicParser *parser);
bool minic_parser_identifier_starts_label(MinicParser *parser);
bool minic_parser_parse_label(MinicParser *parser, bool allow_declaration);

#endif
