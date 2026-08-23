from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M70_ARRAY_LVALUE_POINTER_DECAY'
if marker in text:
    print('M70 array lvalue pointer decay already applied')
    raise SystemExit(0)

anchor = '''    if (minic_type_is_integer(target_type) && minic_type_is_integer(expression->type)) {
        return lower_integer_assignment_value(context, target_type, expression_id, value_id);
    }

    status = lower_expression(context, expression_id, &source_value);
'''
replacement = '''    if (minic_type_is_integer(target_type) && minic_type_is_integer(expression->type)) {
        return lower_integer_assignment_value(context, target_type, expression_id, value_id);
    }

    /* M70_ARRAY_LVALUE_POINTER_DECAY: C array arguments/assignments decay to
       a pointer to their first element. Core already owns address formation for
       addressable arrays (including record array members); materialize that
       address and reinterpret pointer-to-array as the assignment-compatible
       pointer value instead of asking scalar lowering to load an array. */
    if (minic_type_is_pointer(target_type) && minic_type_is_array(expression->type)) {
        MinicCoreValueId array_address;
        MinicType array_pointer_type;

        if (expression->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_pointer_to(expression->type, &array_pointer_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_address(context, expression_id, &array_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (array_address >= context->function->value_count ||
            !minic_type_equal(context->function->values[array_address].type,
                              array_pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return append_scalar_bitcast(
            context, expression->span, target_type, array_address, value_id);
    }

    status = lower_expression(context, expression_id, &source_value);
'''
if text.count(anchor) != 1:
    raise SystemExit(f'M70 anchor count={text.count(anchor)}')
path.write_text(text.replace(anchor, replacement, 1))
print('M70 array lvalue pointer decay applied')
