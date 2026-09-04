#include "frontend/parser_internal.h"

#include <limits.h>
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

bool minic_parser_parse_typed_integer_constant_expression(MinicParser *parser, int64_t *value) {
    MinicConstValue constant;
    MinicExpressionId expression_id;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    if (!minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant)) {
        minic_parser_error(parser, "expected integer constant expression");
        return false;
    }
    if (!minic_const_value_as_int64(parser->program, parser->target_info, &constant, value)) {
        minic_parser_error(parser, "integer constant expression exceeds supported 64-bit range");
        return false;
    }
    return true;
}

bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value) {
    return minic_parser_parse_typed_integer_constant_expression(parser, value);
}

bool minic_parser_parse_integer_constant_expression_value(MinicParser *parser, int64_t *value) {
    return minic_parser_parse_typed_integer_constant_expression(parser, value);
}

bool minic_parser_parse_integer_initializer_bits(MinicParser *parser,
                                                 MinicType target_type,
                                                 uint64_t *bits) {
    MinicConstValue constant;
    MinicConstValue converted;
    MinicExpressionId expression_id;

    if (parser == NULL || bits == NULL || !minic_type_is_integer(target_type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "integer initializer requires an integer target type");
        }
        return false;
    }
    if (!minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    if (!minic_c0_assignment_compatible(parser->program, target_type, expression_id)) {
        minic_parser_error(parser, "integer initializer type mismatch");
        return false;
    }
    if (!minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant) ||
        !minic_const_value_convert_integer(
            parser->program, parser->target_info, &constant, target_type, &converted)) {
        minic_parser_error(parser, "integer initializer requires an integer constant expression");
        return false;
    }
    *bits = converted.bits;
    return true;
}

bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (element_count == NULL ||
        !minic_parser_parse_typed_integer_constant_expression(parser, &value)) {
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

bool minic_parser_parse_record_array_bound(MinicParser *parser,
                                           size_t *element_count,
                                           bool *is_zero_length) {
    int64_t value;

    if (element_count == NULL || is_zero_length == NULL ||
        !minic_parser_parse_typed_integer_constant_expression(parser, &value)) {
        return false;
    }
    if (value < 0) {
        minic_parser_error(parser, "record array bound must not be negative");
        return false;
    }
    if ((uint64_t)value > (uint64_t)SIZE_MAX) {
        minic_parser_error(parser, "record array bound exceeds target object range");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    *is_zero_length = value == 0;
    *element_count = value == 0 ? 1U : (size_t)value;
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

const MinicAttributeDescriptor *minic_parser_current_attribute(const MinicParser *parser) {
    size_t name_length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return NULL;
    }
    name_length = minic_parser_span_length(parser->current.span);
    return minic_attribute_lookup(parser->source + parser->current.span.begin.offset, name_length);
}

bool minic_parser_current_attribute_is(const MinicParser *parser,
                                       MinicAttributeKind kind,
                                       MinicAttributeTarget target) {
    const MinicAttributeDescriptor *descriptor;

    descriptor = minic_parser_current_attribute(parser);
    return descriptor != NULL && descriptor->kind == kind &&
           minic_attribute_allowed_on(descriptor, target);
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

bool minic_parser_materialize_cleanup_contexts(MinicParser *parser,
                                               MinicCleanupContextId stop_context) {
    MinicCleanupContextId current;

    if (parser == NULL ||
        !minic_c0_cleanup_context_reaches(parser->program, parser->cleanup_context, stop_context)) {
        if (parser != NULL) {
            minic_parser_error(parser, "invalid cleanup lifetime exit");
        }
        return false;
    }
    current = parser->cleanup_context;
    while (current != stop_context) {
        const MinicCleanupContext *context;
        const MinicExpression *expression;
        MinicStatement statement;

        context = minic_c0_program_cleanup_context(parser->program, current);
        expression = context == NULL ? NULL
                                     : minic_c0_program_expression(parser->program,
                                                                   context->cleanup_expression);
        if (context == NULL || expression == NULL) {
            minic_parser_error(parser, "invalid cleanup lifetime context");
            return false;
        }
        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_EXPRESSION;
        statement.span = expression->span;
        statement.target_expression = MINIC_EXPRESSION_INVALID;
        statement.expression = context->cleanup_expression;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.inline_asm_id = MINIC_INLINE_ASM_INVALID;
        statement.cleanup_context = MINIC_CLEANUP_CONTEXT_ROOT;
        statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        if (!minic_parser_add_statement(parser, &statement)) {
            return false;
        }
        current = context->parent;
    }
    return true;
}

bool minic_parser_begin_scope(MinicParser *parser) {
    MinicParserScopeFrame *scope;

    if (parser->scope_count == parser->scope_capacity &&
        !minic_parser_grow_array(
            (void **)&parser->scopes, &parser->scope_capacity, sizeof(*parser->scopes))) {
        minic_parser_error(parser, "out of memory while entering scope");
        return false;
    }
    scope = &parser->scopes[parser->scope_count];
    scope->binding_begin = parser->local_binding_count;
    scope->record_tag_begin = parser->record_tag_count;
    scope->cleanup_context = parser->cleanup_context;
    parser->scope_count += 1U;
    return true;
}

void minic_parser_end_scope(MinicParser *parser) {
    size_t label_index;

    if (parser->scope_count == 0U) {
        return;
    }
    for (label_index = parser->local_label_count; label_index > 0U; --label_index) {
        MinicParserLocalLabel *label;

        label = &parser->local_labels[label_index - 1U];
        if (label->is_active && label->scope_depth == parser->scope_count) {
            label->is_active = false;
        }
    }
    parser->scope_count -= 1U;
    parser->local_binding_count = parser->scopes[parser->scope_count].binding_begin;
    parser->record_tag_count = parser->scopes[parser->scope_count].record_tag_begin;
    parser->cleanup_context = parser->scopes[parser->scope_count].cleanup_context;
}

bool minic_parser_declare_local_label(MinicParser *parser,
                                      MinicSourceSpan name_span,
                                      MinicStatementId statement_id) {
    MinicParserLocalLabel *label;
    size_t index;

    if (parser == NULL || parser->scope_count == 0U ||
        parser->current_function == MINIC_FUNCTION_INVALID ||
        statement_id == MINIC_STATEMENT_INVALID) {
        if (parser != NULL) {
            minic_parser_error(parser, "GNU local label requires an active function scope");
        }
        return false;
    }
    for (index = parser->local_label_count; index > 0U; --index) {
        const MinicParserLocalLabel *existing;

        existing = &parser->local_labels[index - 1U];
        if (existing->is_active && existing->scope_depth == parser->scope_count &&
            minic_parser_span_equals(parser, existing->name_span, name_span)) {
            minic_parser_error(parser, "duplicate GNU local label declaration");
            return false;
        }
    }
    if (parser->local_label_count == parser->local_label_capacity &&
        !minic_parser_grow_array((void **)&parser->local_labels,
                                 &parser->local_label_capacity,
                                 sizeof(*parser->local_labels))) {
        minic_parser_error(parser, "out of memory while declaring GNU local label");
        return false;
    }
    label = &parser->local_labels[parser->local_label_count];
    label->name_span = name_span;
    label->statement_id = statement_id;
    label->scope_depth = parser->scope_count;
    label->is_active = true;
    label->is_defined = false;
    parser->local_label_count += 1U;
    return true;
}

MinicStatementId minic_parser_find_local_label(const MinicParser *parser,
                                               MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return MINIC_STATEMENT_INVALID;
    }
    for (index = parser->local_label_count; index > 0U; --index) {
        const MinicParserLocalLabel *label;

        label = &parser->local_labels[index - 1U];
        if (label->is_active && minic_parser_span_equals(parser, label->name_span, name_span)) {
            return label->statement_id;
        }
    }
    return MINIC_STATEMENT_INVALID;
}

bool minic_parser_define_local_label(MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     MinicStatementId *statement_id) {
    size_t index;

    if (parser == NULL || statement_id == NULL) {
        return false;
    }
    for (index = parser->local_label_count; index > 0U; --index) {
        MinicParserLocalLabel *label;

        label = &parser->local_labels[index - 1U];
        if (!label->is_active || !minic_parser_span_equals(parser, label->name_span, name_span)) {
            continue;
        }
        if (label->is_defined) {
            minic_parser_error(parser, "duplicate GNU local label definition");
            return false;
        }
        label->is_defined = true;
        *statement_id = label->statement_id;
        return true;
    }
    return false;
}

bool minic_parser_statement_is_local_label(const MinicParser *parser,
                                           MinicStatementId statement_id) {
    size_t index;

    if (parser == NULL || statement_id == MINIC_STATEMENT_INVALID) {
        return false;
    }
    for (index = 0U; index < parser->local_label_count; ++index) {
        if (parser->local_labels[index].statement_id == statement_id) {
            return true;
        }
    }
    return false;
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

bool minic_parser_bind_scoped_global_object(MinicParser *parser,
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
    scope_begin = parser->scopes[parser->scope_count - 1U].binding_begin;
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
    scope_begin = parser->scopes[parser->scope_count - 1U].binding_begin;
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

MinicGlobalObjectId
minic_parser_find_scoped_global_object_in_current_scope(const MinicParser *parser,
                                                        MinicSourceSpan name_span) {
    size_t scope_begin;
    size_t index;

    if (parser == NULL || parser->scope_count == 0U) {
        return MINIC_GLOBAL_OBJECT_INVALID;
    }
    scope_begin = parser->scopes[parser->scope_count - 1U].binding_begin;
    for (index = parser->local_binding_count; index > scope_begin; --index) {
        const MinicParserLocalBinding *binding;

        binding = &parser->local_bindings[index - 1U];
        if (binding->global_object_id != MINIC_GLOBAL_OBJECT_INVALID &&
            minic_parser_span_equals(parser, name_span, binding->name_span)) {
            return binding->global_object_id;
        }
    }
    return MINIC_GLOBAL_OBJECT_INVALID;
}

void minic_parser_destroy_scopes(MinicParser *parser) {
    free(parser->local_labels);
    parser->local_labels = NULL;
    parser->local_label_count = 0U;
    parser->local_label_capacity = 0U;
    free(parser->local_bindings);
    free(parser->record_tags);
    free(parser->scopes);
    parser->local_bindings = NULL;
    parser->local_binding_count = 0U;
    parser->local_binding_capacity = 0U;
    parser->record_tags = NULL;
    parser->record_tag_count = 0U;
    parser->record_tag_capacity = 0U;
    parser->scopes = NULL;
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

MinicGlobalObjectId minic_parser_find_scoped_global_object(const MinicParser *parser,
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
    size_t index;

    if (parser == NULL) {
        return MINIC_RECORD_INVALID;
    }
    for (index = parser->record_tag_count; index > 0U; --index) {
        const MinicParserRecordTag *tag;

        tag = &parser->record_tags[index - 1U];
        if (minic_parser_span_equals(parser, tag->name_span, name_span)) {
            return tag->record_id;
        }
    }
    return MINIC_RECORD_INVALID;
}

MinicRecordId minic_parser_find_record_in_current_scope(const MinicParser *parser,
                                                        MinicSourceSpan name_span) {
    size_t begin;
    size_t index;

    if (parser == NULL) {
        return MINIC_RECORD_INVALID;
    }
    begin =
        parser->scope_count == 0U ? 0U : parser->scopes[parser->scope_count - 1U].record_tag_begin;
    for (index = parser->record_tag_count; index > begin; --index) {
        const MinicParserRecordTag *tag;

        tag = &parser->record_tags[index - 1U];
        if (minic_parser_span_equals(parser, tag->name_span, name_span)) {
            return tag->record_id;
        }
    }
    return MINIC_RECORD_INVALID;
}

bool minic_parser_bind_record_tag(MinicParser *parser,
                                  MinicSourceSpan name_span,
                                  MinicRecordId record_id) {
    MinicParserRecordTag *tag;

    if (parser == NULL || record_id == MINIC_RECORD_INVALID ||
        minic_parser_find_record_in_current_scope(parser, name_span) != MINIC_RECORD_INVALID) {
        if (parser != NULL) {
            minic_parser_error(parser, "duplicate record tag binding in current scope");
        }
        return false;
    }
    if (parser->record_tag_count == parser->record_tag_capacity &&
        !minic_parser_grow_array((void **)&parser->record_tags,
                                 &parser->record_tag_capacity,
                                 sizeof(*parser->record_tags))) {
        minic_parser_error(parser, "out of memory while binding record tag");
        return false;
    }
    tag = &parser->record_tags[parser->record_tag_count];
    tag->name_span = name_span;
    tag->record_id = record_id;
    parser->record_tag_count += 1U;
    return true;
}
