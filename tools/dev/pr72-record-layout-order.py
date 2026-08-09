#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/layout.c")
text = path.read_text()
start = text.index("static bool minic_riscv64_layout_records(MinicC0Program *program) {")
end = text.index("\nstatic bool minic_riscv64_layout_globals", start)
replacement = r'''static bool minic_riscv64_layout_type_pending(const MinicC0Program *program, MinicType type) {
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, type.record_id);
        return record != NULL && record->is_complete && record->storage_size == 0U;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL &&
               minic_riscv64_layout_type_pending(program, array_type->element_type);
    }
    return false;
}

static bool minic_riscv64_layout_one_record(MinicC0Program *program,
                                             MinicRecord *record,
                                             bool *ready) {
    size_t field_index;
    size_t storage_size;
    size_t record_alignment;

    if (program == NULL || record == NULL || ready == NULL) {
        return false;
    }
    *ready = false;
    if (!record->is_complete) {
        record->storage_size = 0U;
        record->alignment = 0U;
        *ready = true;
        return true;
    }
    if (record->field_count == 0U) {
        return false;
    }

    storage_size = 0U;
    record_alignment = 1U;
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        MinicRecordField *field;
        size_t element_size;
        size_t field_size;
        size_t field_alignment;
        size_t field_offset;

        field = &record->fields[field_index];
        if (field->element_count == 0U) {
            return false;
        }
        if (!minic_riscv64_type_layout(program, field->type, &element_size, &field_alignment)) {
            if (minic_riscv64_layout_type_pending(program, field->type)) {
                return true;
            }
            return false;
        }
        if (element_size > SIZE_MAX / field->element_count) {
            return false;
        }
        field_size = element_size * field->element_count;
        if (record->is_union) {
            field_offset = 0U;
            if (field_size > storage_size) {
                storage_size = field_size;
            }
        } else {
            if (!minic_riscv64_align_up(storage_size, field_alignment, &field_offset) ||
                field_offset > SIZE_MAX - field_size) {
                return false;
            }
            storage_size = field_offset + field_size;
        }
        field->storage_offset = field_offset;
        if (field_alignment > record_alignment) {
            record_alignment = field_alignment;
        }
    }
    if (!minic_riscv64_align_up(storage_size, record_alignment, &record->storage_size)) {
        return false;
    }
    record->alignment = record_alignment;
    *ready = true;
    return true;
}

static bool minic_riscv64_layout_records(MinicC0Program *program) {
    size_t remaining;
    size_t record_index;

    remaining = 0U;
    for (record_index = 0U; record_index < program->record_count; ++record_index) {
        MinicRecord *record;

        record = &program->records[record_index];
        if (!record->is_complete) {
            record->storage_size = 0U;
            record->alignment = 0U;
        } else {
            record->storage_size = 0U;
            record->alignment = 0U;
            remaining += 1U;
        }
    }

    while (remaining > 0U) {
        bool made_progress;

        made_progress = false;
        for (record_index = 0U; record_index < program->record_count; ++record_index) {
            MinicRecord *record;
            bool ready;

            record = &program->records[record_index];
            if (!record->is_complete || record->storage_size != 0U) {
                continue;
            }
            if (!minic_riscv64_layout_one_record(program, record, &ready)) {
                return false;
            }
            if (ready) {
                remaining -= 1U;
                made_progress = true;
            }
        }
        if (!made_progress) {
            return false;
        }
    }
    return true;
}
'''
path.write_text(text[:start] + replacement + text[end:])
print("staged dependency-ordered struct/union layout")
