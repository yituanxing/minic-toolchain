#include "target/riscv64/layout.h"

#include "target/data_layout.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

static bool minic_riscv64_layout_globals(MinicC0Program *program) {
    size_t object_index;

    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        MinicGlobalObject *object;
        size_t storage_size;
        size_t alignment;

        object = &program->global_objects[object_index];
        if (object->is_extern && minic_type_is_void(object->type)) {
            object->storage_size = 0U;
            object->alignment = 0U;
            continue;
        }
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
        if (object->explicit_alignment != 0U) {
            if ((object->explicit_alignment & (object->explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if (object->explicit_alignment > alignment) {
                alignment = object->explicit_alignment;
            }
        }
        object->storage_size = storage_size;
        object->alignment = alignment;
    }
    return true;
}

void minic_riscv64_function_layout_initialize(MinicRiscv64FunctionLayout *layout) {
    if (layout == NULL) {
        return;
    }
    layout->local_offsets = NULL;
    layout->local_count = 0U;
    layout->local_storage_size = 0U;
}

void minic_riscv64_function_layout_destroy(MinicRiscv64FunctionLayout *layout) {
    if (layout == NULL) {
        return;
    }
    free(layout->local_offsets);
    minic_riscv64_function_layout_initialize(layout);
}

bool minic_riscv64_function_layout_local_offset(const MinicRiscv64FunctionLayout *layout,
                                                const MinicFunction *function,
                                                MinicLocalId local_id,
                                                size_t *offset) {
    size_t local_index;

    if (layout == NULL || function == NULL || offset == NULL ||
        layout->local_count != function->local_count || local_id < function->local_begin) {
        return false;
    }
    local_index = local_id - function->local_begin;
    if (local_index >= layout->local_count ||
        (layout->local_count != 0U && layout->local_offsets == NULL)) {
        return false;
    }
    *offset = layout->local_offsets[local_index];
    return true;
}

bool minic_riscv64_layout_function(const char *path,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   MinicRiscv64FunctionLayout *layout,
                                   MinicDiagnostic *diagnostic) {
    MinicRiscv64FunctionLayout result;
    size_t local_index;
    size_t storage_size;

    if (program == NULL || function == NULL || layout == NULL) {
        minic_riscv64_layout_error(diagnostic, path, "function layout inputs are invalid");
        return false;
    }
    minic_riscv64_function_layout_initialize(&result);
    if (!function->is_defined) {
        *layout = result;
        return true;
    }
    if (function->local_begin > program->local_count ||
        function->local_count > program->local_count - function->local_begin) {
        minic_riscv64_layout_error(diagnostic, path, "function local range is invalid");
        return false;
    }

    result.local_count = function->local_count;
    if (result.local_count != 0U) {
        result.local_offsets = (size_t *)calloc(result.local_count, sizeof(*result.local_offsets));
        if (result.local_offsets == NULL) {
            minic_riscv64_layout_error(
                diagnostic, path, "out of memory while laying out RV64 function");
            return false;
        }
    }

    storage_size = 0U;
    for (local_index = 0U; local_index < function->local_count; ++local_index) {
        const MinicLocal *local;
        size_t element_size;
        size_t object_size;
        size_t object_alignment;
        size_t object_offset;

        local = &program->locals[function->local_begin + local_index];
        if (!minic_riscv64_type_layout(program, local->type, &element_size, &object_alignment) ||
            local->element_count == 0U || element_size > SIZE_MAX / local->element_count) {
            minic_riscv64_layout_error(
                diagnostic, path, "local object size is invalid for the RV64 target");
            minic_riscv64_function_layout_destroy(&result);
            return false;
        }
        object_size = element_size * local->element_count;
        if (!minic_riscv64_align_up(storage_size, object_alignment, &object_offset) ||
            object_offset > SIZE_MAX - object_size) {
            minic_riscv64_layout_error(
                diagnostic, path, "local object layout exceeds the RV64 target range");
            minic_riscv64_function_layout_destroy(&result);
            return false;
        }
        result.local_offsets[local_index] = object_offset;
        storage_size = object_offset + object_size;
    }
    result.local_storage_size = storage_size;
    *layout = result;
    return true;
}

bool minic_riscv64_layout_program(const char *path,
                                  MinicC0Program *program,
                                  MinicDiagnostic *diagnostic) {
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
    return true;
}
