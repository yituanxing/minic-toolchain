#include "frontend/parser_internal.h"

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool minic_parser_grow_array(void **storage, size_t *capacity, size_t element_size) {
    size_t new_capacity;
    void *new_storage;

    new_capacity = *capacity == 0U ? 8U : *capacity * 2U;
    if (new_capacity < *capacity ||
        (element_size != 0U && new_capacity > SIZE_MAX / element_size)) {
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

void minic_parser_error(MinicParser *parser, const char *format, ...) {
    va_list arguments;

    if (parser->diagnostic == NULL) {
        return;
    }
    parser->diagnostic->path = parser->path;
    parser->diagnostic->line = parser->current.span.begin.line;
    parser->diagnostic->column = parser->current.span.begin.column;
    va_start(arguments, format);
    (void)vsnprintf(
        parser->diagnostic->message, sizeof(parser->diagnostic->message), format, arguments);
    va_end(arguments);
}

bool minic_parser_advance(MinicParser *parser) {
    return minic_lexer_next(&parser->lexer, &parser->current, parser->diagnostic);
}

bool minic_parser_expect(MinicParser *parser, MinicTokenKind kind, const char *message) {
    if (parser->current.kind != kind) {
        minic_parser_error(parser, "%s", message);
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_array_bound_additive(MinicParser *parser, int64_t *value);

static bool parse_array_bound_primary(MinicParser *parser, int64_t *value) {
    if (parser == NULL || value == NULL) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return minic_parser_parse_integer_value64(parser, value);
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) || !parse_array_bound_additive(parser, value) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in array bound")) {
            return false;
        }
        return true;
    }
    minic_parser_error(parser, "expected integer constant expression in array bound");
    return false;
}

static bool parse_array_bound_unary(MinicParser *parser, int64_t *value) {
    MinicTokenKind operator_kind;
    int64_t operand;

    if (parser == NULL || value == NULL) {
        return false;
    }
    operator_kind = parser->current.kind;
    if (operator_kind != MINIC_TOKEN_PLUS && operator_kind != MINIC_TOKEN_MINUS) {
        return parse_array_bound_primary(parser, value);
    }
    if (!minic_parser_advance(parser) || !parse_array_bound_unary(parser, &operand)) {
        return false;
    }
    if (operator_kind == MINIC_TOKEN_MINUS) {
        if (operand == INT64_MIN) {
            minic_parser_error(parser, "array bound constant expression overflow");
            return false;
        }
        operand = -operand;
    }
    *value = operand;
    return true;
}

static bool parse_array_bound_multiplicative(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_unary(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR || parser->current.kind == MINIC_TOKEN_SLASH ||
           parser->current.kind == MINIC_TOKEN_PERCENT) {
        MinicTokenKind operator_kind;
        int64_t right;

        operator_kind = parser->current.kind;
        if (!minic_parser_advance(parser) || !parse_array_bound_unary(parser, &right)) {
            return false;
        }
        if (operator_kind == MINIC_TOKEN_STAR) {
            if (left != 0 &&
                ((left == -1 && right == INT64_MIN) || (right == -1 && left == INT64_MIN) ||
                 (left > 0 && right > 0 && left > INT64_MAX / right) ||
                 (left > 0 && right < 0 && right < INT64_MIN / left) ||
                 (left < 0 && right > 0 && left < INT64_MIN / right) ||
                 (left < 0 && right < 0 && left < INT64_MAX / right))) {
                minic_parser_error(parser, "array bound constant expression overflow");
                return false;
            }
            left *= right;
        } else {
            if (right == 0) {
                minic_parser_error(parser, "division by zero in array bound constant expression");
                return false;
            }
            if (left == INT64_MIN && right == -1) {
                minic_parser_error(parser, "array bound constant expression overflow");
                return false;
            }
            left = operator_kind == MINIC_TOKEN_SLASH ? left / right : left % right;
        }
    }
    *value = left;
    return true;
}

static bool parse_array_bound_additive(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_multiplicative(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_PLUS || parser->current.kind == MINIC_TOKEN_MINUS) {
        MinicTokenKind operator_kind;
        int64_t right;

        operator_kind = parser->current.kind;
        if (!minic_parser_advance(parser) || !parse_array_bound_multiplicative(parser, &right)) {
            return false;
        }
        if (operator_kind == MINIC_TOKEN_PLUS) {
            if ((right > 0 && left > INT64_MAX - right) ||
                (right < 0 && left < INT64_MIN - right)) {
                minic_parser_error(parser, "array bound constant expression overflow");
                return false;
            }
            left += right;
        } else {
            if ((right < 0 && left > INT64_MAX + right) ||
                (right > 0 && left < INT64_MIN + right)) {
                minic_parser_error(parser, "array bound constant expression overflow");
                return false;
            }
            left -= right;
        }
    }
    *value = left;
    return true;
}

bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (element_count == NULL || !parse_array_bound_additive(parser, &value)) {
        return false;
    }
    if (value <= 0) {
        minic_parser_error(parser, "array bound must be greater than zero");
        return false;
    }
    if ((uint64_t)value > (uint64_t)SIZE_MAX) {
        minic_parser_error(parser, "array bound exceeds target object range");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    *element_count = (size_t)value;
    return true;
}

size_t minic_parser_span_length(MinicSourceSpan span) {
    return span.end.offset - span.begin.offset;
}

bool minic_parser_span_equals(const MinicParser *parser,
                              MinicSourceSpan left,
                              MinicSourceSpan right) {
    size_t left_length;
    size_t right_length;

    left_length = minic_parser_span_length(left);
    right_length = minic_parser_span_length(right);
    return left_length == right_length && memcmp(parser->source + left.begin.offset,
                                                 parser->source + right.begin.offset,
                                                 left_length) == 0;
}

bool minic_parser_add_expression(MinicParser *parser,
                                 const MinicExpression *expression,
                                 MinicExpressionId *expression_id) {
    if (minic_c0_program_add_expression(parser->program, expression, expression_id)) {
        return true;
    }
    minic_parser_error(parser, "out of memory while building expression tree");
    return false;
}

bool minic_parser_add_statement(MinicParser *parser, const MinicStatement *statement) {
    MinicStatementId statement_id;

    if (minic_c0_program_add_statement(parser->program, statement, &statement_id) &&
        minic_c0_block_add_statement(parser->program, parser->current_block, statement_id)) {
        return true;
    }
    minic_parser_error(parser, "out of memory while building statement list");
    return false;
}

bool minic_parser_begin_scope(MinicParser *parser) {
    if (parser->scope_count == parser->scope_capacity &&
        !minic_parser_grow_array((void **)&parser->scope_binding_begins,
                                 &parser->scope_capacity,
                                 sizeof(*parser->scope_binding_begins))) {
        minic_parser_error(parser, "out of memory while entering scope");
        return false;
    }
    parser->scope_binding_begins[parser->scope_count] = parser->local_binding_count;
    parser->scope_count += 1U;
    return true;
}

void minic_parser_end_scope(MinicParser *parser) {
    if (parser->scope_count == 0U) {
        return;
    }
    parser->scope_count -= 1U;
    parser->local_binding_count = parser->scope_binding_begins[parser->scope_count];
}

static bool minic_parser_bind_scoped_object(MinicParser *parser,
                                            MinicSourceSpan name_span,
                                            MinicLocalId local_id,
                                            MinicGlobalObjectId global_object_id) {
    MinicParserLocalBinding *binding;

    if (parser->scope_count == 0U ||
        ((local_id == MINIC_LOCAL_INVALID) == (global_object_id == MINIC_GLOBAL_OBJECT_INVALID))) {
        minic_parser_error(parser, "internal error: invalid scoped object binding");
        return false;
    }
    if (parser->local_binding_count == parser->local_binding_capacity &&
        !minic_parser_grow_array((void **)&parser->local_bindings,
                                 &parser->local_binding_capacity,
                                 sizeof(*parser->local_bindings))) {
        minic_parser_error(parser, "out of memory while binding local name");
        return false;
    }
    binding = &parser->local_bindings[parser->local_binding_count];
    binding->name_span = name_span;
    binding->local_id = local_id;
    binding->global_object_id = global_object_id;
    parser->local_binding_count += 1U;
    return true;
}

bool minic_parser_bind_local(MinicParser *parser,
                             MinicSourceSpan name_span,
                             MinicLocalId local_id) {
    return minic_parser_bind_scoped_object(
        parser, name_span, local_id, MINIC_GLOBAL_OBJECT_INVALID);
}

bool minic_parser_bind_static_local(MinicParser *parser,
                                    MinicSourceSpan name_span,
                                    MinicGlobalObjectId global_object_id) {
    return minic_parser_bind_scoped_object(
        parser, name_span, MINIC_LOCAL_INVALID, global_object_id);
}

bool minic_parser_name_bound_in_current_scope(const MinicParser *parser,
                                              MinicSourceSpan name_span) {
    size_t scope_begin;
    size_t index;

    if (parser->scope_count == 0U) {
        return false;
    }
    scope_begin = parser->scope_binding_begins[parser->scope_count - 1U];
    for (index = parser->local_binding_count; index > scope_begin; --index) {
        const MinicParserLocalBinding *binding;

        binding = &parser->local_bindings[index - 1U];
        if (minic_parser_span_equals(parser, name_span, binding->name_span)) {
            return true;
        }
    }
    return false;
}

MinicLocalId minic_parser_find_local_in_current_scope(const MinicParser *parser,
                                                      MinicSourceSpan name_span) {
    size_t scope_begin;
    size_t index;

    if (parser->scope_count == 0U) {
        return MINIC_LOCAL_INVALID;
    }
    scope_begin = parser->scope_binding_begins[parser->scope_count - 1U];
    for (index = parser->local_binding_count; index > scope_begin; --index) {
        const MinicParserLocalBinding *binding;

        binding = &parser->local_bindings[index - 1U];
        if (binding->local_id != MINIC_LOCAL_INVALID &&
            minic_parser_span_equals(parser, name_span, binding->name_span)) {
            return binding->local_id;
        }
    }
    return MINIC_LOCAL_INVALID;
}

void minic_parser_destroy_scopes(MinicParser *parser) {
    free(parser->local_bindings);
    free(parser->scope_binding_begins);
    parser->local_bindings = NULL;
    parser->local_binding_count = 0U;
    parser->local_binding_capacity = 0U;
    parser->scope_binding_begins = NULL;
    parser->scope_count = 0U;
    parser->scope_capacity = 0U;
}

MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    for (index = parser->local_binding_count; index > 0U; --index) {
        const MinicParserLocalBinding *binding;

        binding = &parser->local_bindings[index - 1U];
        if (binding->local_id != MINIC_LOCAL_INVALID &&
            minic_parser_span_equals(parser, name_span, binding->name_span)) {
            return binding->local_id;
        }
    }
    return MINIC_LOCAL_INVALID;
}

MinicGlobalObjectId minic_parser_find_static_local(const MinicParser *parser,
                                                   MinicSourceSpan name_span) {
    size_t index;

    for (index = parser->local_binding_count; index > 0U; --index) {
        const MinicParserLocalBinding *binding;

        binding = &parser->local_bindings[index - 1U];
        if (binding->global_object_id != MINIC_GLOBAL_OBJECT_INVALID &&
            minic_parser_span_equals(parser, name_span, binding->name_span)) {
            return binding->global_object_id;
        }
    }
    return MINIC_GLOBAL_OBJECT_INVALID;
}

bool minic_parser_name_bound(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    for (index = parser->local_binding_count; index > 0U; --index) {
        if (minic_parser_span_equals(
                parser, name_span, parser->local_bindings[index - 1U].name_span)) {
            return true;
        }
    }
    return false;
}

MinicFunctionId minic_parser_find_function(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->function_count; ++index) {
        const MinicFunction *function;

        function = minic_c0_program_function(parser->program, index);
        if (function != NULL && function->name_length == name_length &&
            memcmp(function->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_FUNCTION_INVALID;
}

MinicRecordId minic_parser_find_record(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->record_count; ++index) {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, index);
        if (record != NULL && record->name_length == name_length &&
            memcmp(record->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_RECORD_INVALID;
}
