#ifndef MINIC_FRONTEND_PARSER_STATEMENT_CONTROL_H
#define MINIC_FRONTEND_PARSER_STATEMENT_CONTROL_H

#include "frontend/parser_internal.h"

/* Internal token-to-AST statement control-flow owner. Generic statements
 * own compound scope parsing, loops/switches only request the block shape. */
bool minic_parser_parse_compound_statement(MinicParser *parser);
bool minic_parser_control_parse_if(MinicParser *parser);
bool minic_parser_control_parse_continue(MinicParser *parser);
bool minic_parser_control_parse_while(MinicParser *parser);
bool minic_parser_control_parse_do_while(MinicParser *parser);
bool minic_parser_control_parse_switch(MinicParser *parser);
bool minic_parser_control_parse_case(MinicParser *parser);
bool minic_parser_control_parse_default(MinicParser *parser);
bool minic_parser_control_parse_for(MinicParser *parser);
bool minic_parser_control_parse_break(MinicParser *parser);

#endif
