#!/usr/bin/env python3
"""Materialize RV64 zero-size aggregate object and zero-stride address support once."""
from pathlib import Path


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    count = text.count(before)
    if count != 1:
        raise SystemExit(f"{path}: expected one materialization anchor, found {count}")
    path.write_text(text.replace(before, after, 1))


expression_path = Path("src/target/riscv64/codegen_expression.c")
replace_once(
    expression_path,
    """    if (file == NULL || register_name == NULL || scratch_register == NULL || element_size == 0U) {
        return false;
    }
    if (element_size == 1U) {
""",
    """    if (file == NULL || register_name == NULL || scratch_register == NULL) {
        return false;
    }
    if (element_size == 0U) {
        return fprintf(file, "  li %s, 0\\n", register_name) >= 0;
    }
    if (element_size == 1U) {
""",
)

function_path = Path("src/target/riscv64/codegen_function.c")
replace_once(
    function_path,
    """static bool minic_riscv64_zero_size_record_definition(const MinicC0Program *program,
                                                      const MinicGlobalObject *object) {
    size_t object_alignment;
    size_t storage_size;

    if (program == NULL || object == NULL ||
        !minic_data_layout_global_object(
            minic_default_data_layout(), program, object, &storage_size, &object_alignment)) {
        return false;
    }
    (void)object_alignment;

    const MinicRecord *record;

    if (storage_size != 0U || !minic_type_is_record(object->type) ||
        object->initializer_count != 0U || object->relocation_count != 0U) {
        return false;
    }
    record = minic_c0_program_record(program, object->type.record_id);
    return record != NULL && record->is_complete && record->field_count == 0U;
}
""",
    """static bool minic_riscv64_zero_size_object_definition(const MinicC0Program *program,
                                                      const MinicGlobalObject *object) {
    size_t object_alignment;
    size_t storage_size;

    if (program == NULL || object == NULL ||
        !minic_data_layout_global_object(
            minic_default_data_layout(), program, object, &storage_size, &object_alignment)) {
        return false;
    }
    (void)object_alignment;
    if (storage_size != 0U || object->initializer_count != 0U || object->relocation_count != 0U) {
        return false;
    }
    if (minic_type_is_record(object->type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, object->type.record_id);
        return record != NULL && record->is_complete && record->field_count == 0U;
    }
    if (minic_type_is_array(object->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        return array_type != NULL && (array_type->is_zero_length || array_type->element_count != 0U);
    }
    return false;
}
""",
)
replace_once(
    function_path,
    """    bool zero_size_record_definition;

    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object_alignment == 0U ||
        !minic_riscv64_alignment_power(object_alignment, &alignment_power)) {
        return false;
    }
    zero_size_record_definition = minic_riscv64_zero_size_record_definition(program, object);
    if (storage_size == 0U && !zero_size_record_definition) {
""",
    """    bool zero_size_object_definition;

    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object_alignment == 0U ||
        !minic_riscv64_alignment_power(object_alignment, &alignment_power)) {
        return false;
    }
    zero_size_object_definition = minic_riscv64_zero_size_object_definition(program, object);
    if (storage_size == 0U && !zero_size_object_definition) {
""",
)
replace_once(
    function_path,
    """        if (record == NULL || !record->is_complete ||
            (object->initializer_count == 0U && !zero_size_record_definition)) {
""",
    """        if (record == NULL || !record->is_complete ||
            (object->initializer_count == 0U && !zero_size_object_definition)) {
""",
)
