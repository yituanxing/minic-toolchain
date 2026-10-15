from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
old = """            minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type) &&
            minic_type_equal(left_type, right_type) &&
            minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      left_type,
                                                      &element_size)) {
"""
new = """            minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type) &&
            /* BATCH_W_QUALIFIED_POINTER_DIFFERENCE: language compatibility is
               owned by frontend/Sema.  `const T * - T *` is a valid pointer
               difference when the pointed-to object types are compatible;
               Core must not re-impose exact pointer-type equality after Sema
               has accepted the expression.  The representation and stride
               lowering below remain target-neutral and unchanged. */
            minic_c0_pointer_difference_compatible(
                context->body->program, left_type, right_type) &&
            minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      left_type,
                                                      &element_size)) {
"""
if old not in text:
    if "BATCH_W_QUALIFIED_POINTER_DIFFERENCE" in text:
        print("CORE_BATCH_W_ALREADY_PATCHED")
        raise SystemExit(0)
    raise SystemExit("Batch W anchor not found")
text = text.replace(old, new, 1)
path.write_text(text)
print("CORE_BATCH_W_PATCHED qualified pointer difference")
