#include "frontend/parser_internal.h"

#include <string.h>

static bool find_record_field(const MinicParser *parser,
                              const MinicRecord *record,
                              MinicSourceSpan name_span,
                              size_t *field_index) {
    size_t name_length;
    size_t index;

    if (field_index == NULL) {
        return false;
    }
    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, index);
        if (field != NULL && field->name_length == name_length &&
            memcmp(field->name, parser->source + name_span.begin.offset, name_length) == 0) {
            *field_index = index;
            return true;
        }
    }
    return false;
}

static bool parse_pointer_record_member(MinicParser *parser,
                                        MinicExpressionId pointer_base_id,
                                        MinicSourcePosition member_begin,
                                        MinicExpressionId *expression_id) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicType record_type;
    MinicType member_type;
    MinicSourceSpan field_span;
    MinicExpression member;
    size_t field_index;

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
    if (!find_record_field(parser, record, field_span, &field_index)) {
        minic_parser_error(parser, "record has no such member");
        return false;
    }
    field = minic_c0_record_field(record, field_index);
    if (field == NULL) {
        minic_parser_error(parser, "invalid record member");
        return false;
    }
    member_type = field->type;
    if (minic_type_is_const(record_type) && !minic_type_add_const(member_type, &member_type)) {
        minic_parser_error(parser, "cannot propagate const to record member");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&member, 0, sizeof(member));
    member.kind = MINIC_EXPRESSION_MEMBER;
    member.span.begin = member_begin;
    member.span.end = field_span.end;
    member.value.member.base = pointer_base_id;
    member.value.member.record_id = record_type.record_id;
    member.value.member.field_index = field_index;
    if (field->is_flexible_array || field->element_count > 1U) {
        if (!minic_type_pointer_to(member_type, &member.type)) {
            minic_parser_error(parser, "record array member pointer depth is unsupported");
            return false;
        }
        member.value_category = MINIC_VALUE_RVALUE;
    } else {
        member.type = member_type;
        member.value_category = MINIC_VALUE_LVALUE;
    }
    return minic_parser_add_expression(parser, &member, expression_id);
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
