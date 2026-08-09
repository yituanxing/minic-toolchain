#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_function.c")
text = path.read_text()
marker = "static bool minic_riscv64_emit_constant_value(FILE *file,\n"
if text.count(marker) != 1:
    raise SystemExit(f"recursive record emitter marker: expected 1 match, found {text.count(marker)}")

helper = r'''static bool minic_riscv64_emit_direct_record_values(FILE *file,
                                                     const MinicC0Program *program,
                                                     const MinicGlobalObject *object,
                                                     const MinicRecord *record) {
    size_t cursor;
    size_t field_index;

    if (file == NULL || program == NULL || object == NULL || record == NULL ||
        !record->is_complete || record->is_union ||
        object->initializer_count != record->field_count) {
        return false;
    }
    cursor = 0U;
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        const MinicRecordField *field;
        size_t field_size;
        size_t field_alignment;
        size_t field_offset;
        int value;

        field = minic_c0_record_field(record, field_index);
        if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
            !minic_riscv64_type_layout(program, field->type, &field_size, &field_alignment)) {
            return false;
        }
        (void)field_alignment;
        field_offset = field->storage_offset;
        if (field_offset < cursor || field_offset > object->storage_size ||
            field_size > object->storage_size - field_offset ||
            !minic_riscv64_emit_zero_bytes(file, field_offset - cursor)) {
            return false;
        }
        value = object->initializer_values[field_index];
        if (minic_type_is_integer(field->type)) {
            const char *directive;

            directive = minic_type_is_char_integer(field->type)    ? ".byte"
                        : minic_type_is_short_integer(field->type) ? ".half"
                        : minic_type_is_long_integer(field->type)  ? ".dword"
                                                                   : ".word";
            if (minic_type_is_char_integer(field->type)) {
                unsigned int byte_value;

                byte_value = (unsigned int)value & 0xffU;
                if (fprintf(file, "  %s %u\n", directive, byte_value) < 0) {
                    return false;
                }
            } else if (fprintf(file, "  %s %d\n", directive, value) < 0) {
                return false;
            }
        } else {
            if (value != 0 ||
                (!minic_type_is_record(field->type) && !minic_type_is_pointer(field->type)) ||
                !minic_riscv64_emit_zero_bytes(file, field_size)) {
                return false;
            }
        }
        cursor = field_offset + field_size;
    }
    return cursor <= object->storage_size &&
           minic_riscv64_emit_zero_bytes(file, object->storage_size - cursor);
}

'''
text = text.replace(marker, helper + marker, 1)

old = '''static bool minic_riscv64_emit_record_values(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicGlobalObject *object) {
    size_t emitted_size;
    size_t initializer_index;

    if (file == NULL || program == NULL || object == NULL || !minic_type_is_record(object->type) ||
        object->is_zero_initialized || object->function_relocation_count != 0U ||
        object->object_relocation_count != 0U) {
        return false;
    }
    initializer_index = 0U;
    emitted_size = 0U;
    return minic_riscv64_emit_constant_value(file,
                                             program,
                                             object,
                                             object->type,
                                             &initializer_index,
                                             &emitted_size) &&
           initializer_index == object->initializer_count && emitted_size == object->storage_size;
}
'''
new = '''static bool minic_riscv64_emit_record_values(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicGlobalObject *object) {
    const MinicRecord *record;
    size_t emitted_size;
    size_t initializer_index;

    if (file == NULL || program == NULL || object == NULL || !minic_type_is_record(object->type) ||
        object->is_zero_initialized || object->function_relocation_count != 0U ||
        object->object_relocation_count != 0U) {
        return false;
    }
    record = minic_c0_program_record(program, object->type.record_id);
    if (record == NULL || !record->is_complete) {
        return false;
    }
    if (!record->is_union && object->initializer_count == record->field_count) {
        return minic_riscv64_emit_direct_record_values(file, program, object, record);
    }
    initializer_index = 0U;
    emitted_size = 0U;
    return minic_riscv64_emit_constant_value(file,
                                             program,
                                             object,
                                             object->type,
                                             &initializer_index,
                                             &emitted_size) &&
           initializer_index == object->initializer_count && emitted_size == object->storage_size;
}
'''
if text.count(old) != 1:
    raise SystemExit(f"record emitter compatibility dispatch: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged compatibility for direct-field and recursive static record initializer encodings")
