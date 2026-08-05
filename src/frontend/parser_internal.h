#ifndef MINIC_FRONTEND_PARSER_INTERNAL_H
#define MINIC_FRONTEND_PARSER_INTERNAL_H

#include "frontend/ast.h"
#include "frontend/lexer.h"
#include "frontend/token.h"
#include "minic/compiler.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct MinicParserLocalBinding {
    MinicSourceSpan name_span;
    MinicLocalId local_id;
} MinicParserLocalBinding;

typedef struct MinicParser {
    const char *path;
    const char *source;
    MinicLexer lexer;
    MinicToken current;
    MinicDiagnostic *diagnostic;
    MinicC0Program *program;
    MinicBlockId current_block;
    MinicFunctionId current_function;
    size_t local_begin;

    MinicParserLocalBinding *local_bindings;
    size_t local_binding_count;
    size_t local_binding_capacity;

    size_t *scope_binding_begins;
    size_t scope_count;
    size_t scope_capacity;
} MinicParser;

void minic_parser_error(MinicParser *parser, const char *format, ...);
bool minic_parser_advance(MinicParser *parser);
bool minic_parser_expect(
    MinicParser *parser,
    MinicTokenKind kind,
    const char *message);
bool minic_parser_parse_integer_value(
    MinicParser *parser,
    int *value);
bool minic_parser_parse_type_name(
    MinicParser *parser,
    MinicType *type);
bool minic_parser_parse_fixed_array_bound(
    MinicParser *parser,
    size_t *element_count);
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

bool minic_parser_begin_scope(MinicParser *parser);
void minic_parser_end_scope(MinicParser *parser);
bool minic_parser_bind_local(
    MinicParser *parser,
    MinicSourceSpan name_span,
    MinicLocalId local_id);
MinicLocalId minic_parser_find_local_in_current_scope(
    const MinicParser *parser,
    MinicSourceSpan name_span);
void minic_parser_destroy_scopes(MinicParser *parser);

MinicLocalId minic_parser_find_local(
    const MinicParser *parser,
    MinicSourceSpan name_span);
MinicFunctionId minic_parser_find_function(
    const MinicParser *parser,
    MinicSourceSpan name_span);
MinicRecordId minic_parser_find_record(
    const MinicParser *parser,
    MinicSourceSpan name_span);
MinicTypeAliasId minic_parser_find_type_alias(
    const MinicParser *parser,
    MinicSourceSpan name_span);

bool minic_parser_parse_record_definition(MinicParser *parser);
bool minic_parser_parse_typedef(MinicParser *parser);
bool minic_parser_parse_static_global(MinicParser *parser);
bool minic_parser_parse_expression(
    MinicParser *parser,
    MinicExpressionId *expression_id,
    unsigned int minimum_precedence);
bool minic_parser_add_default_return(MinicParser *parser);
bool minic_parser_parse_statement(
    MinicParser *parser,
    bool allow_declaration);

#endif
