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
static bool parse_array_bound_bitwise_or(MinicParser *parser, int64_t *value);
static bool parse_array_bound_unary(MinicParser *parser, int64_t *value);

static bool array_bound_type_size(const MinicParser *parser, MinicType type, uint64_t *size) {
    size_t measured_size;
    size_t measured_alignment;

    if (parser == NULL || size == NULL ||
        !minic_data_layout_type(
            parser->data_layout, parser->program, type, &measured_size, &measured_alignment)) {
        return false;
    }
    (void)measured_alignment;
    *size = (uint64_t)measured_size;
    return true;
}

static bool parse_array_bound_sizeof(MinicParser *parser, int64_t *value) {
    MinicType measured_type;
    uint64_t measured_size;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_SIZEOF, "expected 'sizeof'") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after sizeof")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_parser_parse_string_literal_size(parser, &measured_size) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' after sizeof string literal")) {
            return false;
        }
    } else {
        if (!minic_parser_parse_type_name(parser, &measured_type) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after sizeof type") ||
            !array_bound_type_size(parser, measured_type, &measured_size)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "unsupported sizeof type in array bound constant expression");
            }
            return false;
        }
    }
    if (measured_size > (uint64_t)INT64_MAX) {
        minic_parser_error(parser, "sizeof result exceeds array bound constant range");
        return false;
    }
    *value = (int64_t)measured_size;
    return true;
}

static bool array_bound_parenthesis_starts_integer_cast(MinicParser *parser) {
    MinicParser probe;
    MinicTypeAliasId alias_id;
    const MinicTypeAlias *alias;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    switch (probe.current.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_VOLATILE:
    case MINIC_TOKEN_KW_BOOL:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_SHORT:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        alias_id = minic_parser_find_type_alias(parser, probe.current.span);
        alias = minic_c0_program_type_alias(parser->program, alias_id);
        return alias != NULL && minic_type_is_integer(alias->type);
    default:
        return false;
    }
}

static bool array_bound_apply_integer_cast(MinicParser *parser,
                                           MinicType type,
                                           int64_t operand,
                                           int64_t *value) {
    unsigned int bits = 0U;
    uint64_t raw;
    uint64_t mask;
    bool is_unsigned;

    if (parser == NULL || value == NULL || !minic_type_is_integer(type)) {
        if (parser != NULL) {
            minic_parser_error(parser, "array bound cast requires an integer type");
        }
        return false;
    }
    switch (type.integer_rank) {
    case MINIC_INTEGER_RANK_BOOL:
        *value = operand != 0 ? 1 : 0;
        return true;
    case MINIC_INTEGER_RANK_CHAR:
        bits = 8U;
        break;
    case MINIC_INTEGER_RANK_SHORT:
        bits = 16U;
        break;
    case MINIC_INTEGER_RANK_INT:
        bits = 32U;
        break;
    case MINIC_INTEGER_RANK_LONG:
    case MINIC_INTEGER_RANK_LONG_LONG:
        bits = 64U;
        break;
    case MINIC_INTEGER_RANK_INT128:
        minic_parser_error(parser, "128-bit cast exceeds the current 64-bit constant evaluator");
        return false;
    case MINIC_INTEGER_RANK_NONE:
        minic_parser_error(parser, "invalid integer rank in array bound cast");
        return false;
    }
    raw = (uint64_t)operand;
    is_unsigned = minic_type_is_unsigned_integer(type);
    if (bits == 64U) {
        *value = (int64_t)raw;
        return true;
    }
    mask = (UINT64_C(1) << bits) - UINT64_C(1);
    raw &= mask;
    if (!is_unsigned && (raw & (UINT64_C(1) << (bits - 1U))) != 0U) {
        raw |= ~mask;
    }
    *value = (int64_t)raw;
    return true;
}

static bool parse_array_bound_cast(MinicParser *parser, int64_t *value) {
    MinicType cast_type;
    int64_t operand;

    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' before array bound cast") ||
        !minic_parser_parse_type_name(parser, &cast_type) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after array bound cast type") ||
        !parse_array_bound_unary(parser, &operand)) {
        return false;
    }
    return array_bound_apply_integer_cast(parser, cast_type, operand, value);
}

static bool constant_record_member_offset(const MinicParser *parser,
                                          const MinicRecord *record,
                                          const char *name,
                                          size_t name_length,
                                          uint64_t *offset) {
    size_t field_index;
    size_t field_offset;

    if (parser == NULL || record == NULL || name == NULL || offset == NULL ||
        !record->is_complete) {
        return false;
    }
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field = &record->fields[field_index];

        if (field->name_length == name_length && memcmp(field->name, name, name_length) == 0) {
            if (field->is_bit_field ||
                !minic_data_layout_record_field_offset(
                    parser->data_layout, parser->program, record, field_index, &field_offset)) {
                return false;
            }
            *offset = (uint64_t)field_offset;
            return true;
        }
    }
    return false;
}

static bool current_is_builtin_offsetof_constant(const MinicParser *parser) {
    static const char name[] = "__builtin_offsetof";
    size_t length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == sizeof(name) - 1U &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

static bool parse_offsetof_integer_constant(MinicParser *parser, int64_t *value) {
    MinicSourceSpan field_span;
    MinicType record_type;
    const MinicRecord *record;
    uint64_t offset;
    size_t field_name_length;

    if (parser == NULL || value == NULL || !current_is_builtin_offsetof_constant(parser) ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_offsetof") ||
        !minic_parser_parse_type_name(parser, &record_type)) {
        return false;
    }
    if (!minic_type_is_record(record_type)) {
        minic_parser_error(parser, "__builtin_offsetof requires a record type");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_offsetof") ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected direct record field in __builtin_offsetof");
        }
        return false;
    }
    field_span = parser->current.span;
    field_name_length = minic_parser_span_length(field_span);
    if (!constant_record_member_offset(
            parser, record, parser->source + field_span.begin.offset, field_name_length, &offset) ||
        offset > (uint64_t)INT64_MAX || !minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after __builtin_offsetof")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "cannot fold __builtin_offsetof in integer constant expression");
        }
        return false;
    }
    *value = (int64_t)offset;
    return true;
}

static bool parse_array_bound_primary(MinicParser *parser, int64_t *value) {
    if (parser == NULL || value == NULL) {
        return false;
    }
    if (current_is_builtin_offsetof_constant(parser)) {
        return parse_offsetof_integer_constant(parser, value);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_ALIGNOF) {
        return minic_parser_parse_alignof_type_value(parser, value, NULL);
    }
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return minic_parser_parse_integer_value64(parser, value);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        int enum_value;

        if (minic_parser_find_enum_constant(parser, parser->current.span, &enum_value)) {
            *value = (int64_t)enum_value;
            return minic_parser_advance(parser);
        }
    }
    if (parser->current.kind == MINIC_TOKEN_KW_SIZEOF) {
        return parse_array_bound_sizeof(parser, value);
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (array_bound_parenthesis_starts_integer_cast(parser)) {
            return parse_array_bound_cast(parser, value);
        }
        if (!minic_parser_advance(parser) || !parse_array_bound_bitwise_or(parser, value) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' in integer constant expression")) {
            return false;
        }
        return true;
    }
    minic_parser_error(parser, "expected integer constant expression");
    return false;
}

static bool parse_array_bound_unary(MinicParser *parser, int64_t *value) {
    MinicTokenKind operator_kind;
    int64_t operand;

    if (parser == NULL || value == NULL) {
        return false;
    }
    operator_kind = parser->current.kind;
    if (operator_kind != MINIC_TOKEN_PLUS && operator_kind != MINIC_TOKEN_MINUS &&
        operator_kind != MINIC_TOKEN_TILDE && operator_kind != MINIC_TOKEN_BANG) {
        return parse_array_bound_primary(parser, value);
    }
    if (!minic_parser_advance(parser) || !parse_array_bound_unary(parser, &operand)) {
        return false;
    }
    if (operator_kind == MINIC_TOKEN_MINUS) {
        if (operand == INT64_MIN) {
            minic_parser_error(parser, "integer constant expression overflow");
            return false;
        }
        operand = -operand;
    } else if (operator_kind == MINIC_TOKEN_TILDE) {
        operand = (int64_t)(~(uint64_t)operand);
    } else if (operator_kind == MINIC_TOKEN_BANG) {
        operand = operand == 0 ? 1 : 0;
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
                minic_parser_error(parser, "integer constant expression overflow");
                return false;
            }
            left *= right;
        } else {
            if (right == 0) {
                minic_parser_error(parser, "division by zero in integer constant expression");
                return false;
            }
            if (left == INT64_MIN && right == -1) {
                minic_parser_error(parser, "integer constant expression overflow");
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
                minic_parser_error(parser, "integer constant expression overflow");
                return false;
            }
            left += right;
        } else {
            if ((right < 0 && left > INT64_MAX + right) ||
                (right > 0 && left < INT64_MIN + right)) {
                minic_parser_error(parser, "integer constant expression overflow");
                return false;
            }
            left -= right;
        }
    }
    *value = left;
    return true;
}

static bool parse_array_bound_shift(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_additive(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_LESS_LESS ||
           parser->current.kind == MINIC_TOKEN_GREATER_GREATER) {
        MinicTokenKind operator_kind;
        int64_t right;

        operator_kind = parser->current.kind;
        if (!minic_parser_advance(parser) || !parse_array_bound_additive(parser, &right)) {
            return false;
        }
        if (right < 0 || right >= 64) {
            minic_parser_error(parser,
                               "shift count is out of range in integer constant expression");
            return false;
        }
        if (operator_kind == MINIC_TOKEN_LESS_LESS) {
            if (left < 0 || (right != 0 && left > (INT64_MAX >> (unsigned int)right))) {
                minic_parser_error(parser, "left shift overflows integer constant expression");
                return false;
            }
            left <<= (unsigned int)right;
        } else {
            left >>= (unsigned int)right;
        }
    }
    *value = left;
    return true;
}

static bool parse_array_bound_bitwise_and(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_shift(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_AMPERSAND) {
        int64_t right;

        if (!minic_parser_advance(parser) || !parse_array_bound_shift(parser, &right)) {
            return false;
        }
        left = (int64_t)((uint64_t)left & (uint64_t)right);
    }
    *value = left;
    return true;
}

static bool parse_array_bound_bitwise_xor(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_bitwise_and(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_CARET) {
        int64_t right;

        if (!minic_parser_advance(parser) || !parse_array_bound_bitwise_and(parser, &right)) {
            return false;
        }
        left = (int64_t)((uint64_t)left ^ (uint64_t)right);
    }
    *value = left;
    return true;
}

static bool parse_array_bound_bitwise_or(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_bitwise_xor(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_PIPE) {
        int64_t right;

        if (!minic_parser_advance(parser) || !parse_array_bound_bitwise_xor(parser, &right)) {
            return false;
        }
        left = (int64_t)((uint64_t)left | (uint64_t)right);
    }
    *value = left;
    return true;
}

bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value) {
    return parser != NULL && value != NULL && parse_array_bound_bitwise_or(parser, value);
}

bool minic_parser_parse_integer_constant_expression_value(MinicParser *parser, int64_t *value) {
    return value != NULL && parse_array_bound_additive(parser, value);
}

bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (element_count == NULL || !minic_parser_parse_integer_constant_expression(parser, &value)) {
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
        !minic_parser_parse_integer_constant_expression(parser, &value)) {
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
