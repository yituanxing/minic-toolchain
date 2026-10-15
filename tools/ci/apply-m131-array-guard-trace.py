#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M131_ARRAY_GUARD_TRACE'

if marker not in text:
    needle = '''        if (array_base) {
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
'''
    if needle not in text:
        raise SystemExit('M131 array guard seam changed')
    replacement = '''        if (array_base) {
            bool m131_element_equal;
            bool m131_pointer_ok;
            bool m131_stride_ok;
            MinicType m131_pointer_type;
            size_t m131_element_size;

            /* M131_ARRAY_GUARD_TRACE: diagnostic-only decomposition of the
               remaining array-object subscript precondition. */
            m131_element_equal = minic_type_equal(array_info.element_type, expression->type);
            m131_pointer_ok = minic_type_pointer_to(array_info.element_type, &m131_pointer_type);
            m131_stride_ok = m131_pointer_ok &&
                minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                         minic_default_data_layout(),
                                                         m131_pointer_type,
                                                         &m131_element_size);
            if (context->source_function != NULL &&
                strcmp(context->source_function->name, "rcu_init_one") == 0) {
                (void)fprintf(stderr,
                              "CORE_M131_ARRAY_GUARD element_equal=%d pointer_ok=%d stride_ok=%d "
                              "elem_base=%d expr_base=%d elem_record=%zu expr_record=%zu "
                              "elem_ptr_depth=%u expr_ptr_depth=%u elem_bq=%u expr_bq=%u "
                              "elem_pq=%u expr_pq=%u elem_pvq=%u expr_pvq=%u\\n",
                              m131_element_equal ? 1 : 0,
                              m131_pointer_ok ? 1 : 0,
                              m131_stride_ok ? 1 : 0,
                              (int)array_info.element_type.base_kind,
                              (int)expression->type.base_kind,
                              array_info.element_type.record_id,
                              expression->type.record_id,
                              array_info.element_type.pointer_depth,
                              expression->type.pointer_depth,
                              array_info.element_type.base_qualifiers,
                              expression->type.base_qualifiers,
                              array_info.element_type.pointer_qualifiers,
                              expression->type.pointer_qualifiers,
                              array_info.element_type.pointer_volatile_qualifiers,
                              expression->type.pointer_volatile_qualifiers);
            }
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
'''
    text = text.replace(needle, replacement, 1)
    path.write_text(text)

print('M131 array guard diagnostic staged')
