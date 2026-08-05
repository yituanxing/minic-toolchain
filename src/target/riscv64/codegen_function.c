#include "target/riscv64/codegen.h"
#include "target/riscv64/codegen_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static bool minic_riscv64_emit_function(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    size_t *label_counter)
{
    size_t frame_size;
    bool success;

    if (function == NULL || !function->is_defined ||
        function->name_length == 0U ||
        function->body_block >= program->block_count ||
        function->parameter_count > 2U ||
        !minic_riscv64_frame_size(function, &frame_size)) {
        return false;
    }

    success = fprintf(
        file,
        ".globl %s\n"
        ".type %s, @function\n"
        "%s:\n",
        function->name,
        function->name,
        function->name) >= 0;
    if (success) {
        success = minic_riscv64_emit_stack_allocate(file, frame_size);
    }
    if (success) {
        success = minic_riscv64_emit_sp_store64(file, "ra", frame_size - 8U) &&
                  minic_riscv64_emit_sp_store64(file, "s0", frame_size - 16U) &&
                  fprintf(file, "  mv s0, sp\n") >= 0;
    }
    if (success && function->parameter_count >= 1U) {
        success = fprintf(file, "  sw a0, 0(s0)\n") >= 0;
    }
    if (success && function->parameter_count >= 2U) {
        success = fprintf(file, "  sw a1, 4(s0)\n") >= 0;
    }
    if (success) {
        success = minic_riscv64_emit_block(
            file,
            program,
            function,
            function->body_block,
            label_counter);
    }
    if (success) {
        success = fprintf(
            file,
            "  li a0, 0\n"
            ".L%s_return:\n",
            function->name) >= 0;
    }
    if (success) {
        success = minic_riscv64_emit_sp_load64(file, "ra", frame_size - 8U) &&
                  minic_riscv64_emit_sp_load64(file, "s0", frame_size - 16U);
    }
    if (success) {
        success = minic_riscv64_emit_stack_release(file, frame_size);
    }
    if (success) {
        success = fprintf(
            file,
            "  ret\n"
            ".size %s, .-%s\n",
            function->name,
            function->name) >= 0;
    }
    return success;
}

bool minic_riscv64_write_c0_program(
    const char *path,
    const MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    FILE *file;
    size_t function_index;
    size_t label_counter;
    bool success;

    {
        const MinicFunction *entry_function;

        entry_function = minic_c0_program_function(
            program,
            program->entry_function);
        if (entry_function == NULL || !entry_function->is_defined ||
            program->function_count == 0U) {
            minic_riscv64_set_diagnostic(
                diagnostic,
                path,
                "entry function is missing or invalid");
            return false;
        }
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        char message[256];

        (void)snprintf(
            message,
            sizeof(message),
            "cannot open output: %s",
            strerror(errno));
        minic_riscv64_set_diagnostic(diagnostic, path, message);
        return false;
    }

    success = fprintf(file, ".text\n") >= 0;
    label_counter = 0U;
    for (function_index = 0U;
         success && function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function;

        function = &program->functions[function_index];
        if (!function->is_defined) {
            continue;
        }
        success = minic_riscv64_emit_function(
            file,
            program,
            function,
            &label_counter);
    }

    if (!success) {
        minic_riscv64_set_diagnostic(
            diagnostic,
            path,
            "cannot write RISC-V assembly");
    }
    if (fclose(file) != 0 && success) {
        minic_riscv64_set_diagnostic(
            diagnostic,
            path,
            "cannot close RISC-V assembly output");
        success = false;
    }
    return success;
}
