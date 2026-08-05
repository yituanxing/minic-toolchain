#ifndef MINIC_FRONTEND_PARSER_INTERNAL_H
#define MINIC_FRONTEND_PARSER_INTERNAL_H

#include "frontend/ast.h"
#include "frontend/lexer.h"
#include "frontend/token.h"
#include "minic/compiler.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct MinicParser {
    const char *path;
    const char *source;
    MinicLexer lexer;
    MinicToken current;
    MinicDiagnostic *diagnostic;
    MinicC0Program *program;
    MinicBlockId current_block;
    size_t local_begin;
} MinicParser;

void minic_parser_error(MinicParser *parser, const char *format, ...);
bool minic_parser_advance(MinicParser *parser);
bool minic_parser_expect(
    MinicParser *parser,
    MinicTokenKind kind,
    const char *message);
size_t minic_parser_span_length(MinicSourceSpan span);
bool minic_parser_span_equals(
    const MinicParser *parser,
    MinicSourceSpan left,
    MinicSourceSpan right);
bool minic_parser_add_expression(
    MinicParser *parser,
    const MinicExpression *expression,
    MinicExpressionId *expression_id);
bool minic_parser_add_statement(
    MinicParser *parser,
    const MinicStatement *statement);
MinicLocalId minic_parser_find_local(
    const MinicParser *parser,
    MinicSourceSpan name_span);
MinicFunctionId minic_parser_find_function(
    const MinicParser *parser,
    MinicSourceSpan name_span);

bool minic_parser_parse_expression(
    MinicParser *parser,
    MinicExpressionId *expression_id,
    unsigned int minimum_precedence);
bool minic_parser_add_default_return(MinicParser *parser);
bool minic_parser_parse_statement(
    MinicParser *parser,
    bool allow_declaration);

#endif
