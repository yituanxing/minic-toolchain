from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M66_SUBSCRIPT_INDEX_VALUE_TYPE'
if marker in text:
    print('M66 subscript index value type already applied')
    raise SystemExit(0)

anchor = '''        MinicType array_pointer_type;
        MinicType element_type;
        MinicType pointer_type;
        size_t element_size;
        bool array_base;

        base =
            minic_c0_program_expression(context->body->program, expression->value.subscript.base);
        index =
            minic_c0_program_expression(context->body->program, expression->value.subscript.index);
        if (base == NULL || index == NULL || !minic_type_is_integer(index->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
replacement = '''        MinicType array_pointer_type;
        MinicType element_type;
        MinicType index_value_type;
        MinicType pointer_type;
        size_t element_size;
        bool array_base;

        base =
            minic_c0_program_expression(context->body->program, expression->value.subscript.base);
        index =
            minic_c0_program_expression(context->body->program, expression->value.subscript.index);
        /* M66_SUBSCRIPT_INDEX_VALUE_TYPE: an lvalue-to-rvalue conversion strips
           top-level qualifiers. Validate the Core value against that semantic
           value type, not against the qualified lvalue type carried by the AST
           node (for example `const size_t index`). */
        if (base == NULL || index == NULL ||
            !core_scalar_expression_value_type(context->body, index, &index_value_type) ||
            !minic_type_is_integer(index_value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
if text.count(anchor) != 1:
    raise SystemExit(f'M66 declaration anchor count={text.count(anchor)}')
text = text.replace(anchor, replacement, 1)

anchor = '''        if (index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[index_value].type, index->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''
replacement = '''        if (index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[index_value].type, index_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''
if text.count(anchor) != 1:
    raise SystemExit(f'M66 validation anchor count={text.count(anchor)}')
text = text.replace(anchor, replacement, 1)
path.write_text(text)
print('M66 subscript index value type applied')
