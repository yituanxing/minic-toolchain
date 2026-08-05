#include "target/riscv64/codegen.h"
#include "target/riscv64/codegen_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *const minic_riscv64_argument_registers[8] = {
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
};

static bool minic_riscv64_alignment_power(
    size_t alignment,
    unsigned int *power)
{
    unsigned int result;
    size_t value;

    if (alignment == 0U || power == NULL) {
        return false;
    }
    result = 0U;
    value = alignment;
    while (value > 1U) {
        if ((value & 1U) != 0U) {
            return false;
        }
        value >>= 1U;
        result += 1U;
    }
    *power = result;
    return true;
}

static bool minic_riscv64_emit_global_object(
    FILE *file,
    const MinicGlobalObject *object)
{
    unsigned int alignment_power;
    size_t initializer_index;

    if (file == NULL || object == NULL || object->name_length == 0U ||
        object->storage_size == 0U || object->alignment == 0U ||
        object->initializer_count > object->storage_size / 4U ||
        !minic_riscv64_alignment_power(
            object->alignment,
            &alignment_power)) {
        return false;
    }

    if (fprintf(
            file,
            "%s\n",
            object->is_read_only ? ".section .rodata" : ".data") < 0) {
        return false;
    }
    if (!object->is_internal &&
        fprintf(file, ".globl %s\n", object->name) < 0) {
        return false;
    }
    if (fprintf(
            file,
            ".type %s, @object\n"
            ".align %u\n"
            "%s:\n",
            object->name,
            alignment_power,
            object->name) < 0) {
        return false;
    }
    for (initializer_index = 0U;
         initializer_index < object->initializer_count;
         ++initializer_index) {
        if (fprintf(
                file,
                "  .word %d\n",
                object->initializer_values[initializer_index]) < 0) {
            return false;
        }
    }
    return fprintf(
        file,
        ".size %s, %zu\n",
        object->name,
        object->storage_size) >= 0;
}

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
    if (success && function->parameter_count > 8U) {
        return false;
    }
    if (success) {
        size_t parameter_index;

        for (parameter_index = 0U;
             success && parameter_index < function->parameter_count;
             ++parameter_index) {
            success = minic_riscv64_emit_object_store_register(
                file,
                program,
                function,
                function->local_begin + parameter_index,
                minic_riscv64_argument_registers[parameter_index]);
        }
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
    size_t global_index;
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

    success = true;
    for (global_index = 0U;
         success && global_index < program->global_object_count;
         ++global_index) {
        success = minic_riscv64_emit_global_object(
            file,
            &program->global_objects[global_index]);
    }
    if (success) {
        success = fprintf(file, ".text\n") >= 0;
    }

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
