from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
old = '''static bool add_local_array_element_assignment(MinicParser *parser,
                                               MinicLocalId local_id,
                                               size_t index,
                                               MinicExpressionId value_id) {
    const MinicLocal *local;
    MinicExpressionId base_id;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL || !local->is_array) {
        minic_parser_error(parser, "invalid local array initializer element");
        return false;
    }
    if (!add_local_lvalue_expression(parser, local_id, local->name_span, &base_id)) {
        return false;
    }
    return add_array_object_element_assignment(parser, base_id, index, value_id);
}
'''
new = '''static bool add_local_array_element_assignment(MinicParser *parser,
                                               MinicLocalId local_id,
                                               size_t index,
                                               MinicExpressionId value_id) {
    const MinicLocal *local;
    const MinicExpression *value;
    MinicExpressionId base_id;

    local = minic_c0_program_local(parser->program, local_id);
    value = minic_c0_program_expression(parser->program, value_id);
    if (local == NULL || value == NULL || !local->is_array) {
        minic_parser_error(parser, "invalid local array initializer element");
        return false;
    }
    if (!apply_assignment_conversion(parser, local->type, &value_id) ||
        !minic_c0_assignment_compatible(parser->program, local->type, value_id)) {
        minic_parser_error(parser,
                           "local array initializer element type does not match element type");
        return false;
    }
    if (!add_local_lvalue_expression(parser, local_id, local->name_span, &base_id)) {
        return false;
    }
    return add_array_object_element_assignment(parser, base_id, index, value_id);
}
'''
if text.count(old) != 1:
    raise SystemExit(f"compat anchor mismatch: {text.count(old)}")
path.write_text(text.replace(old, new, 1))
