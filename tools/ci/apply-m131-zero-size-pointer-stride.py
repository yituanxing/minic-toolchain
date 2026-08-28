#!/usr/bin/env python3
from pathlib import Path

# Frontend/Sema owns whether a pointee is complete and sizeable. A complete GNU
# zero-size object has a real arithmetic stride of zero; do not reinterpret that
# DataLayout result as unsupported.
path = Path('src/frontend/expression_semantics.c')
text = path.read_text()
marker = 'M131_ZERO_SIZE_POINTER_STRIDE'
if marker not in text:
    needle = '''    return minic_data_layout_type(layout, program, pointee, element_size, &alignment) &&
           *element_size != 0U;
'''
    if needle not in text:
        raise SystemExit('pointer arithmetic element-size seam changed')
    replacement = '''    /* M131_ZERO_SIZE_POINTER_STRIDE: GNU complete zero-size object types
       (notably empty records, and the zero-size array forms already accepted by
       the frontend) have a real pointer-arithmetic stride of zero. Completeness
       is owned by the caller/frontend; DataLayout returning a valid size of zero
       must not be reinterpreted here as an unsupported type. Incomplete types
       still fail because DataLayout cannot size them. */
    return minic_data_layout_type(layout, program, pointee, element_size, &alignment);
'''
    text = text.replace(needle, replacement, 1)
    path.write_text(text)

# POINTER_OFFSET is the shared Core owner for subscript, p +/- i, pointer
# compound assignment, and pointer ++/--. A zero stride is representable and
# still keeps the index operand evaluation in the IR; the backend naturally
# computes base +/- index*0. Make that generic Core contract explicit once.
path = Path('src/core/core_ir.c')
text = path.read_text()
core_marker = 'M131_ZERO_STRIDE_POINTER_OFFSET'
if core_marker not in text:
    needle = '''               minic_type_equal(function->values[base].type, instruction->type) &&
               minic_type_is_integer(function->values[index].type) &&
               instruction->value.pointer_offset.element_size != 0U;
'''
    if needle not in text:
        raise SystemExit('Core POINTER_OFFSET verifier seam changed')
    replacement = '''               minic_type_equal(function->values[base].type, instruction->type) &&
               /* M131_ZERO_STRIDE_POINTER_OFFSET: zero is a valid GNU object
                  stride. Keeping POINTER_OFFSET rather than folding it in the
                  producer preserves index evaluation and gives all pointer
                  arithmetic producers one Core semantic owner. */
               minic_type_is_integer(function->values[index].type);
'''
    text = text.replace(needle, replacement, 1)
    path.write_text(text)

# Pointer difference is a different semantic consumer: Core implements it as
# byte_difference / element_size, so a zero-size pointee has no representable
# element distance. Keep that case fail-closed instead of ever forming /0.
path = Path('src/core/core_lower.c')
text = path.read_text()
difference_marker = 'M131_ZERO_STRIDE_POINTER_DIFFERENCE_FAIL_CLOSED'
if difference_marker not in text:
    needle = '''            minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      left_type,
                                                      &element_size)) {
'''
    if needle not in text:
        raise SystemExit('Core pointer-difference stride seam changed')
    replacement = '''            minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      left_type,
                                                      &element_size) &&
            /* M131_ZERO_STRIDE_POINTER_DIFFERENCE_FAIL_CLOSED: unlike pointer
               +/- integer, pointer difference divides by the pointee stride. */
            element_size != 0U) {
'''
    text = text.replace(needle, replacement, 1)
    path.write_text(text)

# Strict-Core fixture exercises the generic zero-stride POINTER_OFFSET without
# depending on empty-record local-object storage or array declaration support.
Path('tests/compiler/c0/m131_zero_size_record_subscript.c').write_text(r'''struct empty_record { };

static struct empty_record *zero_stride(struct empty_record *pointer, long index) {
    return pointer + index;
}

int main(void) {
    return 0;
}
''')

print('M131 frontend stride + Core zero-stride POINTER_OFFSET owner staged')
