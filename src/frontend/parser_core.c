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
        const char *trace = getenv("CORE_FAST_TRACE");

        if (trace != NULL && trace[0] != '\0' && strcmp(trace, "0") != 0) {
            const MinicExpression *expression =
                minic_c0_program_expression(parser->program, expression_id);
            if (expression != NULL &&
                expression->span.begin.offset <= expression->span.end.offset &&
                expression->span.end.offset <= parser->lexer.length) {
                size_t raw_length = expression->span.end.offset - expression->span.begin.offset;
                (void)fprintf(stderr,
                              "FRONTEND_CONST_DETAIL expression=%u kind=%d category=%d "
                              "span=%zu:%zu..%zu:%zu raw=%.*s\n",
                              (unsigned)expression_id,
                              (int)expression->kind,
                              (int)expression->value_category,
                              expression->span.begin.line,
                              expression->span.begin.column,
                              expression->span.end.line,
                              expression->span.end.column,
                              (int)(raw_length > 240U ? 240U : raw_length),
                              parser->source + expression->span.begin.offset);
            }
        }
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
    if (minic_const_eval_integer(
            parser->program, parser->target_info, expression_id, &constant)) {
        if (!minic_const_value_convert_integer(
                parser->program, parser->target_info, &constant, target_type, &converted)) {
            minic_parser_error(parser,
                               "integer initializer constant cannot be converted to target type");
            return false;
        }
    } else if (!minic_const_eval_arithmetic_to_integer(parser->program,
                                                       parser->target_info,
                                                       expression_id,
                                                       target_type,
                                                       &converted)) {
        minic_parser_error(parser, "integer initializer requires an arithmetic constant expression");
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

/* One lexical identifier matcher for general grammar and builtin parsing. */
bool minic_parser_current_identifier_lexeme_equals_cstr(const MinicParser *parser, const char *name) {
    size_t length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(name) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
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
    scope->tag_begin = parser->tag_count;
    scope->enum_constant_begin = parser->enum_constant_count;
    scope->cleanup_context = parser->cleanup_context;
    parser->scope_count += 1U;
    return true;
}

/* Binding arrays contain value/ID/span metadata, not separately owned heap
 * payloads: leaving a scope rewinds visibility counts without destroying the
 * Program-owned Record/Enum/Type entities declared inside that scope. */
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
    parser->tag_count = parser->scopes[parser->scope_count].tag_begin;
    parser->enum_constant_count = parser->scopes[parser->scope_count].enum_constant_begin;
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
    binding->type_alias_id = MINIC_TYPE_ALIAS_INVALID;
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

bool minic_parser_bind_type_alias(MinicParser *parser,
                                  MinicSourceSpan name_span,
                                  MinicTypeAliasId type_alias_id) {
    MinicParserLocalBinding *binding;

    if (parser == NULL || parser->scope_count == 0U ||
        type_alias_id == MINIC_TYPE_ALIAS_INVALID) {
        if (parser != NULL) {
            minic_parser_error(parser, "internal error: invalid scoped typedef binding");
        }
        return false;
    }
    if (parser->local_binding_count == parser->local_binding_capacity &&
        !minic_parser_grow_array((void **)&parser->local_bindings,
                                 &parser->local_binding_capacity,
                                 sizeof(*parser->local_bindings))) {
        minic_parser_error(parser, "out of memory while binding typedef name");
        return false;
    }
    binding = &parser->local_bindings[parser->local_binding_count];
    binding->name_span = name_span;
    binding->local_id = MINIC_LOCAL_INVALID;
    binding->global_object_id = MINIC_GLOBAL_OBJECT_INVALID;
    binding->type_alias_id = type_alias_id;
    parser->local_binding_count += 1U;
    return true;
}

bool minic_parser_name_bound_in_current_scope(const MinicParser *parser,
                                              MinicSourceSpan name_span) {
    size_t scope_begin;
    size_t enum_begin;
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
    enum_begin = parser->scopes[parser->scope_count - 1U].enum_constant_begin;
    for (index = parser->enum_constant_count; index > enum_begin; --index) {
        if (minic_parser_span_equals(
                parser, name_span, parser->enum_constants[index - 1U].name_span)) {
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
    if (getenv("MINIC_TYPEDEF_INDEX_PERF") != NULL) {
        (void)fprintf(stderr,
                      "MINIC_TYPEDEF_INDEX queries=%zu probes=%zu fallback=%zu "
                      "rebuilds=%zu aliases=%zu\n",
                      parser->perf_typedef_queries, parser->perf_typedef_probes,
                      parser->perf_typedef_fallbacks, parser->perf_typedef_rebuilds,
                      parser->program == NULL ? 0U : parser->program->type_alias_count);
    }
    free(parser->perf_typedef_name_slots);
    parser->perf_typedef_name_slots = NULL;
    free(parser->local_labels);
    parser->local_labels = NULL;
    parser->local_label_count = 0U;
    parser->local_label_capacity = 0U;
    free(parser->local_bindings);
    free(parser->tags);
    free(parser->scopes);
    parser->local_bindings = NULL;
    parser->local_binding_count = 0U;
    parser->local_binding_capacity = 0U;
    parser->tags = NULL;
    parser->tag_count = 0U;
    parser->tag_capacity = 0U;
    parser->scopes = NULL;
    parser->scope_count = 0U;
    parser->scope_capacity = 0U;
}


bool minic_parser_find_nearest_ordinary_binding(
    const MinicParser *parser, MinicSourceSpan name_span,
    MinicParserOrdinaryBinding *result) {
    size_t depth;
    if (result == NULL) {
        return false;
    }
    result->local_id = MINIC_LOCAL_INVALID;
    result->global_object_id = MINIC_GLOBAL_OBJECT_INVALID;
    result->type_alias_id = MINIC_TYPE_ALIAS_INVALID;
    result->enumerator_id = MINIC_ENUMERATOR_INVALID;
    if (parser == NULL) {
        return false;
    }

    /* Walk scopes innermost first. Within a legal C scope the ordinary
     * namespace has no conflicting declarations across these two arrays. */
    for (depth = parser->scope_count; depth > 0U; --depth) {
        size_t begin_binding = parser->scopes[depth - 1U].binding_begin;
        size_t end_binding = depth == parser->scope_count
            ? parser->local_binding_count : parser->scopes[depth].binding_begin;
        size_t begin_enum = parser->scopes[depth - 1U].enum_constant_begin;
        size_t end_enum = depth == parser->scope_count
            ? parser->enum_constant_count : parser->scopes[depth].enum_constant_begin;
        size_t index;
        for (index = end_binding; index > begin_binding; --index) {
            const MinicParserLocalBinding *binding =
                &parser->local_bindings[index - 1U];
            if (minic_parser_span_equals(parser, name_span, binding->name_span)) {
                result->local_id = binding->local_id;
                result->global_object_id = binding->global_object_id;
                result->type_alias_id = binding->type_alias_id;
                return true;
            }
        }
        for (index = end_enum; index > begin_enum; --index) {
            const MinicParserEnumConstant *enumerator =
                &parser->enum_constants[index - 1U];
            if (minic_parser_span_equals(parser, name_span, enumerator->name_span)) {
                result->enumerator_id = enumerator->enumerator_id;
                return true;
            }
        }
    }
    return false;
}

MinicLocalId minic_parser_find_local(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;
    MinicParser *perf_parser = (MinicParser *)parser;

    perf_parser->perf_find_local_calls += 1U;
    for (index = parser->local_binding_count; index > 0U; --index) {
        perf_parser->perf_find_local_steps += 1U;
        const MinicParserLocalBinding *binding;

        binding = &parser->local_bindings[index - 1U];
        /* Resolve by the nearest binding, even if that binding is a typedef
         * or scoped extern.  Those names hide an outer local variable. */
        if (minic_parser_span_equals(parser, name_span, binding->name_span)) {
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

static size_t minic_parser_function_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    if (name == NULL) {
        return 0U;
    }
    for (index = 0U; index < length; ++index) {
        hash ^= (unsigned char)name[index];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static bool minic_parser_function_name_slot_insert(
    MinicParser *parser,
    MinicFunctionId *slots,
    size_t capacity,
    MinicFunctionId function_id) {
    const MinicFunction *function;
    size_t mask;
    size_t slot_index;
    size_t probes;

    if (parser == NULL || parser->program == NULL || slots == NULL ||
        capacity == 0U || (capacity & (capacity - 1U)) != 0U) {
        return false;
    }
    function = minic_c0_program_function(parser->program, function_id);
    if (function == NULL || function->name == NULL || function->name_length == 0U) {
        return true;
    }
    mask = capacity - 1U;
    slot_index = minic_parser_function_name_hash(function->name, function->name_length) & mask;
    for (probes = 0U; probes < capacity; ++probes) {
        MinicFunctionId existing_id = slots[slot_index];

        if (existing_id == MINIC_FUNCTION_INVALID) {
            slots[slot_index] = function_id;
            return true;
        }
        {
            const MinicFunction *existing =
                minic_c0_program_function(parser->program, existing_id);
            if (existing != NULL &&
                existing->name_length == function->name_length &&
                memcmp(existing->name, function->name, function->name_length) == 0) {
                /* Preserve first-declaration wins. Unlike a reverse scan,
                   later redeclarations must NOT displace earlier ids. */
                if (function_id < existing_id) {
                    slots[slot_index] = function_id;
                }
                return true;
            }
        }
        slot_index = (slot_index + 1U) & mask;
    }
    return false;
}

static bool minic_parser_function_name_index_rebuild(
    MinicParser *parser,
    size_t required_count) {
    MinicFunctionId *slots;
    size_t capacity = 16U;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return false;
    }
    while (capacity < required_count * 2U) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*slots)) {
        return false;
    }
    slots = (MinicFunctionId *)malloc(capacity * sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (index = 0U; index < capacity; ++index) {
        slots[index] = MINIC_FUNCTION_INVALID;
    }
    for (index = 0U; index < required_count; ++index) {
        if (!minic_parser_function_name_slot_insert(
                parser, slots, capacity, (MinicFunctionId)index)) {
            free(slots);
            return false;
        }
    }
    free(parser->perf_function_name_slots);
    parser->perf_function_name_slots = slots;
    parser->perf_function_name_slot_capacity = capacity;
    parser->perf_function_name_indexed_count = required_count;
    return true;
}

static bool minic_parser_function_name_index_sync(MinicParser *parser) {
    size_t required_count;
    size_t index;

    if (parser == NULL || parser->program == NULL) {
        return false;
    }
    required_count = parser->program->function_count;
    if (required_count < parser->perf_function_name_indexed_count) {
        return minic_parser_function_name_index_rebuild(parser, required_count);
    }
    if (parser->perf_function_name_slots == NULL ||
        parser->perf_function_name_slot_capacity == 0U ||
        required_count > parser->perf_function_name_slot_capacity / 2U) {
        return minic_parser_function_name_index_rebuild(parser, required_count);
    }
    for (index = parser->perf_function_name_indexed_count;
         index < required_count;
         ++index) {
        if (!minic_parser_function_name_slot_insert(
                parser,
                parser->perf_function_name_slots,
                parser->perf_function_name_slot_capacity,
                (MinicFunctionId)index)) {
            return false;
        }
    }
    parser->perf_function_name_indexed_count = required_count;
    return true;
}

/* The original lookup is first-declaration-wins.  Keep that exact
 * ordering on allocator failure and in forced-fallback regression tests. */
static MinicFunctionId minic_parser_function_name_linear_fallback(
    MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;
    const size_t length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->function_count; ++index) {
        const MinicFunction *fn = minic_c0_program_function(parser->program, index);
        parser->perf_find_function_steps += 1U;
        if (fn != NULL && fn->name_length == length &&
            memcmp(fn->name, parser->source + name_span.begin.offset, length) == 0) {
            return (MinicFunctionId)index;
        }
    }
    return MINIC_FUNCTION_INVALID;
}

MinicFunctionId minic_parser_find_function(const MinicParser *parser_const,
                                                 MinicSourceSpan name_span) {
    MinicParser *parser = (MinicParser *)parser_const;
    size_t name_length;
    size_t mask;
    size_t slot_index;
    size_t probes;

    if (parser == NULL || parser->program == NULL) {
        return MINIC_FUNCTION_INVALID;
    }
    parser->perf_find_function_calls += 1U;
#if defined(MINIC_TEST_FORCE_PARSER_HASH_FALLBACK)
    return minic_parser_function_name_linear_fallback(parser, name_span);
#endif
    if (parser->cache_owner != parser ||
        !minic_parser_function_name_index_sync(parser) ||
        parser->perf_function_name_slot_capacity == 0U) {
        return minic_parser_function_name_linear_fallback(parser, name_span);
    }
    name_length = minic_parser_span_length(name_span);
    mask = parser->perf_function_name_slot_capacity - 1U;
    slot_index =
        minic_parser_function_name_hash(
            parser->source + name_span.begin.offset, name_length) & mask;
    for (probes = 0U;
         probes < parser->perf_function_name_slot_capacity;
         ++probes) {
        MinicFunctionId function_id =
            parser->perf_function_name_slots[slot_index];
        const MinicFunction *function;

        parser->perf_find_function_steps += 1U;
        if (function_id == MINIC_FUNCTION_INVALID) {
            return MINIC_FUNCTION_INVALID;
        }
        function = minic_c0_program_function(parser->program, function_id);
        if (function != NULL && function->name_length == name_length &&
            memcmp(function->name,
                   parser->source + name_span.begin.offset,
                   name_length) == 0) {
            return function_id;
        }
        slot_index = (slot_index + 1U) & mask;
    }
    return MINIC_FUNCTION_INVALID;
}


const MinicParserTagBinding *minic_parser_find_tag(const MinicParser *parser,
                                                   MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return NULL;
    }
    for (index = parser->tag_count; index > 0U; --index) {
        const MinicParserTagBinding *tag = &parser->tags[index - 1U];

        if (minic_parser_span_equals(parser, tag->name_span, name_span)) {
            return tag;
        }
    }
    return NULL;
}

const MinicParserTagBinding *minic_parser_find_tag_in_current_scope(
    const MinicParser *parser, MinicSourceSpan name_span) {
    size_t begin;
    size_t index;

    if (parser == NULL) {
        return NULL;
    }
    begin = parser->scope_count == 0U ? 0U
                                       : parser->scopes[parser->scope_count - 1U].tag_begin;
    for (index = parser->tag_count; index > begin; --index) {
        const MinicParserTagBinding *tag = &parser->tags[index - 1U];

        if (minic_parser_span_equals(parser, tag->name_span, name_span)) {
            return tag;
        }
    }
    return NULL;
}

/* The returned binding belongs to parser->tags; it is not a stable handle.
 * Binding another tag (or semantic parsing that binds one) may realloc this
 * array. Callers must copy the needed ID/kind or resolve again afterward. */
bool minic_parser_resolve_tag(MinicParser *parser, MinicSourceSpan name_span,
                              MinicParserTagKind kind, bool current_scope_only,
                              const MinicParserTagBinding **tag) {
    if (parser == NULL || tag == NULL) {
        return false;
    }
    *tag = current_scope_only
               ? minic_parser_find_tag_in_current_scope(parser, name_span)
               : minic_parser_find_tag(parser, name_span);
    if (*tag != NULL && (*tag)->kind != kind) {
        minic_parser_error(parser, "tag declared as a different struct/union/enum kind");
        return false;
    }
    return true;
}

static bool minic_parser_bind_tag(MinicParser *parser, MinicSourceSpan name_span,
                                  MinicParserTagKind kind, MinicRecordId record_id,
                                  MinicEnumId enum_id) {
    MinicParserTagBinding *tag;

    if (parser == NULL) {
        return false;
    }
    if (minic_parser_find_tag_in_current_scope(parser, name_span) != NULL) {
        minic_parser_error(parser, "conflicting tag declaration in current scope");
        return false;
    }
    if (parser->tag_count == parser->tag_capacity &&
        !minic_parser_grow_array((void **)&parser->tags,
                                 &parser->tag_capacity,
                                 sizeof(*parser->tags))) {
        minic_parser_error(parser, "out of memory while binding C tag");
        return false;
    }
    tag = &parser->tags[parser->tag_count];
    tag->name_span = name_span;
    tag->kind = kind;
    if (kind == MINIC_PARSER_TAG_ENUM) {
        tag->entity.enum_id = enum_id;
    } else {
        tag->entity.record_id = record_id;
    }
    parser->tag_count += 1U;
    return true;
}

bool minic_parser_bind_record_tag(MinicParser *parser, MinicSourceSpan name_span,
                                  MinicRecordId record_id, bool is_union) {
    if (record_id == MINIC_RECORD_INVALID) {
        return false;
    }
    return minic_parser_bind_tag(parser, name_span,
                                 is_union ? MINIC_PARSER_TAG_UNION : MINIC_PARSER_TAG_STRUCT,
                                 record_id, MINIC_ENUM_INVALID);
}

bool minic_parser_bind_enum_tag(MinicParser *parser, MinicSourceSpan name_span,
                                MinicEnumId enum_id) {
    if (enum_id == MINIC_ENUM_INVALID) {
        return false;
    }
    return minic_parser_bind_tag(parser, name_span, MINIC_PARSER_TAG_ENUM,
                                 MINIC_RECORD_INVALID, enum_id);
}
