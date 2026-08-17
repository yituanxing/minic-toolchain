from pathlib import Path

path = Path('src/frontend/parser_expression.c')
text = path.read_text()
old = '''static bool parse_alignof(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourceSpan span;
    int64_t alignment;

    if (!minic_parser_parse_alignof_type_value(parser, &alignment, &span)) {
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = span;
    expression.type = minic_type_unsigned_long();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = alignment;
    return minic_parser_add_expression(parser, &expression, expression_id);
}
'''
new = '''static bool parse_alignof(MinicParser *parser, MinicExpressionId *expression_id) {
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
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else {
        MinicExpressionId operand_id;
        const MinicExpression *operand;

        if (!parse_unary(parser, &operand_id, false)) {
            return false;
        }
        operand = minic_c0_program_expression(parser->program, operand_id);
        if (operand == NULL) {
            minic_parser_error(parser, "invalid alignof operand");
            return false;
        }
        measured_type = operand->type;
        if (minic_c0_expression_array_object_info(parser->program, operand, NULL) &&
            !minic_parser_materialize_array_object_type(parser, operand_id, &measured_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot preserve array object type for alignof");
            }
            return false;
        }
        end = operand->span.end;
    }

    if (!minic_data_layout_type(minic_target_info_data_layout(parser->target_info),
                                parser->program,
                                measured_type,
                                &measured_size,
                                &measured_alignment) ||
        measured_alignment > (size_t)INT64_MAX) {
        minic_parser_error(parser, "alignof requires a complete object type");
        return false;
    }
    (void)measured_size;

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
if text.count(old) != 1:
    raise SystemExit(f'alignof anchor mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))
