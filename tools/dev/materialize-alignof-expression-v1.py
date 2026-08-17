from pathlib import Path

header = Path('src/target/data_layout.h')
text = header.read_text()
anchor = '''bool minic_data_layout_record_field_layout(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset,
                                           size_t *bit_offset);
'''
addition = '''bool minic_data_layout_record_field_alignment(const MinicDataLayout *layout,
                                              const MinicC0Program *program,
                                              const MinicRecord *record,
                                              size_t field_index,
                                              size_t *alignment);
''' + anchor
if text.count(anchor) != 1:
    raise SystemExit('data_layout.h field-layout anchor changed')
header.write_text(text.replace(anchor, addition, 1))

source = Path('src/target/data_layout.c')
text = source.read_text()
anchor = '''bool minic_data_layout_record_field_layout(const MinicDataLayout *layout,
                                           const MinicC0Program *program,
                                           const MinicRecord *record,
                                           size_t field_index,
                                           size_t *offset,
                                           size_t *bit_offset) {
'''
helper = '''bool minic_data_layout_record_field_alignment(const MinicDataLayout *layout,
                                              const MinicC0Program *program,
                                              const MinicRecord *record,
                                              size_t field_index,
                                              size_t *alignment) {
    const MinicRecordField *field;
    size_t field_size;
    size_t field_alignment;

    if (layout == NULL || program == NULL || record == NULL || alignment == NULL ||
        field_index >= record->field_count) {
        return false;
    }
    field = &record->fields[field_index];
    if (field->is_bit_field ||
        !minic_data_layout_type(layout, program, field->type, &field_size, &field_alignment)) {
        return false;
    }
    (void)field_size;
    if (field->is_packed || (record->is_packed && field->explicit_alignment == 0U)) {
        field_alignment = 1U;
    }
    if (field->explicit_alignment != 0U) {
        if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U) {
            return false;
        }
        if (field->explicit_alignment > field_alignment) {
            field_alignment = field->explicit_alignment;
        }
    }
    *alignment = field_alignment;
    return true;
}

''' + anchor
if text.count(anchor) != 1:
    raise SystemExit('data_layout.c field-layout anchor changed')
source.write_text(text.replace(anchor, helper, 1))

path = Path('src/frontend/parser_expression.c')
text = path.read_text()
start_marker = 'static bool parse_alignof(MinicParser *parser, MinicExpressionId *expression_id) {'
end_marker = 'static bool parse_sizeof(MinicParser *parser, MinicExpressionId *expression_id) {'
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0 or end <= start:
    raise SystemExit('parse_alignof function boundary changed')
new = r'''static bool alignof_expression_alignment(MinicParser *parser,
                                         MinicExpressionId operand_id,
                                         const MinicExpression *operand,
                                         size_t *alignment) {
    const MinicDataLayout *layout;
    MinicArrayObjectInfo array_info;
    size_t size;

    if (parser == NULL || operand == NULL || alignment == NULL) {
        return false;
    }
    layout = minic_target_info_data_layout(parser->target_info);
    if (operand->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicRecord *record;

        record = minic_c0_program_record(parser->program, operand->value.member.record_id);
        return minic_data_layout_record_field_alignment(layout,
                                                        parser->program,
                                                        record,
                                                        operand->value.member.field_index,
                                                        alignment);
    }
    if (operand->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, operand->value.global_object_id);
        if (object == NULL ||
            !minic_data_layout_global_object(layout, parser->program, object, &size, alignment) ||
            *alignment == 0U) {
            return false;
        }
        return true;
    }
    if (minic_c0_expression_array_object_info(parser->program, operand, &array_info)) {
        return minic_data_layout_type(
            layout, parser->program, array_info.element_type, &size, alignment);
    }
    (void)operand_id;
    return minic_data_layout_type(layout, parser->program, operand->type, &size, alignment);
}

static bool parse_alignof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicType measured_type;
    size_t measured_alignment;
    size_t measured_size;

    if (parser == NULL || expression_id == NULL || !current_is_alignof(parser)) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_LPAREN && parenthesis_starts_cast(parser)) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_type_name(parser, &measured_type)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_RPAREN) {
            minic_parser_error(parser, "expected ')' after alignof type");
            return false;
        }
        end = parser->current.span.end;
        if (!minic_parser_advance(parser) ||
            !minic_data_layout_type(minic_target_info_data_layout(parser->target_info),
                                    parser->program,
                                    measured_type,
                                    &measured_size,
                                    &measured_alignment)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "alignof requires a supported type");
            }
            return false;
        }
        (void)measured_size;
    } else {
        MinicExpressionId operand_id;
        const MinicExpression *operand;

        if (!parse_unary(parser, &operand_id, false)) {
            return false;
        }
        operand = minic_c0_program_expression(parser->program, operand_id);
        if (operand == NULL ||
            !alignof_expression_alignment(parser, operand_id, operand, &measured_alignment)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "alignof requires an alignable expression");
            }
            return false;
        }
        end = operand->span.end;
    }
    if (measured_alignment > (size_t)INT64_MAX) {
        minic_parser_error(parser, "alignof result is not representable");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span.begin = begin;
    expression.span.end = end;
    expression.type = minic_type_unsigned_long();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = (int64_t)measured_alignment;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

'''
path.write_text(text[:start] + new + text[end:])
