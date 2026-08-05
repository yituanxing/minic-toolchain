#include "target/riscv64/layout.h"

#include <stdint.h>
#include <stdio.h>

static void minic_riscv64_layout_error(
    MinicDiagnostic *diagnostic,
    const char *path,
    const char *message)
{
    if (diagnostic == NULL) {
        return;
    }
    diagnostic->path = path;
    diagnostic->line = 1U;
    diagnostic->column = 1U;
    (void)snprintf(
        diagnostic->message,
        sizeof(diagnostic->message),
        "%s",
        message);
}

static bool minic_riscv64_type_layout(
    MinicType type,
    size_t *size,
    size_t *alignment)
{
    if (size == NULL || alignment == NULL) {
        return false;
    }
    if (minic_type_is_integer(type)) {
        *size = 4U;
        *alignment = 4U;
        return true;
    }
    if (minic_type_is_pointer(type)) {
        *size = 8U;
        *alignment = 8U;
        return true;
    }
    return false;
}

static bool minic_riscv64_align_up(
    size_t value,
    size_t alignment,
    size_t *result)
{
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

bool minic_riscv64_layout_program(
    const char *path,
    MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    size_t function_index;

    if (program == NULL) {
        minic_riscv64_layout_error(
            diagnostic,
            path,
            "cannot layout a null program");
        return false;
    }

    for (function_index = 0U;
         function_index < program->function_count;
         ++function_index) {
        MinicFunction *function;
        size_t local_index;
        size_t storage_size;

        function = &program->functions[function_index];
        if (!function->is_defined) {
            function->local_storage_size = 0U;
            continue;
        }
        if (function->local_begin > program->local_count ||
            function->local_count >
                program->local_count - function->local_begin) {
            minic_riscv64_layout_error(
                diagnostic,
                path,
                "function local range is invalid");
            return false;
        }

        storage_size = 0U;
        for (local_index = 0U;
             local_index < function->local_count;
             ++local_index) {
            MinicLocal *local;
            size_t object_size;
            size_t object_alignment;
            size_t object_offset;

            local = &program->locals[function->local_begin + local_index];
            if (!minic_riscv64_type_layout(
                    local->type,
                    &object_size,
                    &object_alignment) ||
                !minic_riscv64_align_up(
                    storage_size,
                    object_alignment,
                    &object_offset) ||
                object_offset > SIZE_MAX - object_size) {
                minic_riscv64_layout_error(
                    diagnostic,
                    path,
                    "local object layout exceeds the RV64 target range");
                return false;
            }
            local->storage_offset = object_offset;
            storage_size = object_offset + object_size;
        }
        function->local_storage_size = storage_size;
    }
    return true;
}
