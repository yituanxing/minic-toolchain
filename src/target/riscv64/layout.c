#include "target/riscv64/layout.h"

#include "target/data_layout.h"

#include <stdint.h>
#include <stdio.h>

static void
minic_riscv64_layout_error(MinicDiagnostic *diagnostic, const char *path, const char *message) {
    if (diagnostic == NULL) {
        return;
    }
    diagnostic->path = path;
    diagnostic->line = 1U;
    diagnostic->column = 1U;
    (void)snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
}

bool minic_riscv64_type_layout(const MinicC0Program *program,
                               MinicType type,
                               size_t *size,
                               size_t *alignment) {
    return minic_data_layout_type(minic_default_data_layout(), program, type, size, alignment);
}

static bool minic_riscv64_align_up(size_t value, size_t alignment, size_t *result) {
    size_t remainder;
    size_t padding;

    if (result == NULL || alignment == 0U) {
        return false;
    }
    remainder = value % alignment;
    padding = remainder == 0U ? 0U : alignment - remainder;
    if (value > SIZE_MAX - padding) {
        return false;
    }
    *result = value + padding;
    return true;
}

static bool minic_riscv64_layout_records(MinicC0Program *program) {
    const MinicDataLayout *layout;
    size_t record_index;

    if (program == NULL) {
        return false;
    }
    layout = minic_default_data_layout();
    for (record_index = 0U; record_index < program->record_count; ++record_index) {
        MinicRecord *record;
        MinicType record_type;
        size_t field_index;
        size_t storage_size;
        size_t alignment;

        record = &program->records[record_index];
        if (!record->is_complete) {
            record->storage_size = 0U;
            record->alignment = 0U;
            continue;
        }
        record_type = minic_type_record(record_index);
        if (!minic_data_layout_type(layout, program, record_type, &storage_size, &alignment)) {
            return false;
        }
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            MinicRecordField *field;
            size_t field_offset;

            field = &record->fields[field_index];
            if (!minic_data_layout_record_field_offset(
                    layout, program, record, field_index, &field_offset)) {
                return false;
            }
            field->storage_offset = field_offset;
            if (field->is_bit_field) {
                field->bit_offset = 0U;
            }
        }
        record->storage_size = storage_size;
        record->alignment = alignment;
    }
    return true;
}

static bool minic_riscv64_layout_globals(MinicC0Program *program) {
    size_t object_index;

    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        MinicGlobalObject *object;
        size_t storage_size;
        size_t alignment;

        object = &program->global_objects[object_index];
        if (object->is_extern && minic_type_is_record(object->type)) {
            const MinicRecord *record;

            record = minic_c0_program_record(program, object->type.record_id);
            if (record != NULL && !record->is_complete) {
                object->storage_size = 0U;
                object->alignment = 0U;
                continue;
            }
        }
        if (object->is_extern && minic_type_is_array(object->type)) {
            const MinicArrayType *array_type;

            array_type = minic_c0_program_array_type(program, object->type.array_type_id);
            if (array_type != NULL && array_type->element_count == 0U) {
                object->storage_size = 0U;
                object->alignment = 0U;
                continue;
            }
        }
        if (!minic_riscv64_type_layout(program, object->type, &storage_size, &alignment)) {
            return false;
        }
        object->storage_size = storage_size;
        object->alignment = alignment;
    }
    return true;
}

bool minic_riscv64_layout_program(const char *path,
                                  MinicC0Program *program,
                                  MinicDiagnostic *diagnostic) {
    size_t function_index;

    if (program == NULL) {
        minic_riscv64_layout_error(diagnostic, path, "cannot layout a null program");
        return false;
    }
    if (!minic_riscv64_layout_records(program)) {
        minic_riscv64_layout_error(diagnostic, path, "record size is invalid for the RV64 target");
        return false;
    }
    if (!minic_riscv64_layout_globals(program)) {
        minic_riscv64_layout_error(
            diagnostic, path, "global object size is invalid for the RV64 target");
        return false;
    }

    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function;
        size_t local_index;
        size_t storage_size;

        function = &program->functions[function_index];
        if (!function->is_defined) {
            function->local_storage_size = 0U;
            continue;
        }
        if (function->local_begin > program->local_count ||
            function->local_count > program->local_count - function->local_begin) {
            minic_riscv64_layout_error(diagnostic, path, "function local range is invalid");
            return false;
        }

        storage_size = 0U;
        for (local_index = 0U; local_index < function->local_count; ++local_index) {
            MinicLocal *local;
            size_t element_size;
            size_t object_size;
            size_t object_alignment;
            size_t object_offset;

            local = &program->locals[function->local_begin + local_index];
            if (!minic_riscv64_type_layout(
                    program, local->type, &element_size, &object_alignment) ||
                local->element_count == 0U || element_size > SIZE_MAX / local->element_count) {
                minic_riscv64_layout_error(
                    diagnostic, path, "local object size is invalid for the RV64 target");
                return false;
            }
            object_size = element_size * local->element_count;
            if (!minic_riscv64_align_up(storage_size, object_alignment, &object_offset) ||
                object_offset > SIZE_MAX - object_size) {
                minic_riscv64_layout_error(
                    diagnostic, path, "local object layout exceeds the RV64 target range");
                return false;
            }
            local->storage_offset = object_offset;
            storage_size = object_offset + object_size;
        }
        function->local_storage_size = storage_size;
    }
    return true;
}
