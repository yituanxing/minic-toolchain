from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()

marker = 'M71_LEGACY_ARRAY_OBJECT_SUBSCRIPT'
if marker in text:
    print('M71 legacy array object subscript already applied')
    raise SystemExit(0)

anchor = '''        if (array_base) {
            if (!array_info.has_materialized_type || !minic_type_is_array(base->type) ||
                !minic_type_equal(array_info.element_type, expression->type) ||
                !minic_type_pointer_to(array_info.element_type, &pointer_type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          minic_default_data_layout(),
                                                          pointer_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            subscript_status =
                lower_address(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
            if (base_value >= context->function->value_count ||
                !minic_type_pointer_to(base->type, &array_pointer_type) ||
                !minic_type_equal(context->function->values[base_value].type,
                                  array_pointer_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            subscript_status = append_scalar_bitcast(
                context, base->span, pointer_type, base_value, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
        } else {
'''

replacement = '''        if (array_base) {
            /* M71_LEGACY_ARRAY_OBJECT_SUBSCRIPT: array-object metadata has two
               valid frontend representations while array type convergence is
               still in progress. Materialized arrays carry an array MinicType;
               legacy local/member arrays carry the element type plus explicit
               array-object metadata. Both denote the same C array object and
               must form the address of element zero without loading the array. */
            if (!minic_type_equal(array_info.element_type, expression->type) ||
                !minic_type_pointer_to(array_info.element_type, &pointer_type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          minic_default_data_layout(),
                                                          pointer_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            subscript_status =
                lower_address(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
            if (base_value >= context->function->value_count) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (array_info.has_materialized_type) {
                if (!minic_type_is_array(base->type) ||
                    !minic_type_pointer_to(base->type, &array_pointer_type) ||
                    !minic_type_equal(context->function->values[base_value].type,
                                      array_pointer_type)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                subscript_status = append_scalar_bitcast(
                    context, base->span, pointer_type, base_value, &base_value);
                if (subscript_status != MINIC_CORE_LOWER_OK) {
                    return subscript_status;
                }
            } else if (minic_type_is_array(base->type) ||
                       !minic_type_equal(base->type, array_info.element_type) ||
                       !minic_type_equal(context->function->values[base_value].type,
                                         pointer_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        } else {
'''

count = text.count(anchor)
if count != 1:
    raise SystemExit(f'M71 anchor count={count}')

path.write_text(text.replace(anchor, replacement, 1))
print('M71 legacy array object subscript applied')
