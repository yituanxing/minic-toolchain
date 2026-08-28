from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M63_QUALIFIED_SCALAR_UPDATE_VALUE'
if marker in text:
    print('M63 qualified scalar update value already applied')
    raise SystemExit(0)

anchor = '''    MinicCoreLowerStatus status;
    MinicType stored_type;
    bool increment;
    bool prefix;
'''
replacement = '''    MinicCoreLowerStatus status;
    MinicType expression_value_type;
    MinicType stored_type;
    bool increment;
    bool prefix;
'''
# This declaration block belongs to lower_scalar_update; there may be only one.
if text.count(anchor) != 1:
    raise SystemExit(f'M63 declaration anchor count={text.count(anchor)}')
text = text.replace(anchor, replacement, 1)

anchor = '''    operand = minic_c0_program_expression(context->body->program, expression->value.unary.operand);
    if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
        !core_memory_scalar_type(operand->type) || minic_type_is_const(operand->type) ||
        !minic_type_unqualified(operand->type, &stored_type) ||
        !minic_type_equal(expression->type, stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
replacement = '''    operand = minic_c0_program_expression(context->body->program, expression->value.unary.operand);
    /* M63_QUALIFIED_SCALAR_UPDATE_VALUE: the memory operand may be qualified
       (notably volatile), while the computed prefix/postfix value transported
       by Core is an ordinary unqualified scalar. Preserve qualifiers solely on
       the load/store effects and compare the expression's value type after
       unqualification. */
    if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
        !core_memory_scalar_type(operand->type) || minic_type_is_const(operand->type) ||
        !minic_type_unqualified(operand->type, &stored_type) ||
        !minic_type_unqualified(expression->type, &expression_value_type) ||
        !minic_type_equal(expression_value_type, stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
if text.count(anchor) != 1:
    raise SystemExit(f'M63 guard anchor count={text.count(anchor)}')
text = text.replace(anchor, replacement, 1)
path.write_text(text)
print('M63 qualified scalar update value applied')
