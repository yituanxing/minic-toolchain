#ifndef MINIC_FRONTEND_PARSER_LOCAL_DECLARATIONS_H
#define MINIC_FRONTEND_PARSER_LOCAL_DECLARATIONS_H
#include "frontend/parser_internal.h"

/* Block-scope object/function declarations and storage-lifetime grammar.
 * Expression, CFG and statement dispatch remain in parser_statement.c. */
bool minic_parser_local_decl_starts_auto_type(const MinicParser *parser);
bool minic_parser_local_decl_auto_type(MinicParser *parser);
bool minic_parser_local_decl_ordinary(MinicParser *parser);
bool minic_parser_local_decl_extern(MinicParser *parser);
bool minic_parser_local_decl_static(MinicParser *parser);
#endif
