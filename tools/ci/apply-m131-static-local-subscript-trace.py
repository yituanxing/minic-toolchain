#!/usr/bin/env python3
from pathlib import Path

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
    path.write_text(text)

print('M131 static-local subscript diagnostic staged')
