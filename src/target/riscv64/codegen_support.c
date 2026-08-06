#include "target/riscv64/codegen_internal.h"

#include <stdint.h>
#include <stdio.h>

static bool minic_riscv64_local_object(
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicLocalId local_id,
    const MinicLocal **local)
{
    const MinicLocal *object;

    if (program == NULL || function == NULL || local == NULL ||
        local_id < function->local_begin ||
        local_id - function->local_begin >= function->local_count) {
        return false;
    }
    object = minic_c0_program_local(program, local_id);
    if (object == NULL || function->local_storage_size == 0U ||
        object->storage_offset >= function->local_storage_size) {
        return false;
    }
    *local = object;
    return true;
}

static bool minic_riscv64_scalar_object_access(
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicLocalId local_id,
    const MinicLocal **local,
    size_t *width)
{
    const MinicLocal *object;
    size_t object_width;

    if (width == NULL ||
        !minic_riscv64_local_object(
            program,
            function,
            local_id,
            &object)) {
        return false;
    }
    if (minic_type_is_integer(object->type)) {
        object_width = 4U;
    } else if (minic_type_is_pointer(object->type)) {
        object_width = 8U;
    } else {
        return false;
    }
    if (object_width >
        function->local_storage_size - object->storage_offset) {
        return false;
    }
    *local = object;
    *width = object_width;
    return true;
}

static bool minic_riscv64_emit_s0_access(
    FILE *file,
    const char *instruction,
    const char *register_name,
    size_t offset)
{
    if (offset <= 2047U) {
        return fprintf(
            file,
            "  %s %s, %zu(s0)\n",
            instruction,
            register_name,
            offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, s0, t2\n"
        "  %s %s, 0(t2)\n",
        offset,
        instruction,
        register_name) >= 0;
}

void minic_riscv64_set_diagnostic(
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

bool minic_riscv64_emit_stack_allocate(FILE *file, size_t size)
{
    if (size == 0U) {
        return true;
    }
    if (size <= 2048U) {
        return fprintf(file, "  addi sp, sp, -%zu\n", size) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  sub sp, sp, t2\n",
        size) >= 0;
}

bool minic_riscv64_emit_stack_release(FILE *file, size_t size)
{
    if (size == 0U) {
        return true;
    }
    if (size <= 2047U) {
        return fprintf(file, "  addi sp, sp, %zu\n", size) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add sp, sp, t2\n",
        size) >= 0;
}

bool minic_riscv64_emit_sp_store64(
    FILE *file,
    const char *register_name,
    size_t offset)
{
    if (offset <= 2047U) {
        return fprintf(file, "  sd %s, %zu(sp)\n", register_name, offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, sp, t2\n"
        "  sd %s, 0(t2)\n",
        offset,
        register_name) >= 0;
}

bool minic_riscv64_emit_sp_load64(
    FILE *file,
    const char *register_name,
    size_t offset)
{
    if (offset <= 2047U) {
        return fprintf(file, "  ld %s, %zu(sp)\n", register_name, offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, sp, t2\n"
        "  ld %s, 0(t2)\n",
        offset,
        register_name) >= 0;
}

bool minic_riscv64_emit_object_address(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicLocalId local_id)
{
    const MinicLocal *local;

    if (!minic_riscv64_local_object(
            program,
            function,
            local_id,
            &local)) {
        return false;
    }
    if (local->storage_offset <= 2047U) {
        return fprintf(
            file,
            "  addi a0, s0, %zu\n",
            local->storage_offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add a0, s0, t2\n",
        local->storage_offset) >= 0;
}

bool minic_riscv64_emit_object_load(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicLocalId local_id)
{
    const MinicLocal *local;
    size_t width;
    const char *instruction;

    if (!minic_riscv64_scalar_object_access(
            program,
            function,
            local_id,
            &local,
            &width)) {
        return false;
    }
    if (width == 4U) {
        instruction = minic_type_is_unsigned_integer(local->type) ? "lwu" : "lw";
    } else {
        instruction = "ld";
    }
    return minic_riscv64_emit_s0_access(
        file,
        instruction,
        "a0",
        local->storage_offset);
}

bool minic_riscv64_emit_object_store_register(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicLocalId local_id,
    const char *register_name)
{
    const MinicLocal *local;
    size_t width;

    if (register_name == NULL ||
        !minic_riscv64_scalar_object_access(
            program,
            function,
            local_id,
            &local,
            &width)) {
        return false;
    }
    return minic_riscv64_emit_s0_access(
        file,
        width == 4U ? "sw" : "sd",
        register_name,
        local->storage_offset);
}

bool minic_riscv64_emit_object_store(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicLocalId local_id)
{
    return minic_riscv64_emit_object_store_register(
        file,
        program,
        function,
        local_id,
        "a0");
}

bool minic_riscv64_frame_size(
    const MinicFunction *function,
    size_t *frame_size)
{
    size_t required_bytes;

    if (function == NULL || frame_size == NULL ||
        function->local_storage_size > SIZE_MAX - 31U) {
        return false;
    }
    required_bytes = function->local_storage_size + 16U;
    *frame_size = (required_bytes + 15U) & ~(size_t)15U;
    return true;
}
