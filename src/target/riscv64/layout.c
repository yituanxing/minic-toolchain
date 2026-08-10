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

static bool minic_riscv64_layout_type_pending(const MinicC0Program *program, MinicType type) {
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, type.record_id);
        return record != NULL && record->is_complete && record->alignment == 0U;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL &&
               minic_riscv64_layout_type_pending(program, array_type->element_type);
    }
    return false;
}

static bool
minic_riscv64_layout_one_record(MinicC0Program *program, MinicRecord *record, bool *ready) {
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
    storage_size = 0U;
    record_alignment = 1U;
    for (field_index = 0U; field_index < record->field_count; ++field_index) {
        MinicRecordField *field;
        size_t element_size;
        size_t field_size;
        size_t field_alignment;
        size_t field_offset;

        field = &record->fields[field_index];
        if (field->is_bit_field) {
            size_t storage_bits;

            if (!minic_type_is_integer(field->type) || field->element_count != 1U ||
                field->is_flexible_array ||
                !minic_riscv64_type_layout(program, field->type, &element_size, &field_alignment) ||
                element_size > SIZE_MAX / 8U) {
                return false;
            }
            storage_bits = element_size * 8U;
            if (field->bit_width > storage_bits ||
                (field->bit_width != 0U && field->bit_width != storage_bits)) {
                return false;
            }
            field->bit_offset = 0U;
            if (record->is_union) {
                field_offset = 0U;
                if (field->bit_width != 0U && element_size > storage_size) {
                    storage_size = element_size;
                }
            } else {
                if (record->is_packed) {
                    field_offset = storage_size;
                } else if (!minic_riscv64_align_up(storage_size, field_alignment, &field_offset)) {
                    return false;
                }
                if (field->bit_width != 0U) {
                    if (field_offset > SIZE_MAX - element_size) {
                        return false;
                    }
                    storage_size = field_offset + element_size;
                } else {
                    storage_size = field_offset;
                }
            }
            field->storage_offset = field_offset;
            if (!record->is_packed && field_alignment > record_alignment) {
                record_alignment = field_alignment;
            }
            continue;
        }
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
        field_size = (field->is_flexible_array || field->is_zero_length_array)
                         ? 0U
                         : element_size * field->element_count;
        if (field->explicit_alignment != 0U) {
            if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if (field->explicit_alignment > field_alignment) {
                field_alignment = field->explicit_alignment;
            }
        }
        if (record->is_union) {
            field_offset = 0U;
            if (field_size > storage_size) {
                storage_size = field_size;
            }
        } else {
            if (record->is_packed && field->explicit_alignment == 0U) {
                field_offset = storage_size;
            } else if (!minic_riscv64_align_up(storage_size, field_alignment, &field_offset)) {
                return false;
            }
            if (field_offset > SIZE_MAX - field_size) {
                return false;
            }
            storage_size = field_offset + field_size;
        }
        field->storage_offset = field_offset;
        if ((!record->is_packed || field->explicit_alignment != 0U) &&
            field_alignment > record_alignment) {
            record_alignment = field_alignment;
        }
    }
    if (record->explicit_alignment != 0U) {
        if ((record->explicit_alignment & (record->explicit_alignment - 1U)) != 0U) {
            return false;
        }
        if (record->explicit_alignment > record_alignment) {
            record_alignment = record->explicit_alignment;
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
            if (!record->is_complete || record->alignment != 0U) {
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
