#include "frontend/parser_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void minic_parser_error(MinicParser *parser, const char *format, ...)
{
    va_list arguments;

    if (parser->diagnostic == NULL) {
        return;
    }
    parser->diagnostic->path = parser->path;
    parser->diagnostic->line = parser->current.span.begin.line;
    parser->diagnostic->column = parser->current.span.begin.column;
    va_start(arguments, format);
    (void)vsnprintf(
        parser->diagnostic->message,
        sizeof(parser->diagnostic->message),
        format,
        arguments);
    va_end(arguments);
}

bool minic_parser_advance(MinicParser *parser)
{
    return minic_lexer_next(&parser->lexer, &parser->current, parser->diagnostic);
}

bool minic_parser_expect(
    MinicParser *parser,
    MinicTokenKind kind,
    const char *message)
{
    if (parser->current.kind != kind) {
        minic_parser_error(parser, "%s", message);
        return false;
    }
    return minic_parser_advance(parser);
}

size_t minic_parser_span_length(MinicSourceSpan span)
{
    return span.end.offset - span.begin.offset;
}

bool minic_parser_span_equals(
    const MinicParser *parser,
    MinicSourceSpan left,
    MinicSourceSpan right)
{
    size_t left_length;
    size_t right_length;

    left_length = minic_parser_span_length(left);
    right_length = minic_parser_span_length(right);
    return left_length == right_length &&
           memcmp(
               parser->source + left.begin.offset,
               parser->source + right.begin.offset,
               left_length) == 0;
}

bool minic_parser_add_expression(
    MinicParser *parser,
    const MinicExpression *expression,
    MinicExpressionId *expression_id)
{
    if (minic_c0_program_add_expression(
            parser->program,
            expression,
            expression_id)) {
        return true;
    }
    minic_parser_error(parser, "out of memory while building expression tree");
    return false;
}

bool minic_parser_add_statement(
    MinicParser *parser,
    const MinicStatement *statement)
{
    MinicStatementId statement_id;

    if (minic_c0_program_add_statement(
            parser->program,
            statement,
            &statement_id) &&
        minic_c0_block_add_statement(
            parser->program,
            parser->current_block,
            statement_id)) {
        return true;
    }
    minic_parser_error(parser, "out of memory while building statement list");
    return false;
}

MinicLocalId minic_parser_find_local(
    const MinicParser *parser,
    MinicSourceSpan name_span)
{
    size_t index;

    for (index = parser->local_begin;
         index < parser->program->local_count;
         ++index) {
        if (minic_parser_span_equals(
                parser,
                name_span,
                parser->program->locals[index].name_span)) {
            return index;
        }
    }
    return MINIC_LOCAL_INVALID;
}

MinicFunctionId minic_parser_find_function(
    const MinicParser *parser,
    MinicSourceSpan name_span)
{
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->function_count; ++index) {
        const MinicFunction *function;

        function = minic_c0_program_function(parser->program, index);
        if (function != NULL && function->name_length == name_length &&
            memcmp(
                function->name,
                parser->source + name_span.begin.offset,
                name_length) == 0) {
            return index;
        }
    }
    return MINIC_FUNCTION_INVALID;
}
