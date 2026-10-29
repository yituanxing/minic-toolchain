#include "frontend/parser_internal.h"
#include "frontend/parser_builtin_memory.h"
#include "frontend/expression_semantics.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GNU/C11 builtin expression ownership. Preserve early/literal/late order
 * without leaking individual builtin handlers into the general parser. */

static bool parse_builtin_extract_return_addr(MinicParser *parser,
                                              MinicExpressionId *expression_id) {
    const MinicExpression *operand;
    MinicExpression cast;
    MinicExpressionId operand_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicType void_pointer_type;

    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_extract_return_addr")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_extract_return_addr") ||
        !minic_parser_expression_parse_internal(parser, &operand_id, 0U, true)) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand == NULL || !minic_type_is_pointer(operand->type) ||
        !minic_type_pointer_to(minic_type_void(), &void_pointer_type)) {
        minic_parser_error(parser, "__builtin_extract_return_addr requires a pointer operand");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_extract_return_addr operand");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    /* RISC-V return addresses use the architectural code pointer value directly;
       model GCC's extraction hook as the canonical pointer cast. */
    (void)memset(&cast, 0, sizeof(cast));
    cast.kind = MINIC_EXPRESSION_CAST;
    cast.span.begin = begin;
    cast.span.end = end;
    cast.type = void_pointer_type;
    cast.value_category = MINIC_VALUE_RVALUE;
    cast.value.unary.operand = operand_id;
    return minic_parser_add_expression(parser, &cast, expression_id);
}

static bool parse_builtin_expect(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicExpression conversion;
    MinicExpressionId value_id;
    const MinicExpression *value;
    int64_t expected_value;

    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_expect")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_expect") ||
        !minic_parser_parse_expression(parser, &value_id, 0U)) {
        return false;
    }
    value = minic_c0_program_expression(parser->program, value_id);
    if (value == NULL || !minic_type_is_integer(value->type)) {
        minic_parser_error(parser, "__builtin_expect first argument must have integer type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_COMMA || !minic_parser_advance(parser) ||
        !minic_parser_parse_integer_constant_expression(parser, &expected_value)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "__builtin_expect second argument must be an integer constant");
        }
        return false;
    }
    (void)expected_value;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_expect arguments");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    /* GCC's documented type is long. The prediction hint itself has no runtime semantics,
     * so lower the builtin to the ordinary value conversion and leave optimization metadata
     * for a future IR/branch-probability pass. */
    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span.begin = begin;
    conversion.span.end = end;
    conversion.type = minic_type_long();
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = value_id;
    return minic_parser_add_expression(parser, &conversion, expression_id);
}

/* SQLite's first atomic store is relaxed and scalar. Do not silently
 * substitute ordinary assignment or accept stronger memory orders. */
static bool parse_builtin_atomic_load(MinicParser *parser,
                                      MinicExpressionId *expression_id) {
    MinicExpression expression;
    const MinicExpression *address;
    MinicExpressionId address_id;
    MinicType pointee, result;
    MinicSourcePosition begin, end;
    size_t bytes;
    int64_t order;
    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__atomic_load_n")) return false;
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN,
                             "expected '(' after __atomic_load_n") ||
        !minic_parser_expression_parse_internal(parser, &address_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA,
                             "expected atomic address/order comma") ||
        !minic_parser_parse_integer_constant_expression(parser, &order))
        return false;
    if (order != 0) {
        minic_parser_error(parser, "__atomic_load_n only supports relaxed memory order (0)");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __atomic_load_n");
        return false;
    }
    end = parser->current.span.end;
    address = minic_c0_program_expression(parser->program, address_id);
    if (address == NULL || !minic_type_pointee(address->type, &pointee) ||
        !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
        !minic_target_info_sizeof_type(parser->target_info, parser->program,
                                       pointee, &bytes) ||
        (bytes != 1U && bytes != 2U && bytes != 4U && bytes != 8U) ||
        !minic_type_unqualified(pointee, &result)) {
        minic_parser_error(parser,
            "__atomic_load_n requires readable 1/2/4/8-byte integer or pointer object");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_ATOMIC_LOAD;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = result;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.atomic_load.address = address_id;
    expression.value.atomic_load.memory_order = 0U;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_atomic_store(MinicParser *parser,
                                       MinicExpressionId *expression_id) {
    MinicExpression expression;
    const MinicExpression *address, *source;
    MinicExpressionId address_id, source_id;
    MinicType pointee;
    MinicSourcePosition begin, end;
    int64_t order;
    size_t bytes;
    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__atomic_store_n")) return false;
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN,
                             "expected '(' after __atomic_store_n") ||
        !minic_parser_expression_parse_internal(parser, &address_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA,
                             "expected atomic address/value comma") ||
        !minic_parser_expression_parse_internal(parser, &source_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA,
                             "expected atomic value/order comma") ||
        !minic_parser_parse_integer_constant_expression(parser, &order)) return false;
    if (order != 0) {
        minic_parser_error(parser, "__atomic_store_n only supports relaxed memory order (0)");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __atomic_store_n");
        return false;
    }
    end = parser->current.span.end;
    address = minic_c0_program_expression(parser->program, address_id);
    source = minic_c0_program_expression(parser->program, source_id);
    if (address == NULL || source == NULL ||
        !minic_type_pointee(address->type, &pointee) ||
        !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) || minic_type_is_const(pointee) ||
        !minic_target_info_sizeof_type(parser->target_info, parser->program,
                                       pointee, &bytes) ||
        (bytes != 1U && bytes != 2U && bytes != 4U && bytes != 8U) ||
        !minic_c0_assignment_compatible(parser->program, pointee, source_id)) {
        minic_parser_error(parser,
            "__atomic_store_n requires writable 1/2/4/8-byte integer or pointer object and convertible value");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_ATOMIC_STORE;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.atomic_store.address = address_id;
    expression.value.atomic_store.source = source_id;
    expression.value.atomic_store.memory_order = 0U;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_prefetch(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression discard_cast;
    MinicExpressionId address_id;
    const MinicExpression *address;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    int64_t rw = 0;
    int64_t locality = 3;

    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_prefetch")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_prefetch") ||
        !minic_parser_expression_parse_internal(parser, &address_id, 0U, true)) {
        return false;
    }
    address = minic_c0_program_expression(parser->program, address_id);
    if (address == NULL || !minic_type_is_pointer(address->type)) {
        minic_parser_error(parser, "__builtin_prefetch address must have pointer type");
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_COMMA) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_integer_constant_expression(parser, &rw)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "__builtin_prefetch rw must be an integer constant");
            }
            return false;
        }
        if (rw < 0 || rw > 2) {
            minic_parser_error(parser, "__builtin_prefetch rw must be between 0 and 2");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_integer_constant_expression(parser, &locality)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser,
                                       "__builtin_prefetch locality must be an integer constant");
                }
                return false;
            }
            if (locality < 0 || locality > 3) {
                minic_parser_error(parser, "__builtin_prefetch locality must be between 0 and 3");
                return false;
            }
        }
    }
    (void)rw;
    (void)locality;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_prefetch arguments");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    /* Prefetch is an optimization hint. Keep the address expression as a real
     * runtime edge so side effects and address evaluation are preserved, but
     * lower the hint itself to a void cast. Cast normalization turns this into
     * the canonical DISCARD node; targets may therefore omit a prefetch
     * instruction without inventing a prefetch-specific AST representation. */
    (void)memset(&discard_cast, 0, sizeof(discard_cast));
    discard_cast.kind = MINIC_EXPRESSION_CAST;
    discard_cast.span.begin = begin;
    discard_cast.span.end = end;
    discard_cast.type = minic_type_void();
    discard_cast.value_category = MINIC_VALUE_RVALUE;
    discard_cast.value.unary.operand = address_id;
    return minic_parser_add_expression(parser, &discard_cast, expression_id);
}

static bool current_is_builtin_offsetof(const MinicParser *parser) {
    static const char name[] = "__builtin_offsetof";
    size_t length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == sizeof(name) - 1U &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

typedef struct MinicOffsetofDesignatorState {
    MinicType type;
    MinicExpressionId offset_id;
    bool is_array;
} MinicOffsetofDesignatorState;

static bool append_offsetof_term(MinicParser *parser,
                                 MinicSourcePosition begin,
                                 MinicExpressionId term_id,
                                 MinicOffsetofDesignatorState *state) {
    const MinicExpression *term;
    MinicExpression sum;

    if (parser == NULL || state == NULL || term_id == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    if (state->offset_id == MINIC_EXPRESSION_INVALID) {
        state->offset_id = term_id;
        return true;
    }
    term = minic_c0_program_expression(parser->program, term_id);
    if (term == NULL || !minic_type_is_integer(term->type)) {
        minic_parser_error(parser, "invalid __builtin_offsetof designator offset term");
        return false;
    }
    (void)memset(&sum, 0, sizeof(sum));
    sum.kind = MINIC_EXPRESSION_BINARY;
    sum.span.begin = begin;
    sum.span.end = term->span.end;
    sum.type = minic_type_unsigned_long();
    sum.value_category = MINIC_VALUE_RVALUE;
    sum.value.binary.operator_kind = MINIC_BINARY_ADD;
    sum.value.binary.left = state->offset_id;
    sum.value.binary.right = term_id;
    return minic_parser_add_expression(parser, &sum, &state->offset_id);
}

static bool parse_offsetof_member_segment(MinicParser *parser,
                                          MinicSourcePosition begin,
                                          MinicType record_type,
                                          MinicOffsetofDesignatorState *state) {
    const MinicRecord *record;
    const MinicRecordField *final_field;
    MinicRecordFieldPath path;
    MinicSourceSpan field_span;
    size_t path_index;

    if (parser == NULL || state == NULL || !minic_type_is_record(record_type) ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record field in __builtin_offsetof designator");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser,
                           "__builtin_offsetof member designator requires a complete record");
        return false;
    }
    field_span = parser->current.span;
    if (!minic_parser_find_record_field_path(parser, record, field_span, &path)) {
        minic_parser_error(parser,
                           path.ambiguous ? "record field is ambiguous in __builtin_offsetof"
                                          : "record has no such field in __builtin_offsetof");
        return false;
    }
    if (path.depth == 0U) {
        minic_parser_error(parser, "empty record field path in __builtin_offsetof");
        return false;
    }

    final_field = NULL;
    for (path_index = 0U; path_index < path.depth; ++path_index) {
        const MinicRecord *path_record;
        const MinicRecordField *path_field;
        MinicExpression segment;
        MinicExpressionId segment_id;

        path_record = minic_c0_program_record(parser->program, path.record_ids[path_index]);
        path_field = minic_c0_record_field(path_record, path.field_indices[path_index]);
        if (path_record == NULL || path_field == NULL) {
            minic_parser_error(parser, "invalid record field path in __builtin_offsetof");
            return false;
        }
        if (path_index + 1U == path.depth) {
            if (path_field->is_bit_field) {
                minic_parser_error(parser, "__builtin_offsetof cannot name a bit-field");
                return false;
            }
            final_field = path_field;
        }

        (void)memset(&segment, 0, sizeof(segment));
        segment.kind = MINIC_EXPRESSION_OFFSETOF;
        segment.span.begin = begin;
        segment.span.end = field_span.end;
        segment.type = minic_type_unsigned_long();
        segment.value_category = MINIC_VALUE_RVALUE;
        segment.value.offsetof_value.record_id = path.record_ids[path_index];
        segment.value.offsetof_value.field_index = path.field_indices[path_index];
        if (!minic_parser_add_expression(parser, &segment, &segment_id) ||
            !append_offsetof_term(parser, begin, segment_id, state)) {
            return false;
        }
    }
    if (final_field == NULL || !minic_parser_advance(parser)) {
        return false;
    }
    state->type = final_field->type;
    state->is_array = final_field->is_array;
    return true;
}

static bool parse_offsetof_array_segment(MinicParser *parser,
                                         MinicSourcePosition begin,
                                         MinicOffsetofDesignatorState *state) {
    MinicExpressionId index_id;
    MinicExpressionId stride_id;
    MinicExpressionId scaled_id;
    const MinicExpression *index_expression;
    const MinicArrayType *nested_array;
    MinicExpression stride;
    MinicExpression scaled;
    MinicSourceSpan index_span;
    MinicType index_type;
    MinicType selected_type;
    MinicType scaled_type;

    if (parser == NULL || state == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }
    if (!state->is_array) {
        minic_parser_error(parser, "__builtin_offsetof array designator requires an array field");
        return false;
    }
    selected_type = state->type;
    if (!minic_parser_advance(parser) || !minic_parser_expression_parse_internal(parser, &index_id, 0U, true)) {
        return false;
    }
    index_expression = minic_c0_program_expression(parser->program, index_id);
    if (index_expression == NULL || !minic_type_is_integer(index_expression->type)) {
        minic_parser_error(parser, "__builtin_offsetof array index requires an integer");
        return false;
    }
    /* The expression pool may grow while adding the stride/scaled nodes below.
     * Snapshot semantic data before any append instead of retaining a pool pointer. */
    index_type = index_expression->type;
    index_span = index_expression->span;
    if (parser->current.kind != MINIC_TOKEN_RBRACKET) {
        minic_parser_error(parser, "expected ']' in __builtin_offsetof array designator");
        return false;
    }

    (void)memset(&stride, 0, sizeof(stride));
    stride.kind = MINIC_EXPRESSION_SIZEOF;
    stride.span = parser->current.span;
    stride.type = minic_type_unsigned_long();
    stride.value_category = MINIC_VALUE_RVALUE;
    stride.value.sizeof_type = selected_type;
    if (!minic_parser_add_expression(parser, &stride, &stride_id) ||
        !minic_target_info_integer_common_for_program(
            parser->target_info, parser->program, index_type, stride.type, &scaled_type)) {
        minic_parser_error(parser, "cannot type __builtin_offsetof array index scale");
        return false;
    }

    (void)memset(&scaled, 0, sizeof(scaled));
    scaled.kind = MINIC_EXPRESSION_BINARY;
    scaled.span.begin = index_span.begin;
    scaled.span.end = parser->current.span.end;
    scaled.type = scaled_type;
    scaled.value_category = MINIC_VALUE_RVALUE;
    scaled.value.binary.operator_kind = MINIC_BINARY_MULTIPLY;
    scaled.value.binary.left = index_id;
    scaled.value.binary.right = stride_id;
    if (!minic_parser_add_expression(parser, &scaled, &scaled_id) ||
        !append_offsetof_term(parser, begin, scaled_id, state) || !minic_parser_advance(parser)) {
        return false;
    }

    state->type = selected_type;
    state->is_array = false;
    if (minic_type_is_array(selected_type)) {
        nested_array = minic_c0_program_array_type(parser->program, selected_type.array_type_id);
        if (nested_array == NULL || nested_array->element_count == 0U) {
            minic_parser_error(parser,
                               "invalid nested array type in __builtin_offsetof designator");
            return false;
        }
        state->type = nested_array->element_type;
        state->is_array = true;
    }
    return true;
}

static bool parse_builtin_offsetof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourcePosition begin;
    MinicType record_type;
    MinicOffsetofDesignatorState state;

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_offsetof") ||
        !minic_parser_parse_type_name(parser, &record_type)) {
        return false;
    }
    if (!minic_type_is_record(record_type)) {
        minic_parser_error(parser, "__builtin_offsetof requires a record type");
        return false;
    }
    {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, record_type.record_id);
        if (record == NULL || !record->is_complete) {
            minic_parser_error(parser, "__builtin_offsetof requires a complete record type");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_offsetof") ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected record field in __builtin_offsetof");
        }
        return false;
    }

    state.type = record_type;
    state.offset_id = MINIC_EXPRESSION_INVALID;
    state.is_array = false;
    if (!parse_offsetof_member_segment(parser, begin, record_type, &state)) {
        return false;
    }

    while (parser->current.kind != MINIC_TOKEN_RPAREN) {
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!parse_offsetof_array_segment(parser, begin, &state)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_DOT) {
            MinicType member_record_type;

            if (state.is_array || !minic_type_is_record(state.type)) {
                minic_parser_error(parser,
                                   "__builtin_offsetof nested member designator requires a record");
                return false;
            }
            member_record_type = state.type;
            if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected member name after '.' in __builtin_offsetof");
                return false;
            }
            if (!parse_offsetof_member_segment(parser, begin, member_record_type, &state)) {
                return false;
            }
            continue;
        }
        minic_parser_error(parser, "unsupported __builtin_offsetof designator suffix");
        return false;
    }
    if (state.offset_id == MINIC_EXPRESSION_INVALID) {
        minic_parser_error(parser, "empty __builtin_offsetof designator");
        return false;
    }
    *expression_id = state.offset_id;
    return minic_parser_advance(parser);
}

static bool
generic_types_compatible(const MinicC0Program *program, MinicType left, MinicType right) {
    return minic_c0_types_compatible(program, left, right);
}

static bool
parse_generic_selection(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicExpressionId controlling_id;
    const MinicExpression *controlling;
    MinicExpressionId selected_id = MINIC_EXPRESSION_INVALID;
    MinicExpressionId default_id = MINIC_EXPRESSION_INVALID;
    MinicType controlling_type;
    bool saw_matching_type = false;
    bool saw_default = false;

    if (!minic_parser_current_identifier_lexeme_equals_cstr(parser, "_Generic") || !minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after _Generic") ||
        !minic_parser_expression_parse_internal(parser, &controlling_id, 0U, true)) {
        return false;
    }
    controlling = minic_c0_program_expression(parser->program, controlling_id);
    if (controlling == NULL) {
        minic_parser_error(parser, "invalid _Generic controlling expression");
        return false;
    }
    controlling_type = controlling->type;
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected ',' after _Generic controlling expression")) {
        return false;
    }

    for (;;) {
        bool is_default = parser->current.kind == MINIC_TOKEN_KW_DEFAULT;
        MinicType association_type = minic_type_void();
        MinicExpressionId association_id;

        if (is_default) {
            if (saw_default) {
                minic_parser_error(parser, "duplicate default association in _Generic");
                return false;
            }
            saw_default = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (!minic_parser_parse_type_name(parser, &association_type)) {
            return false;
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_COLON, "expected ':' in _Generic association") ||
            !minic_parser_expression_parse_internal(parser, &association_id, 0U, decay_array)) {
            return false;
        }

        if (is_default) {
            default_id = association_id;
        } else if (generic_types_compatible(parser->program, controlling_type, association_type)) {
            if (saw_matching_type) {
                minic_parser_error(parser, "multiple compatible type associations in _Generic");
                return false;
            }
            saw_matching_type = true;
            selected_id = association_id;
        }

        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after _Generic associations")) {
        return false;
    }
    if (!saw_matching_type) {
        selected_id = default_id;
    }
    if (selected_id == MINIC_EXPRESSION_INVALID) {
        minic_parser_error(parser, "no compatible association and no default in _Generic");
        return false;
    }
    *expression_id = selected_id;
    return true;
}

static bool parse_builtin_types_compatible_p(MinicParser *parser,
                                             MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicType left_type;
    MinicType right_type;

    if (!minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_types_compatible_p")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_types_compatible_p") ||
        !minic_parser_parse_type_name(parser, &left_type) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_types_compatible_p") ||
        !minic_parser_parse_type_name(parser, &right_type)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value =
        generic_types_compatible(parser->program, left_type, right_type) ? 1 : 0;
    return minic_parser_expect(
               parser, MINIC_TOKEN_RPAREN, "expected ')' after __builtin_types_compatible_p") &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool
parse_builtin_choose_expr(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {
    MinicExpressionId condition_id;
    MinicExpressionId when_true_id;
    MinicExpressionId when_false_id;
    MinicConstValue condition_value;
    const MinicExpression *condition_expr;
    bool condition_is_zero;

    if (!minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_choose_expr")) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_choose_expr") ||
        !minic_parser_expression_parse_internal(parser, &condition_id, 0U, true) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected first ',' in __builtin_choose_expr") ||
        !minic_parser_expression_parse_internal(parser, &when_true_id, 0U, decay_array) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected second ',' in __builtin_choose_expr") ||
        !minic_parser_expression_parse_internal(parser, &when_false_id, 0U, decay_array) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after __builtin_choose_expr")) {
        return false;
    }
    condition_expr = minic_c0_program_expression(parser->program, condition_id);
    /* __builtin_constant_p is itself an ICE in a GNU choose-expression.
     * An unresolved query means "not proved constant" (zero) IN THIS
     * constant-expression context.  Keep its original Core expression for
     * ordinary runtime/local-fact uses: folding it globally to zero would
     * lose the existing straight-line constant propagation.
     *
     * Linux Nouveau's PUSH_* _Static_assert macros expand exactly this
     * pattern for nonconstant _p->cur < _p->seg bounds checks. */
    if (condition_expr != NULL &&
        condition_expr->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        condition_expr->value.builtin_unary.operator_kind ==
            MINIC_BUILTIN_UNARY_CONSTANT_P) {
        condition_is_zero = true;
    } else if (!minic_const_eval_integer(
                   parser->program, parser->target_info, condition_id, &condition_value) ||
               !minic_const_value_is_zero(
                   parser->program, parser->target_info,
                   &condition_value, &condition_is_zero)) {
        minic_parser_error(
            parser, "__builtin_choose_expr condition must be an integer constant expression");
        return false;
    }
    *expression_id = condition_is_zero ? when_false_id : when_true_id;
    return true;
}

static bool object_extent_direct_object(const MinicParser *parser,
                                        MinicExpressionId expression_id,
                                        size_t *extent) {
    const MinicExpression *expression;

    if (parser == NULL || extent == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;
        size_t element_size;

        local = minic_c0_program_local(parser->program, expression->value.local_id);
        if (local == NULL) {
            return false;
        }
        if (!local->is_array) {
            return minic_target_info_sizeof_type(
                parser->target_info, parser->program, local->type, extent);
        }
        if (local->element_count == 0U ||
            !minic_target_info_sizeof_type(
                parser->target_info, parser->program, local->type, &element_size) ||
            element_size > SIZE_MAX / local->element_count) {
            return false;
        }
        *extent = element_size * local->element_count;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object;

        object =
            minic_c0_program_global_object(parser->program, expression->value.global_object_id);
        return object != NULL && minic_target_info_sizeof_type(
                                     parser->target_info, parser->program, object->type, extent);
    }
    return false;
}

static bool
object_extent_exact_start(const MinicParser *parser, MinicExpressionId pointer_id, size_t *extent) {
    const MinicExpression *pointer;
    const MinicExpression *operand;

    if (parser == NULL || extent == NULL) {
        return false;
    }
    pointer = minic_c0_program_expression(parser->program, pointer_id);
    if (pointer == NULL) {
        return false;
    }
    if (pointer->kind == MINIC_EXPRESSION_CAST && minic_type_is_pointer(pointer->type)) {
        const MinicExpression *cast_operand;

        cast_operand = minic_c0_program_expression(parser->program, pointer->value.unary.operand);
        if (cast_operand != NULL && minic_type_is_pointer(cast_operand->type)) {
            return object_extent_exact_start(parser, pointer->value.unary.operand, extent);
        }
    }
    if (pointer->kind != MINIC_EXPRESSION_ADDRESS_OF) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, pointer->value.unary.operand);
    if (operand == NULL) {
        return false;
    }
    if (object_extent_direct_object(parser, pointer->value.unary.operand, extent)) {
        return true;
    }
    if (operand->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *index;

        index = minic_c0_program_expression(parser->program, operand->value.subscript.index);
        if (index == NULL || index->kind != MINIC_EXPRESSION_INTEGER ||
            !minic_type_is_integer(index->type) || index->value.integer_value != 0) {
            return false;
        }
        return object_extent_direct_object(parser, operand->value.subscript.base, extent);
    }
    return false;
}

static bool parse_builtin_object_size(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression result;
    MinicExpressionId pointer_id;
    const MinicExpression *pointer;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    int64_t mode;
    size_t extent;
    uint64_t result_bits;

    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_object_size")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_object_size") ||
        !minic_parser_expression_parse_internal(parser, &pointer_id, 0U, true)) {
        return false;
    }
    pointer = minic_c0_program_expression(parser->program, pointer_id);
    if (pointer == NULL || !minic_type_is_pointer(pointer->type)) {
        minic_parser_error(parser, "__builtin_object_size first argument must be a pointer");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_object_size") ||
        !minic_parser_parse_integer_constant_expression(parser, &mode)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "__builtin_object_size mode must be an integer constant");
        }
        return false;
    }
    if (mode < 0 || mode > 3) {
        minic_parser_error(parser, "__builtin_object_size mode must be between 0 and 3");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_object_size arguments");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (object_extent_exact_start(parser, pointer_id, &extent)) {
        result_bits = (uint64_t)extent;
    } else {
        result_bits = mode < 2 ? UINT64_MAX : UINT64_C(0);
    }

    /* This is a compile-time query: keep the parsed pointer expression only as
     * semantic/provenance input and never retain a runtime edge. Unknown objects
     * use GCC's conservative mode-dependent fallback. */
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_EXPRESSION_INTEGER;
    result.span.begin = begin;
    result.span.end = end;
    result.type = minic_type_unsigned_long();
    result.value_category = MINIC_VALUE_RVALUE;
    (void)memcpy(&result.value.integer_value, &result_bits, sizeof(result_bits));
    return minic_parser_add_expression(parser, &result, expression_id);
}

static bool parse_builtin_constant_p(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression result;
    MinicExpressionId operand_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicConstValue constant_value;
    bool is_constant;

    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_constant_p")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_constant_p") ||
        !minic_parser_expression_parse_internal(parser, &operand_id, 0U, true)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_constant_p operand");
        return false;
    }
    end = parser->current.span.end;
    is_constant =
        minic_const_eval_integer(parser->program, parser->target_info, operand_id, &constant_value);
    if (!minic_parser_advance(parser)) {
        return false;
    }

    /* Keep unresolved constant-p queries unevaluated in the AST.  Core lowering
     * can then answer the query from its conservative straight-line local fact
     * table without ever evaluating the operand at runtime.  Pure frontend ICEs
     * remain folded immediately. */
    (void)memset(&result, 0, sizeof(result));
    result.span.begin = begin;
    result.span.end = end;
    result.type = minic_type_int();
    result.value_category = MINIC_VALUE_RVALUE;
    if (is_constant) {
        result.kind = MINIC_EXPRESSION_INTEGER;
        result.value.integer_value = 1;
    } else {
        result.kind = MINIC_EXPRESSION_BUILTIN_UNARY;
        result.value.builtin_unary.operator_kind = MINIC_BUILTIN_UNARY_CONSTANT_P;
        result.value.builtin_unary.operand = operand_id;
    }
    return minic_parser_add_expression(parser, &result, expression_id);
}

static bool parse_builtin_call_frame_address(MinicParser *parser,
                                             MinicCallFrameAddressKind kind,
                                             const char *spelling,
                                             MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicExpressionId level_id;
    const MinicExpression *level_expression;
    MinicConstValue level_value;
    MinicSourcePosition begin;
    int64_t level;

    if (parser == NULL || spelling == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, spelling)) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after call-frame builtin") ||
        !minic_parser_expression_parse_internal(parser, &level_id, 0U, true)) {
        return false;
    }
    level_expression = minic_c0_program_expression(parser->program, level_id);
    if (level_expression == NULL || !minic_type_is_integer(level_expression->type) ||
        !minic_const_eval_integer(parser->program, parser->target_info, level_id, &level_value) ||
        !minic_const_value_as_int64(parser->program, parser->target_info, &level_value, &level)) {
        minic_parser_error(parser, "%s level must be an integer constant", spelling);
        return false;
    }
    if (level < 0 || (uint64_t)level > (uint64_t)UINT_MAX ||
        !minic_target_info_call_frame_address_supported(
            parser->target_info, kind, (unsigned int)level)) {
        minic_parser_error(parser, "%s level is not supported by target", spelling);
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after call-frame builtin level");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_CALL_FRAME_ADDRESS;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.call_frame_address.kind = kind;
    expression.value.call_frame_address.level = (unsigned int)level;
    if (!minic_type_pointer_to(minic_type_void(), &expression.type)) {
        minic_parser_error(parser, "cannot form call-frame builtin result type");
        return false;
    }
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_positive_infinity(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    bool is_float;

    /* SQLite 3.54.1 sqlite3Fp10Convert2() consumes __builtin_inff().
     * The existing __builtin_huge_val() is binary64; the *f form must
     * preserve binary32 storage and be promoted only by C conversions. */
    if (parser == NULL || expression_id == NULL ||
        (!minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_huge_val") &&
         !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_inf") &&
         !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_inff"))) {
        return false;
    }
    is_float = minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_inff");
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after positive infinity builtin")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "positive infinity builtin takes no arguments");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_FLOATING;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = is_float ? minic_type_float() : minic_type_double();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.floating_bits = is_float
        ? UINT64_C(0x7f800000) : UINT64_C(0x7ff0000000000000);
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_alloca(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicExpressionId size_id;
    const MinicExpression *size_expression;
    MinicSourcePosition begin;
    MinicSourcePosition end;

    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_alloca")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_alloca") ||
        !minic_parser_expression_parse_internal(parser, &size_id, 0U, true)) {
        return false;
    }
    size_expression = minic_c0_program_expression(parser->program, size_id);
    if (size_expression == NULL || !minic_type_is_integer(size_expression->type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "__builtin_alloca requires an integer byte count");
        }
        return false;
    }
    /* Core gives dynamic stack allocation one canonical byte-count type.
       Assignment compatibility alone is insufficient here: e.g. alloca(int)
       is a valid C/GNU use, but the AST verifier requires the builtin operand
       itself to carry unsigned long after semantic conversion. */
    if (!minic_type_equal(size_expression->type, minic_type_unsigned_long())) {
        MinicExpression conversion;
        MinicExpressionId source_id = size_id;
        MinicSourceSpan source_span = size_expression->span;

        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = source_span;
        conversion.type = minic_type_unsigned_long();
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = source_id;
        if (!minic_parser_add_expression(parser, &conversion, &size_id)) {
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_alloca");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_ALLOCA;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = size_id;
    if (!minic_type_pointer_to(minic_type_void(), &expression.type)) {
        minic_parser_error(parser, "cannot form __builtin_alloca result type");
        return false;
    }
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_unreachable(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;

    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_unreachable")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_unreachable")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "__builtin_unreachable takes no arguments");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_UNREACHABLE;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_unary(MinicParser *parser,
                                MinicBuiltinUnaryOperator operator_kind,
                                const char *spelling,
                                MinicExpressionId *expression_id) {
    MinicExpression conversion;
    MinicExpression expression;
    const MinicExpression *operand;
    MinicExpressionId converted_id;
    MinicExpressionId operand_id;
    MinicSourcePosition begin;
    MinicType argument_type;

    if (parser == NULL || spelling == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, spelling)) {
        return false;
    }
    switch (operator_kind) {
    case MINIC_BUILTIN_UNARY_CLZ:
    case MINIC_BUILTIN_UNARY_CTZ:
    case MINIC_BUILTIN_UNARY_POPCOUNT:
        argument_type = minic_type_unsigned_int();
        break;
    case MINIC_BUILTIN_UNARY_CLZL:
    case MINIC_BUILTIN_UNARY_CTZL:
    case MINIC_BUILTIN_UNARY_POPCOUNTL:
        argument_type = minic_type_unsigned_long();
        break;
    case MINIC_BUILTIN_UNARY_CLZLL:
    case MINIC_BUILTIN_UNARY_CTZLL:
    case MINIC_BUILTIN_UNARY_POPCOUNTLL:
        argument_type = minic_type_unsigned_long_long();
        break;
    case MINIC_BUILTIN_UNARY_FFSLL:
        argument_type = minic_type_long_long();
        break;
    case MINIC_BUILTIN_UNARY_ISDIGIT:
        argument_type = minic_type_int();
        break;
    case MINIC_BUILTIN_UNARY_BSWAP16:
        argument_type = minic_type_unsigned_short();
        break;
    case MINIC_BUILTIN_UNARY_BSWAP32:
        argument_type = minic_type_unsigned_int();
        break;
    case MINIC_BUILTIN_UNARY_BSWAP64:
        argument_type = minic_type_unsigned_long_long();
        break;
    default:
        return false;
    }

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after unary builtin") ||
        !minic_parser_expression_parse_internal(parser, &operand_id, 0U, true)) {
        return false;
    }
    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand == NULL || !minic_type_is_integer(operand->type)) {
        minic_parser_error(parser, "unary builtin requires an integer operand");
        return false;
    }
    if (!minic_type_equal(operand->type, argument_type)) {
        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = operand->span;
        conversion.type = argument_type;
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = operand_id;
        if (!minic_parser_add_expression(parser, &conversion, &converted_id)) {
            return false;
        }
        operand_id = converted_id;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after unary builtin operand");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_UNARY;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    /* GCC bswap builtins return uint16_t/uint32_t/uint64_t respectively;
     * all existing bit-count/isdigit builtins return int. */
    expression.type =
        operator_kind == MINIC_BUILTIN_UNARY_BSWAP16 ||
        operator_kind == MINIC_BUILTIN_UNARY_BSWAP32 ||
        operator_kind == MINIC_BUILTIN_UNARY_BSWAP64
            ? argument_type : minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.builtin_unary.operator_kind = operator_kind;
    expression.value.builtin_unary.operand = operand_id;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

/* Floating classification is not an external function declaration.
 * Keep the operand as a single AST child so side effects occur exactly
 * once even though the lowering compares the resulting value to itself. */
static bool parse_builtin_isnan(MinicParser *parser,
                                MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicExpressionId operand_id;
    const MinicExpression *operand;
    MinicSourcePosition begin;
    if (parser == NULL || expression_id == NULL ||
        !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_isnan"))
        return false;
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN,
                             "expected '(' after __builtin_isnan") ||
        !minic_parser_expression_parse_internal(parser, &operand_id, 0U, true))
        return false;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_isnan operand");
        return false;
    }
    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand == NULL ||
        (!minic_type_is_float(operand->type) &&
         !minic_type_is_double(operand->type))) {
        minic_parser_error(parser,
                           "__builtin_isnan requires float or double operand");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_UNARY;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.builtin_unary.operator_kind = MINIC_BUILTIN_UNARY_ISNAN;
    expression.value.builtin_unary.operand = operand_id;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool normalize_overflow_integer_operand(MinicParser *parser,
                                               MinicExpressionId *operand_id,
                                               MinicType result_type) {
    const MinicExpression *operand;
    MinicConstValue constant;
    MinicExpression conversion;
    MinicExpressionId source_id;
    MinicSourceSpan span;

    if (parser == NULL || operand_id == NULL) {
        return false;
    }
    source_id = *operand_id;
    operand = minic_c0_program_expression(parser->program, source_id);
    if (operand == NULL || !minic_type_is_integer(operand->type)) {
        return false;
    }
    if (minic_c0_integer_range_representable_in_type(
            parser->program, parser->target_info, operand->type, result_type)) {
        return true;
    }
    if (!minic_const_eval_integer(parser->program, parser->target_info, source_id, &constant) ||
        !minic_const_value_integer_representable_in_type(
            parser->program, parser->target_info, &constant, result_type)) {
        /* GNU overflow builtins operate in infinite precision and only then
           compare/store through the result pointer type.  A dynamic wider
           operand therefore must retain its original type through Core. */
        return true;
    }

    span = operand->span;
    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span = span;
    conversion.type = result_type;
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = source_id;
    return minic_parser_add_expression(parser, &conversion, operand_id);
}

static bool parse_builtin_overflow_typed(MinicParser *parser,
                                         MinicOverflowOperator operator_kind,
                                         const MinicType *fixed_type,
                                         MinicExpressionId *expression_id) {
    MinicExpression expression;
    const MinicExpression *left;
    const MinicExpression *right;
    const MinicExpression *result_pointer;
    MinicExpressionId left_id;
    MinicExpressionId right_id;
    MinicExpressionId result_pointer_id;
    MinicSourcePosition begin;
    MinicType result_type;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after overflow builtin") ||
        !minic_parser_expression_parse_internal(parser, &left_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected first ',' in overflow builtin") ||
        !minic_parser_expression_parse_internal(parser, &right_id, 0U, true) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_COMMA, "expected second ',' in overflow builtin") ||
        !minic_parser_expression_parse_internal(parser, &result_pointer_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after overflow builtin")) {
        return false;
    }

    left = minic_c0_program_expression(parser->program, left_id);
    right = minic_c0_program_expression(parser->program, right_id);
    result_pointer = minic_c0_program_expression(parser->program, result_pointer_id);
    if (left == NULL || right == NULL || result_pointer == NULL ||
        !minic_type_pointee(result_pointer->type, &result_type) ||
        !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type)) {
        minic_parser_error(parser,
                           "overflow builtin requires integer operands and a non-bool integer "
                           "result pointee");
        return false;
    }
    /* Typed GNU overflow builtins operate in the named signed/unsigned
     * integer type; do not accidentally route a wider operand through the
     * arbitrary-precision generic builtin. Only the precisely typed operands
     * are admitted until cross-width conversion is properly qualified. */
    if (fixed_type != NULL &&
        (!minic_type_equal(result_type, *fixed_type) ||
         !minic_type_equal(left->type, *fixed_type) ||
         !minic_type_equal(right->type, *fixed_type))) {
        minic_parser_error(parser,
                           "typed overflow builtin requires operands and result pointer "
                           "of its exact integer type");
        return false;
    }
    {
        MinicSourcePosition result_end = result_pointer->span.end;

        if (!normalize_overflow_integer_operand(parser, &left_id, result_type) ||
            !normalize_overflow_integer_operand(parser, &right_id, result_type)) {
            minic_parser_error(
                parser, "overflow builtin operand is not representable by the result pointee type");
            return false;
        }
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_BUILTIN_OVERFLOW;
        expression.span.begin = begin;
        expression.span.end = result_end;
    }
    expression.type = minic_type_bool();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.overflow.operator_kind = operator_kind;
    expression.value.overflow.left = left_id;
    expression.value.overflow.right = right_id;
    expression.value.overflow.result_pointer = result_pointer_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

/* Generic overflow is infinite-precision; typed GNU variants use an exact
 * target integer type. Both lower through the same Core overflow instruction. */
static bool parse_builtin_overflow(MinicParser *parser,
                                   MinicOverflowOperator operator_kind,
                                   MinicExpressionId *expression_id) {
    return parse_builtin_overflow_typed(parser, operator_kind, NULL, expression_id);
}

/* GCC's *_overflow_p third operand selects the destination integer type,
 * without being evaluated.  Preserve its AST id as type-only metadata: Core
 * may never lower or execute the expression. */
static bool parse_builtin_overflow_predicate(MinicParser *parser,
                                             MinicOverflowOperator operator_kind,
                                             MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicExpressionId left_id, right_id, result_id;
    const MinicExpression *left, *right, *result;
    MinicSourcePosition begin;
    MinicType result_type;
    if (parser == NULL || expression_id == NULL) return false;
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after overflow predicate") ||
        !minic_parser_expression_parse_internal(parser, &left_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' after first operand") ||
        !minic_parser_expression_parse_internal(parser, &right_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' after second operand") ||
        !minic_parser_expression_parse_internal(parser, &result_id, 0U, true) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after overflow predicate"))
        return false;
    left = minic_c0_program_expression(parser->program, left_id);
    right = minic_c0_program_expression(parser->program, right_id);
    result = minic_c0_program_expression(parser->program, result_id);
    if (left == NULL || right == NULL || result == NULL ||
        !minic_type_is_integer(left->type) || !minic_type_is_integer(right->type) ||
        !minic_type_is_integer(result->type) || minic_type_is_bool_integer(result->type) ||
        !minic_type_unqualified(result->type, &result_type) ||
        !normalize_overflow_integer_operand(parser, &left_id, result_type) ||
        !normalize_overflow_integer_operand(parser, &right_id, result_type)) {
        minic_parser_error(parser, "overflow predicate requires three integer operands");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_OVERFLOW_P;
    expression.span.begin = begin;
    expression.span.end = result->span.end;
    expression.type = minic_type_bool();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.overflow.operator_kind = operator_kind;
    expression.value.overflow.left = left_id;
    expression.value.overflow.right = right_id;
    expression.value.overflow.result_pointer = result_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_fixed_width_overflow(MinicParser *parser,
                                       MinicExpressionId *expression_id,
                                       bool *matched) {
    static const char *const names[] = {
        "__builtin_sadd_overflow", "__builtin_uadd_overflow",
        "__builtin_saddl_overflow", "__builtin_uaddl_overflow",
        "__builtin_saddll_overflow", "__builtin_uaddll_overflow",
        "__builtin_ssub_overflow", "__builtin_usub_overflow",
        "__builtin_ssubl_overflow", "__builtin_usubl_overflow",
        "__builtin_ssubll_overflow", "__builtin_usubll_overflow",
        "__builtin_smul_overflow", "__builtin_umul_overflow",
        "__builtin_smull_overflow", "__builtin_umull_overflow",
        "__builtin_smulll_overflow", "__builtin_umulll_overflow"
    };
    size_t i;
    *matched = false;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        MinicOverflowOperator kind;
        MinicType type;
        if (!minic_parser_current_identifier_lexeme_equals_cstr(parser, names[i]))
            continue;
        kind = i < 6U ? MINIC_OVERFLOW_ADD
             : i < 12U ? MINIC_OVERFLOW_SUBTRACT
             : MINIC_OVERFLOW_MULTIPLY;
        switch (i % 6U) {
        case 0U: type = minic_type_int(); break;
        case 1U: type = minic_type_unsigned_int(); break;
        case 2U: type = minic_type_long(); break;
        case 3U: type = minic_type_unsigned_long(); break;
        case 4U: type = minic_type_long_long(); break;
        default: type = minic_type_unsigned_long_long(); break;
        }
        *matched = true;
        return parse_builtin_overflow_typed(parser, kind, &type, expression_id);
    }
    return true;
}

static bool parse_builtin_va_list_target(MinicParser *parser,
                                         MinicExpressionId *target_id,
                                         MinicSourceSpan *span) {
    const MinicExpression *target;

    if (parser == NULL || target_id == NULL || span == NULL) {
        return false;
    }
    *span = parser->current.span;
    if (!minic_parser_expression_parse_internal(parser, target_id, 0U, false)) {
        return false;
    }
    target = minic_c0_program_expression(parser->program, *target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(target->type) || minic_type_is_const(target->type)) {
        minic_parser_error(parser, "GNU va builtin requires a modifiable va_list lvalue");
        return false;
    }
    return true;
}

static bool parse_builtin_va_start(MinicParser *parser, MinicExpressionId *expression_id) {
    const MinicFunction *function;
    MinicExpression expression;
    MinicExpressionId target_id;
    MinicLocalId last_parameter_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicSourceSpan target_span;

    function = parser != NULL ? minic_c0_program_function(parser->program, parser->current_function)
                              : NULL;
    if (parser == NULL || expression_id == NULL || function == NULL || !function->is_variadic ||
        function->parameter_count == 0U) {
        if (parser != NULL) {
            minic_parser_error(parser, "__builtin_va_start requires a variadic function");
        }
        return false;
    }

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_va_start") ||
        !parse_builtin_va_list_target(parser, &target_id, &target_span) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_va_start")) {
        return false;
    }
    (void)target_span;

    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "__builtin_va_start requires the last named parameter");
        return false;
    }
    last_parameter_id = minic_parser_find_local(parser, parser->current.span);
    if (last_parameter_id == MINIC_LOCAL_INVALID ||
        last_parameter_id != parser->local_begin + function->parameter_count - 1U) {
        minic_parser_error(parser,
                           "__builtin_va_start second argument must be the last named parameter");
        return false;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_va_start");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_VA_START;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = target_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_va_copy(MinicParser *parser, MinicExpressionId *expression_id) {
    const MinicExpression *source;
    const MinicExpression *target;
    MinicExpression expression;
    MinicExpressionId source_id;
    MinicExpressionId target_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicSourceSpan target_span;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_va_copy") ||
        !parse_builtin_va_list_target(parser, &target_id, &target_span) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_va_copy") ||
        !minic_parser_expression_parse_internal(parser, &source_id, 0U, false)) {
        return false;
    }
    (void)target_span;

    target = minic_c0_program_expression(parser->program, target_id);
    source = minic_c0_program_expression(parser->program, source_id);
    if (target == NULL || source == NULL || source->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_pointer(source->type)) {
        minic_parser_error(parser, "__builtin_va_copy source must be a va_list lvalue");
        return false;
    }
    if (!minic_c0_types_compatible(parser->program, target->type, source->type)) {
        minic_parser_error(parser, "__builtin_va_copy source type must match destination type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_va_copy");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_VA_COPY;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.binary.left = target_id;
    expression.value.binary.right = source_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_va_end(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicExpressionId target_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicSourceSpan target_span;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_va_end") ||
        !parse_builtin_va_list_target(parser, &target_id, &target_span)) {
        return false;
    }
    (void)target_span;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_va_end");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_VA_END;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_void();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = target_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_va_arg(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicExpressionId target_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicSourceSpan target_span;
    MinicType value_type;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_va_arg") ||
        !parse_builtin_va_list_target(parser, &target_id, &target_span) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_va_arg") ||
        !minic_parser_parse_type_name(parser, &value_type)) {
        return false;
    }
    (void)target_span;
    if ((!minic_type_is_integer(value_type) && !minic_type_is_pointer(value_type) &&
         !minic_type_is_double(value_type) && !minic_type_is_record(value_type)) ||
        minic_type_is_bool_integer(value_type)) {
        minic_parser_error(
            parser, "__builtin_va_arg requires an integer, pointer, double, or record type");
        return false;
    }
    if (minic_type_is_record(value_type) &&
        !minic_parser_require_complete_object_type(
            parser, value_type, "__builtin_va_arg record type must be complete")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_va_arg");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BUILTIN_VA_ARG;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = value_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = target_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

bool minic_parser_try_builtin_primary_early(
    MinicParser *parser, MinicExpressionId *expression_id,
    bool decay_array, bool *handled) {
    MinicExpressionId primary_id;
    if (parser == NULL || expression_id == NULL || handled == NULL)
        return false;
    *handled = false;

    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_va_start")) {
        if (!parse_builtin_va_start(parser, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_va_copy")) {
        if (!parse_builtin_va_copy(parser, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_va_end")) {
        if (!parse_builtin_va_end(parser, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_va_arg")) {
        if (!parse_builtin_va_arg(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_huge_val") ||
        minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_inf") ||
        minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_inff")) {
        if (!parse_builtin_positive_infinity(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_alloca")) {
        if (!parse_builtin_alloca(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_unreachable")) {
        if (!parse_builtin_unreachable(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_return_address")) {
        if (!parse_builtin_call_frame_address(
                parser, MINIC_CALL_FRAME_ADDRESS_RETURN, "__builtin_return_address", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_frame_address")) {
        if (!parse_builtin_call_frame_address(
                parser, MINIC_CALL_FRAME_ADDRESS_FRAME, "__builtin_frame_address", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_bswap16")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_BSWAP16, "__builtin_bswap16", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_bswap32")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_BSWAP32, "__builtin_bswap32", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_bswap64")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_BSWAP64, "__builtin_bswap64", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_clz")) {
        if (!parse_builtin_unary(parser, MINIC_BUILTIN_UNARY_CLZ, "__builtin_clz", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_clzl")) {
        if (!parse_builtin_unary(parser, MINIC_BUILTIN_UNARY_CLZL, "__builtin_clzl", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_clzll")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_CLZLL, "__builtin_clzll", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_ctz")) {
        if (!parse_builtin_unary(parser, MINIC_BUILTIN_UNARY_CTZ, "__builtin_ctz", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_ctzl")) {
        if (!parse_builtin_unary(parser, MINIC_BUILTIN_UNARY_CTZL, "__builtin_ctzl", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_ctzll")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_CTZLL, "__builtin_ctzll", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_popcount")) {
        if (!parse_builtin_unary(parser, MINIC_BUILTIN_UNARY_POPCOUNT,
                                 "__builtin_popcount", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_popcountl")) {
        if (!parse_builtin_unary(parser, MINIC_BUILTIN_UNARY_POPCOUNTL,
                                 "__builtin_popcountl", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_popcountll")) {
        if (!parse_builtin_unary(parser, MINIC_BUILTIN_UNARY_POPCOUNTLL,
                                 "__builtin_popcountll", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_ffs")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_FFSLL, "__builtin_ffs", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_ffsl")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_FFSLL, "__builtin_ffsl", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_ffsll")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_FFSLL, "__builtin_ffsll", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_isnan")) {
        if (!parse_builtin_isnan(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id))
            return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_isdigit")) {
        if (!parse_builtin_unary(
                parser, MINIC_BUILTIN_UNARY_ISDIGIT, "__builtin_isdigit", &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_constant_p")) {
        if (!parse_builtin_constant_p(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_object_size")) {
        if (!parse_builtin_object_size(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    {
        bool fixed_overflow_handled = false;
        if (!parse_fixed_width_overflow(parser, &primary_id,
                                        &fixed_overflow_handled))
            return false;
        if (fixed_overflow_handled) {
            if (!minic_parser_parse_postfix(parser, primary_id, &primary_id))
                return false;
            *handled = true;
            return minic_parser_finish_value_expression(
                parser, primary_id, decay_array, expression_id);
        }
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_add_overflow")) {
        if (!parse_builtin_overflow(parser, MINIC_OVERFLOW_ADD, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_add_overflow_p")) {
        if (!parse_builtin_overflow_predicate(parser, MINIC_OVERFLOW_ADD, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_sub_overflow")) {
        if (!parse_builtin_overflow(parser, MINIC_OVERFLOW_SUBTRACT, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_sub_overflow_p")) {
        if (!parse_builtin_overflow_predicate(parser, MINIC_OVERFLOW_SUBTRACT, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_mul_overflow")) {
        if (!parse_builtin_overflow(parser, MINIC_OVERFLOW_MULTIPLY, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_mul_overflow_p")) {
        if (!parse_builtin_overflow_predicate(parser, MINIC_OVERFLOW_MULTIPLY, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_types_compatible_p")) {
        if (!parse_builtin_types_compatible_p(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_choose_expr")) {
        if (!parse_builtin_choose_expr(parser, &primary_id, decay_array) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "_Generic")) {
        if (!parse_generic_selection(parser, &primary_id, decay_array) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    return true;
}

/* Architecture-specific builtins reuse one typed CALL AST and CoreCallView.
 * Frontend is intentionally oblivious to the ISA/opcode/immediate rule; the
 * shared TargetInfo schema is the only admission source. */
static bool parse_target_builtin(
    MinicParser *parser, MinicExpressionId *expression_id, bool *handled) {
    char internal_name[128];
    const char *mnemonic;
    MinicType result_type, parameter_types[3];
    size_t parameter_count, immediate_index;
    MinicFunctionId function_id = MINIC_FUNCTION_INVALID;
    const MinicFunction *callee;
    MinicExpression expression;
    MinicSourcePosition begin, end;
    size_t i, name_length;
    if (parser == NULL || expression_id == NULL || handled == NULL)
        return false;
    *handled = false;
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER)
        return true;
    name_length = minic_parser_span_length(parser->current.span);
    if (!minic_target_info_machine_builtin_signature(
            parser->target_info,
            parser->source + parser->current.span.begin.offset, name_length,
            internal_name, sizeof(internal_name), &result_type,
            parameter_types, &parameter_count, &mnemonic, &immediate_index))
        return true;
    (void)mnemonic;
    (void)immediate_index;
    *handled = true;
    for (i = 0U; i < parser->program->function_count; ++i) {
        const MinicFunction *candidate =
            minic_c0_program_function(parser->program, i);
        size_t p;
        if (candidate == NULL || candidate->name == NULL ||
            candidate->name_length != strlen(internal_name) ||
            memcmp(candidate->name, internal_name, candidate->name_length) != 0)
            continue;
        if (candidate->is_defined || candidate->is_variadic ||
            candidate->parameter_count != parameter_count ||
            !minic_type_equal(candidate->return_type, result_type)) {
            minic_parser_error(parser, "target builtin reserved callee conflict");
            return false;
        }
        for (p = 0U; p < parameter_count; ++p)
            if (!minic_type_equal(candidate->parameter_types[p], parameter_types[p])) {
                minic_parser_error(parser, "target builtin reserved signature conflict");
                return false;
            }
        function_id = i;
        break;
    }
    if (function_id == MINIC_FUNCTION_INVALID &&
        (!minic_c0_program_add_function(
             parser->program, internal_name, strlen(internal_name),
             parser->program->local_count, 0U, MINIC_BLOCK_INVALID, &function_id) ||
         !minic_c0_program_set_function_signature(
             parser->program, function_id, result_type,
             parameter_types, parameter_count))) {
        minic_parser_error(parser, "cannot declare target machine builtin");
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN,
                             "expected '(' after target machine builtin"))
        return false;
    callee = minic_c0_program_function(parser->program, function_id);
    if (callee == NULL || !minic_c0_program_set_function_referenced(
            parser->program, function_id, true)) {
        minic_parser_error(parser, "invalid target builtin callee");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_CALL;
    expression.span.begin = begin;
    expression.type = result_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.call.function_id = function_id;
    if (!minic_parser_expression_parse_call_arguments(parser, &expression, callee))
        return false;
    end = parser->current.span.end;
    if (!minic_parser_advance(parser))
        return false;
    expression.span.end = end;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

bool minic_parser_try_builtin_primary_late(
    MinicParser *parser, MinicExpressionId *expression_id,
    bool decay_array, bool *handled) {
    MinicExpressionId primary_id;
    if (parser == NULL || expression_id == NULL || handled == NULL)
        return false;
    *handled = false;
    if (!parse_target_builtin(parser, &primary_id, handled))
        return false;
    if (*handled) {
        if (!minic_parser_parse_postfix(parser, primary_id, &primary_id))
            return false;
        return minic_parser_finish_value_expression(
            parser, primary_id, decay_array, expression_id);
    }

    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__atomic_load_n")) {
        if (!parse_builtin_atomic_load(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id))
            return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__atomic_store_n")) {
        if (!parse_builtin_atomic_store(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id))
            return false;
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    /* Memory-library builtins share canonical signature and call-AST lowering. */
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_memcpy") ||
        minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_memmove") ||
        minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_memset")) {
        if (!minic_parser_parse_builtin_memory(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_extract_return_addr")) {
        if (!parse_builtin_extract_return_addr(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_expect")) {
        if (!parse_builtin_expect(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "__builtin_prefetch")) {
        if (!parse_builtin_prefetch(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        *handled = true;
        return minic_parser_finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
        current_is_builtin_offsetof(parser)) {
        *handled = true;
        return parse_builtin_offsetof(parser, expression_id);
    }
    return true;
}
