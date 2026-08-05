#include "frontend/parser_internal.h"

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool minic_parser_grow_array(
    void **storage,
    size_t *capacity,
    size_t element_size)
{
    size_t new_capacity;
    void *new_storage;

    new_capacity = *capacity == 0U ? 8U : *capacity * 2U;
    if (new_capacity < *capacity ||
        element_size != 0U && new_capacity > SIZE_MAX / element_size) {
        return false;
    }
    new_storage = realloc(*storage, new_capacity * element_size);
    if (new_storage == NULL) {
        return false;
    }
    *storage = new_storage;
    *capacity = new_capacity;
    return true;
}

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

bool minic_parser_begin_scope(MinicParser *parser)
{
    if (parser->scope_count == parser->scope_capacity &&
        !minic_parser_grow_array(
            (void **)&parser->scope_binding_begins,
            &parser->scope_capacity,
            sizeof(*parser->scope_binding_begins))) {
        minic_parser_error(parser, "out of memory while entering scope");
        return false;
    }
    parser->scope_binding_begins[parser->scope_count] =
        parser->local_binding_count;
    parser->scope_count += 1U;
    return true;
}

void minic_parser_end_scope(MinicParser *parser)
{
    if (parser->scope_count == 0U) {
        return;
    }
    parser->scope_count -= 1U;
    parser->local_binding_count =
        parser->scope_binding_begins[parser->scope_count];
}

bool minic_parser_bind_local(
    MinicParser *parser,
    MinicSourceSpan name_span,
    MinicLocalId local_id)
{
    MinicParserLocalBinding *binding;

    if (parser->scope_count == 0U) {
        minic_parser_error(parser, "internal error: local binding outside scope");
        return false;
    }
    if (parser->local_binding_count == parser->local_binding_capacity &&
        !minic_parser_grow_array(
            (void **)&parser->local_bindings,
            &parser->local_binding_capacity,
            sizeof(*parser->local_bindings))) {
        minic_parser_error(parser, "out of memory while binding local name");
        return false;
    }
    binding = &parser->local_bindings[parser->local_binding_count];
    binding->name_span = name_span;
    binding->local_id = local_id;
    parser->local_binding_count += 1U;
    return true;
}

MinicLocalId minic_parser_find_local_in_current_scope(
    const MinicParser *parser,
    MinicSourceSpan name_span)
{
    size_t scope_begin;
    size_t index;

    if (parser->scope_count == 0U) {
        return MINIC_LOCAL_INVALID;
    }
    scope_begin = parser->scope_binding_begins[parser->scope_count - 1U];
    for (index = parser->local_binding_count; index > scope_begin; --index) {
        const MinicParserLocalBinding *binding;

        binding = &parser->local_bindings[index - 1U];
        if (minic_parser_span_equals(parser, name_span, binding->name_span)) {
            return binding->local_id;
        }
    }
    return MINIC_LOCAL_INVALID;
}

void minic_parser_destroy_scopes(MinicParser *parser)
{
    free(parser->local_bindings);
    free(parser->scope_binding_begins);
    parser->local_bindings = NULL;
    parser->local_binding_count = 0U;
    parser->local_binding_capacity = 0U;
    parser->scope_binding_begins = NULL;
    parser->scope_count = 0U;
    parser->scope_capacity = 0U;
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
