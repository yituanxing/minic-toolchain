#include "frontend/parser_internal.h"

#include <string.h>

static void search_record_field_path(const MinicParser *parser,
                                     const MinicRecord *record,
                                     MinicSourceSpan name_span,
                                     MinicRecordId *record_stack,
                                     size_t *field_stack,
                                     size_t depth,
                                     MinicRecordFieldPath *result) {
    size_t name_length;
    size_t index;

    if (parser == NULL || record == NULL || result == NULL || result->ambiguous ||
        depth >= MINIC_RECORD_MEMBER_MAX_DEPTH) {
        return;
    }
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, index);
        if (field == NULL) {
            continue;
        }
        record_stack[depth] = (MinicRecordId)(record - parser->program->records);
        field_stack[depth] = index;
        if (!field->is_anonymous_member && field->name_length == name_length &&
            memcmp(field->name, parser->source + name_span.begin.offset, name_length) == 0) {
            if (result->found) {
                result->ambiguous = true;
                return;
            }
            result->depth = depth + 1U;
            (void)memcpy(
                result->record_ids, record_stack, result->depth * sizeof(result->record_ids[0]));
            (void)memcpy(result->field_indices,
                         field_stack,
                         result->depth * sizeof(result->field_indices[0]));
            result->found = true;
            continue;
        }
        if (field->is_anonymous_member && minic_type_is_record(field->type)) {
            const MinicRecord *nested;

            nested = minic_c0_program_record(parser->program, field->type.record_id);
            if (nested != NULL && nested->is_complete) {
                search_record_field_path(
                    parser, nested, name_span, record_stack, field_stack, depth + 1U, result);
            }
        }
    }
}

bool minic_parser_find_record_field_path(const MinicParser *parser,
                                         const MinicRecord *record,
                                         MinicSourceSpan name_span,
                                         MinicRecordFieldPath *result) {
    MinicRecordId record_stack[MINIC_RECORD_MEMBER_MAX_DEPTH];
    size_t field_stack[MINIC_RECORD_MEMBER_MAX_DEPTH];

    if (parser == NULL || record == NULL || result == NULL) {
        return false;
    }
    (void)memset(result, 0, sizeof(*result));
    (void)memset(record_stack, 0, sizeof(record_stack));
    (void)memset(field_stack, 0, sizeof(field_stack));
    search_record_field_path(parser, record, name_span, record_stack, field_stack, 0U, result);
    return result->found && !result->ambiguous;
}

static bool add_pointer_record_field(MinicParser *parser,
                                     MinicExpressionId pointer_base_id,
                                     MinicRecordId record_id,
                                     size_t field_index,
                                     MinicSourcePosition member_begin,
                                     MinicSourceSpan field_span,
                                     MinicExpressionId *expression_id) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicType record_type;
    MinicType member_type;
    MinicExpression member_expression;

    base = minic_c0_program_expression(parser->program, pointer_base_id);
    record = minic_c0_program_record(parser->program, record_id);
    field = minic_c0_record_field(record, field_index);
    if (base == NULL || record == NULL || field == NULL ||
        !minic_type_pointee(base->type, &record_type) || !minic_type_is_record(record_type) ||
        record_type.record_id != record_id) {
        return false;
    }
    member_type = field->type;
    if (minic_type_is_const(record_type) && !minic_type_add_const(member_type, &member_type)) {
        minic_parser_error(parser, "cannot propagate const to record member");
        return false;
    }

    (void)memset(&member_expression, 0, sizeof(member_expression));
    member_expression.kind = MINIC_EXPRESSION_MEMBER;
    member_expression.span.begin = member_begin;
    member_expression.span.end = field_span.end;
    member_expression.value.member.base = pointer_base_id;
    member_expression.value.member.record_id = record_id;
    member_expression.value.member.field_index = field_index;
    if (field->is_array) {
        if (!minic_type_pointer_to(member_type, &member_expression.type)) {
            minic_parser_error(parser, "record array member pointer depth is unsupported");
            return false;
        }
        member_expression.value_category = MINIC_VALUE_RVALUE;
    } else {
        member_expression.type = member_type;
        member_expression.value_category = MINIC_VALUE_LVALUE;
    }
    return minic_parser_add_expression(parser, &member_expression, expression_id);
}

static bool parse_pointer_record_member(MinicParser *parser,
                                        MinicExpressionId pointer_base_id,
                                        MinicSourcePosition member_begin,
                                        MinicExpressionId *expression_id) {
    const MinicExpression *base;
    const MinicRecord *record;
    MinicType record_type;
    MinicSourceSpan field_span;
    MinicRecordFieldPath path;
    MinicExpressionId current_pointer_id;
    size_t path_index;

    base = minic_c0_program_expression(parser->program, pointer_base_id);
    if (base == NULL || !minic_type_pointee(base->type, &record_type) ||
        !minic_type_is_record(record_type)) {
        minic_parser_error(parser, "pointer member access requires a pointer to record");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "member access requires a complete record");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record member name");
        return false;
    }

    field_span = parser->current.span;
    if (!minic_parser_find_record_field_path(parser, record, field_span, &path)) {
        minic_parser_error(parser,
                           path.ambiguous ? "record member is ambiguous through anonymous members"
                                          : "record has no such member");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    current_pointer_id = pointer_base_id;
    for (path_index = 0U; path_index < path.depth; ++path_index) {
        MinicExpressionId member_id;

        if (!add_pointer_record_field(parser,
                                      current_pointer_id,
                                      path.record_ids[path_index],
                                      path.field_indices[path_index],
                                      member_begin,
                                      field_span,
                                      &member_id)) {
            return false;
        }
        if (path_index + 1U == path.depth) {
            *expression_id = member_id;
            return true;
        }
        {
            const MinicExpression *nested_member;
            MinicExpression address;

            nested_member = minic_c0_program_expression(parser->program, member_id);
            if (nested_member == NULL || nested_member->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_is_record(nested_member->type)) {
                minic_parser_error(parser, "anonymous member path does not contain a record");
                return false;
            }
            (void)memset(&address, 0, sizeof(address));
            address.kind = MINIC_EXPRESSION_ADDRESS_OF;
            address.span = nested_member->span;
            if (!minic_type_pointer_to(nested_member->type, &address.type)) {
                minic_parser_error(parser, "anonymous member pointer depth is unsupported");
                return false;
            }
            address.value_category = MINIC_VALUE_RVALUE;
            address.value.unary.operand = member_id;
            if (!minic_parser_add_expression(parser, &address, &current_pointer_id)) {
                return false;
            }
        }
    }
    minic_parser_error(parser, "empty anonymous member path");
    return false;
}

bool minic_parser_parse_pointer_member(MinicParser *parser,
                                       MinicExpressionId base_id,
                                       MinicExpressionId *expression_id) {
    const MinicExpression *base;
    MinicSourcePosition member_begin;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL) {
        minic_parser_error(parser, "invalid member base");
        return false;
    }
    member_begin = base->span.begin;
    if (!minic_parser_expect(parser, MINIC_TOKEN_ARROW, "expected '->'")) {
        return false;
    }
    return parse_pointer_record_member(parser, base_id, member_begin, expression_id);
}

bool minic_parser_parse_direct_member(MinicParser *parser,
                                      MinicExpressionId base_id,
                                      MinicExpressionId *expression_id) {
    const MinicExpression *base;
    MinicExpression address;
    MinicExpressionId address_id;
    MinicSourcePosition member_begin;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || base->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(base->type)) {
        minic_parser_error(parser, "direct member access requires a record lvalue");
        return false;
    }
    member_begin = base->span.begin;
    if (!minic_parser_expect(parser, MINIC_TOKEN_DOT, "expected '.'")) {
        return false;
    }

    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = base->span;
    if (!minic_type_pointer_to(base->type, &address.type)) {
        minic_parser_error(parser, "direct member address depth is unsupported");
        return false;
    }
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = base_id;
    if (!minic_parser_add_expression(parser, &address, &address_id)) {
        return false;
    }
    return parse_pointer_record_member(parser, address_id, member_begin, expression_id);
}
