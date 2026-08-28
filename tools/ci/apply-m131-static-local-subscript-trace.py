#!/usr/bin/env python3
from pathlib import Path

# M131 census trigger: keep semantic source untouched; this file only stages diagnostics.
path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M131_STATIC_LOCAL_SUBSCRIPT_TRACE'

if marker not in text:
    needle = '''        (void)memset(&array_info, 0, sizeof(array_info));
        array_base = minic_c0_expression_array_object_info(
            context->body->program, base, &array_info);
        if (array_base) {
'''
    if needle not in text:
        raise SystemExit('M131 subscript array-info seam changed')
    replacement = '''        (void)memset(&array_info, 0, sizeof(array_info));
        array_base = minic_c0_expression_array_object_info(
            context->body->program, base, &array_info);
        /* M131_STATIC_LOCAL_SUBSCRIPT_TRACE: diagnostic-only census for the
           remaining discarded address-of-subscript frontier. */
        if (context->source_function != NULL &&
            strcmp(context->source_function->name, "rcu_init_one") == 0) {
            (void)fprintf(stderr,
                          "CORE_M131_SUBSCRIPT_DETAIL stage=array-info base_kind=%d "
                          "base_vc=%d base_is_array=%d array_base=%d materialized=%d "
                          "index_kind=%d index_vc=%d\\n",
                          (int)base->kind,
                          (int)base->value_category,
                          minic_type_is_array(base->type) ? 1 : 0,
                          array_base ? 1 : 0,
                          array_base && array_info.has_materialized_type ? 1 : 0,
                          (int)index->kind,
                          (int)index->value_category);
        }
        if (array_base) {
'''
    text = text.replace(needle, replacement, 1)

    needle2 = '''            subscript_status =
                lower_address(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
'''
    if needle2 not in text:
        raise SystemExit('M131 array-base address seam changed')
    replacement2 = '''            subscript_status =
                lower_address(context, expression->value.subscript.base, &base_value);
            if (context->source_function != NULL &&
                strcmp(context->source_function->name, "rcu_init_one") == 0) {
                (void)fprintf(stderr,
                              "CORE_M131_SUBSCRIPT_DETAIL stage=array-base-address status=%d "
                              "base_kind=%d\\n",
                              (int)subscript_status,
                              (int)base->kind);
            }
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
'''
    text = text.replace(needle2, replacement2, 1)

    needle3 = '''        subscript_status =
            lower_expression(context, expression->value.subscript.index, &index_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }
'''
    if needle3 not in text:
        raise SystemExit('M131 subscript index seam changed')
    replacement3 = '''        subscript_status =
            lower_expression(context, expression->value.subscript.index, &index_value);
        if (context->source_function != NULL &&
            strcmp(context->source_function->name, "rcu_init_one") == 0) {
            (void)fprintf(stderr,
                          "CORE_M131_SUBSCRIPT_DETAIL stage=index status=%d index_kind=%d\\n",
                          (int)subscript_status,
                          (int)index->kind);
        }
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }
'''
    text = text.replace(needle3, replacement3, 1)

    needle4 = '''    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        MinicCoreLowerStatus status;

        status = lower_address(context, expression->value.unary.operand, value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (*value_id >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_equal(context->function->values[*value_id].type, expression->type)) {
            return MINIC_CORE_LOWER_OK;
        }
'''
    if needle4 not in text:
        raise SystemExit('M131 address-of type-contract seam changed')
    replacement4 = '''    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        MinicCoreLowerStatus status;

        status = lower_address(context, expression->value.unary.operand, value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (*value_id >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (context->source_function != NULL &&
            strcmp(context->source_function->name, "rcu_init_one") == 0) {
            MinicType lowered_pointee;
            MinicType expected_pointee;
            bool lowered_pointer = minic_type_pointee(
                context->function->values[*value_id].type, &lowered_pointee);
            bool expected_pointer = minic_type_pointee(expression->type, &expected_pointee);
            (void)fprintf(stderr,
                          "CORE_M131_ADDRESS_DETAIL type_equal=%d lowered_pointer=%d "
                          "expected_pointer=%d pointee_equal=%d lowered_const=%d "
                          "expected_const=%d lowered_volatile=%d expected_volatile=%d\\n",
                          minic_type_equal(context->function->values[*value_id].type,
                                           expression->type) ? 1 : 0,
                          lowered_pointer ? 1 : 0,
                          expected_pointer ? 1 : 0,
                          lowered_pointer && expected_pointer &&
                                  minic_type_equal(lowered_pointee, expected_pointee)
                              ? 1
                              : 0,
                          lowered_pointer && minic_type_is_const(lowered_pointee) ? 1 : 0,
                          expected_pointer && minic_type_is_const(expected_pointee) ? 1 : 0,
                          lowered_pointer && minic_type_is_volatile(lowered_pointee) ? 1 : 0,
                          expected_pointer && minic_type_is_volatile(expected_pointee) ? 1 : 0);
        }
        if (minic_type_equal(context->function->values[*value_id].type, expression->type)) {
            return MINIC_CORE_LOWER_OK;
        }
'''
    text = text.replace(needle4, replacement4, 1)
    path.write_text(text)

print('M131 static-local subscript/address diagnostic staged')
