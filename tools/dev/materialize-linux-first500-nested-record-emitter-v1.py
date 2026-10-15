#!/usr/bin/env python3
"""Keep the scalar-record fast path restricted to scalar record fields."""
from pathlib import Path


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    count = text.count(before)
    if count != 1:
        raise SystemExit(f"{path}: expected one materialization anchor, found {count}")
    path.write_text(text.replace(before, after, 1))


path = Path("src/target/riscv64/codegen_function.c")
replace_once(
    path,
    """    {
        bool has_recursive_relocation;
        size_t index;

        has_recursive_relocation = false;
        for (index = 0U; index < object->relocation_count; ++index) {
            if (object->relocations[index].location_kind ==
                MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
                has_recursive_relocation = true;
                break;
            }
        }
        if (!record->is_union && object->initializer_count == record->field_count &&
            !has_recursive_relocation && !minic_riscv64_record_has_bit_fields(record)) {
            return minic_riscv64_emit_direct_record_values(file, program, object, record);
        }
    }
""",
    """    {
        bool has_recursive_relocation;
        bool has_nonscalar_field;
        size_t index;

        has_recursive_relocation = false;
        for (index = 0U; index < object->relocation_count; ++index) {
            if (object->relocations[index].location_kind ==
                MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) {
                has_recursive_relocation = true;
                break;
            }
        }
        has_nonscalar_field = false;
        for (index = 0U; index < record->field_count; ++index) {
            const MinicRecordField *field;

            field = minic_c0_record_field(record, index);
            if (field == NULL || field->element_count != 1U ||
                (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type))) {
                has_nonscalar_field = true;
                break;
            }
        }
        if (!record->is_union && object->initializer_count == record->field_count &&
            !has_recursive_relocation && !has_nonscalar_field &&
            !minic_riscv64_record_has_bit_fields(record)) {
            return minic_riscv64_emit_direct_record_values(file, program, object, record);
        }
    }
""",
)
