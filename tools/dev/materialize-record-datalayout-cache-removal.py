#!/usr/bin/env python3
from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text.rstrip() + "\n", encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


# Semantic AST: keep record layout inputs, remove derived DataLayout outputs.
path = "src/frontend/ast.h"
text = read(path)
text = replace_once(
    text,
    """    MinicType type;
    size_t element_count;
    size_t storage_offset;
    size_t bit_width;
    size_t bit_offset;
    size_t explicit_alignment;
""",
    """    MinicType type;
    size_t element_count;
    size_t bit_width;
    size_t explicit_alignment;
""",
    "record field derived layout fields",
)
text = replace_once(
    text,
    """    MinicRecordField *fields;
    size_t field_count;
    size_t field_capacity;
    size_t storage_size;
    size_t alignment;
    size_t explicit_alignment;
""",
    """    MinicRecordField *fields;
    size_t field_count;
    size_t field_capacity;
    size_t explicit_alignment;
""",
    "record derived layout fields",
)
write(path, text)

# Remove the only frontend initialization of a derived bit offset.
path = "src/frontend/ast.c"
text = read(path)
text = replace_once(text, "    field->bit_offset = 0U;\n", "", "bit-field derived offset init")
write(path, text)

# RV64 record layout becomes a validation pass over DataLayout, not a cache materializer.
path = "src/target/riscv64/layout.c"
text = read(path)
start = text.index("static bool minic_riscv64_layout_records(")
end = text.index("static bool minic_riscv64_layout_globals(", start)
replacement = """static bool minic_riscv64_layout_records(MinicC0Program *program) {
    const MinicDataLayout *layout;
    size_t record_index;

    if (program == NULL) {
        return false;
    }
    layout = minic_default_data_layout();
    for (record_index = 0U; record_index < program->record_count; ++record_index) {
        const MinicRecord *record;
        MinicType record_type;
        size_t field_index;
        size_t storage_size;
        size_t alignment;

        record = &program->records[record_index];
        if (!record->is_complete) {
            continue;
        }
        record_type = minic_type_record(record_index);
        if (!minic_data_layout_type(layout, program, record_type, &storage_size, &alignment)) {
            return false;
        }
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            size_t field_offset;
            size_t bit_offset;

            if (!minic_data_layout_record_field_layout(
                    layout, program, record, field_index, &field_offset, &bit_offset)) {
                return false;
            }
        }
    }
    return true;
}

"""
text = text[:start] + replacement + text[end:]
write(path, text)

# Frontend record test should test semantic metadata only.
path = "tests/frontend/record_test.c"
text = read(path)
old = """    if (record == NULL || strcmp(record->name, \"AES_ctx\") != 0 ||
        !record->is_complete || record->field_count != 2U ||
        record->storage_size != 0U || record->alignment != 0U) {
"""
new = """    if (record == NULL || strcmp(record->name, \"AES_ctx\") != 0 ||
        !record->is_complete || record->field_count != 2U) {
"""
text = replace_once(text, old, new, "frontend record cache assertion")
write(path, text)

# RV64 layout test now validates record layout through DataLayout queries directly.
path = "tests/target/riscv64/layout_test.c"
text = read(path)
include_anchor = '#include "frontend/type.h"\n'
if '#include "target/data_layout.h"\n' not in text:
    text = replace_once(
        text,
        include_anchor,
        include_anchor + '#include "target/data_layout.h"\n',
        "layout test DataLayout include",
    )

old = """    hooks_record = minic_c0_program_record(&program, hooks_record_id);
    if (hooks_record == NULL || hooks_record->storage_size != 16U ||
        hooks_record->alignment != 8U || hooks_record->fields[0].storage_offset != 0U ||
        hooks_record->fields[1].storage_offset != 8U) {
        minic_c0_program_destroy(&program);
        return fail(\"function pointer record field layout\");
    }

    floating_record = minic_c0_program_record(&program, floating_record_id);
    if (floating_record == NULL || floating_record->storage_size != 24U ||
        floating_record->alignment != 8U || floating_record->fields[0].storage_offset != 0U ||
        floating_record->fields[1].storage_offset != 8U ||
        floating_record->fields[2].storage_offset != 16U) {
        minic_c0_program_destroy(&program);
        return fail(\"double record field layout\");
    }

    record = minic_c0_program_record(&program, record_id);
    if (record == NULL || record->storage_size != 24U || record->alignment != 4U ||
        record->fields[0].storage_offset != 0U || record->fields[1].storage_offset != 4U ||
        record->fields[2].storage_offset != 20U) {
        minic_c0_program_destroy(&program);
        return fail(\"record field layout\");
    }
"""
new = """    hooks_record = minic_c0_program_record(&program, hooks_record_id);
    {
        size_t size;
        size_t alignment;
        size_t offset0;
        size_t offset1;

        if (hooks_record == NULL ||
            !minic_riscv64_type_layout(
                &program, minic_type_record(hooks_record_id), &size, &alignment) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, hooks_record, 0U, &offset0) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, hooks_record, 1U, &offset1) ||
            size != 16U || alignment != 8U || offset0 != 0U || offset1 != 8U) {
            minic_c0_program_destroy(&program);
            return fail(\"function pointer record field layout\");
        }
    }

    floating_record = minic_c0_program_record(&program, floating_record_id);
    {
        size_t size;
        size_t alignment;
        size_t offset0;
        size_t offset1;
        size_t offset2;

        if (floating_record == NULL ||
            !minic_riscv64_type_layout(
                &program, minic_type_record(floating_record_id), &size, &alignment) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, floating_record, 0U, &offset0) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, floating_record, 1U, &offset1) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, floating_record, 2U, &offset2) ||
            size != 24U || alignment != 8U || offset0 != 0U || offset1 != 8U || offset2 != 16U) {
            minic_c0_program_destroy(&program);
            return fail(\"double record field layout\");
        }
    }

    record = minic_c0_program_record(&program, record_id);
    {
        size_t size;
        size_t alignment;
        size_t offset0;
        size_t offset1;
        size_t offset2;

        if (record == NULL ||
            !minic_riscv64_type_layout(
                &program, minic_type_record(record_id), &size, &alignment) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, record, 0U, &offset0) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, record, 1U, &offset1) ||
            !minic_data_layout_record_field_offset(
                minic_default_data_layout(), &program, record, 2U, &offset2) ||
            size != 24U || alignment != 4U || offset0 != 0U || offset1 != 4U || offset2 != 20U) {
            minic_c0_program_destroy(&program);
            return fail(\"record field layout\");
        }
    }
"""
text = replace_once(text, old, new, "layout test record cache assertions")
write(path, text)

print("MATERIALIZED record-datalayout-cache-removal")
