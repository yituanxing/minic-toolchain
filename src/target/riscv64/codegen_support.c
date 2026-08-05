#include "target/riscv64/codegen_internal.h"

#include <stdint.h>
#include <stdio.h>

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

bool minic_riscv64_emit_local_load(
    FILE *file,
    const MinicFunction *function,
    MinicLocalId local_id)
{
    size_t relative_id;
    size_t offset;

    if (local_id < function->local_begin ||
        local_id - function->local_begin >= function->local_count) {
        return false;
    }
    relative_id = local_id - function->local_begin;
    if (relative_id > SIZE_MAX / 4U) {
        return false;
    }
    offset = relative_id * 4U;
    if (offset <= 2047U) {
        return fprintf(file, "  lw a0, %zu(s0)\n", offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, s0, t2\n"
        "  lw a0, 0(t2)\n",
        offset) >= 0;
}

bool minic_riscv64_emit_local_store(
    FILE *file,
    const MinicFunction *function,
    MinicLocalId local_id)
{
    size_t relative_id;
    size_t offset;

    if (local_id < function->local_begin ||
        local_id - function->local_begin >= function->local_count) {
        return false;
    }
    relative_id = local_id - function->local_begin;
    if (relative_id > SIZE_MAX / 4U) {
        return false;
    }
    offset = relative_id * 4U;
    if (offset <= 2047U) {
        return fprintf(file, "  sw a0, %zu(s0)\n", offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, s0, t2\n"
        "  sw a0, 0(t2)\n",
        offset) >= 0;
}

bool minic_riscv64_frame_size(
    const MinicFunction *function,
    size_t *frame_size)
{
    size_t local_bytes;
    size_t required_bytes;

    if (function->local_count > (SIZE_MAX - 31U) / 4U) {
        return false;
    }
    local_bytes = function->local_count * 4U;
    required_bytes = local_bytes + 16U;
    *frame_size = (required_bytes + 15U) & ~(size_t)15U;
    return true;
}
