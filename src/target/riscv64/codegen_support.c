#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/abi.h"

#include <stdint.h>
#include <stdio.h>

static bool minic_riscv64_scalar_width(MinicType type, size_t *width) {
    if (width == NULL) {
        return false;
    }
    if (minic_type_is_pointer(type) || minic_type_is_double(type)) {
        *width = 8U;
        return true;
    }
    if (minic_type_is_float(type)) {
        *width = 4U;
        return true;
    }
    if (!minic_type_is_integer(type)) {
        return false;
    }
    *width = (minic_type_is_bool_integer(type) || minic_type_is_char_integer(type)) ? 1U
             : minic_type_is_short_integer(type)                                    ? 2U
             : minic_type_is_long_integer(type)                                     ? 8U
                                                                                    : 4U;
    return true;
}

static const char *minic_riscv64_load_instruction(MinicType type) {
    if (minic_type_is_pointer(type) || minic_type_is_double(type)) {
        return "ld";
    }
    if (minic_type_is_float(type)) {
        return "lwu";
    }
    if (!minic_type_is_integer(type)) {
        return NULL;
    }
    if (minic_type_is_bool_integer(type)) {
        return "lbu";
    }
    if (minic_type_is_char_integer(type)) {
        return minic_type_is_unsigned_integer(type) ? "lbu" : "lb";
    }
    if (minic_type_is_short_integer(type)) {
        return minic_type_is_unsigned_integer(type) ? "lhu" : "lh";
    }
    if (minic_type_is_long_integer(type)) {
        return "ld";
    }
    return minic_type_is_unsigned_integer(type) ? "lwu" : "lw";
}

static const char *minic_riscv64_store_instruction(MinicType type) {
    if (minic_type_is_pointer(type) || minic_type_is_double(type)) {
        return "sd";
    }
    if (minic_type_is_float(type)) {
        return "sw";
    }
    if (!minic_type_is_integer(type)) {
        return NULL;
    }
    return (minic_type_is_bool_integer(type) || minic_type_is_char_integer(type)) ? "sb"
           : minic_type_is_short_integer(type)                                    ? "sh"
           : minic_type_is_long_integer(type)                                     ? "sd"
                                                                                  : "sw";
}

bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *storage_size,
                                         size_t *register_chunks) {
    MinicRiscv64AbiValue value;

    if (storage_size == NULL || register_chunks == NULL ||
        !minic_riscv64_abi_classify_value(program, type, &value) ||
        value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
        return false;
    }
    *storage_size = value.storage_size;
    *register_chunks = value.slot_count;
    return true;
}

bool minic_riscv64_emit_integer_aggregate_load_chunk(FILE *file,
                                                     const MinicC0Program *program,
                                                     MinicType type,
                                                     size_t chunk_index,
                                                     const char *destination_register,
                                                     const char *address_register) {
    size_t chunk_count;
    size_t chunk_offset;
    size_t chunk_size;
    size_t index;
    size_t storage_size;

    if (file == NULL || destination_register == NULL || address_register == NULL ||
        !minic_riscv64_integer_aggregate_abi(program, type, &storage_size, &chunk_count) ||
        chunk_index >= chunk_count || chunk_index > SIZE_MAX / 8U) {
        return false;
    }
    chunk_offset = chunk_index * 8U;
    chunk_size = storage_size - chunk_offset;
    if (chunk_size > 8U) {
        chunk_size = 8U;
    }
    if (chunk_size == 8U) {
        return fprintf(file,
                       "  ld %s, %zu(%s)\n",
                       destination_register,
                       chunk_offset,
                       address_register) >= 0;
    }
    if (chunk_size == 4U) {
        return fprintf(file,
                       "  lwu %s, %zu(%s)\n",
                       destination_register,
                       chunk_offset,
                       address_register) >= 0;
    }
    if (chunk_size == 2U) {
        return fprintf(file,
                       "  lhu %s, %zu(%s)\n",
                       destination_register,
                       chunk_offset,
                       address_register) >= 0;
    }
    if (chunk_size == 1U) {
        return fprintf(file,
                       "  lbu %s, %zu(%s)\n",
                       destination_register,
                       chunk_offset,
                       address_register) >= 0;
    }
    if (fprintf(file, "  li %s, 0\n", destination_register) < 0) {
        return false;
    }
    for (index = 0U; index < chunk_size; ++index) {
        if (fprintf(file, "  lbu t6, %zu(%s)\n", chunk_offset + index, address_register) < 0 ||
            (index != 0U && fprintf(file, "  slli t6, t6, %zu\n", index * 8U) < 0) ||
            fprintf(file, "  or %s, %s, t6\n", destination_register, destination_register) < 0) {
            return false;
        }
    }
    return true;
}

static bool minic_riscv64_local_object(const MinicC0Program *program,
                                       const MinicFunction *function,
                                       const MinicRiscv64FunctionLayout *function_layout,
                                       MinicLocalId local_id,
                                       const MinicLocal **local,
                                       size_t *offset) {
    const MinicLocal *object;
    size_t object_offset;

    if (program == NULL || function == NULL || function_layout == NULL || local == NULL ||
        offset == NULL || local_id < function->local_begin ||
        local_id - function->local_begin >= function->local_count) {
        return false;
    }
    object = minic_c0_program_local(program, local_id);
    if (object == NULL ||
        !minic_riscv64_function_layout_local_offset(
            function_layout, function, local_id, &object_offset) ||
        object_offset > function_layout->local_storage_size) {
        return false;
    }
    *local = object;
    *offset = object_offset;
    return true;
}

static bool minic_riscv64_scalar_object_access(const MinicC0Program *program,
                                               const MinicFunction *function,
                                               const MinicRiscv64FunctionLayout *function_layout,
                                               MinicLocalId local_id,
                                               const MinicLocal **local,
                                               size_t *offset,
                                               size_t *width) {
    const MinicLocal *object;
    size_t object_offset;
    size_t object_width;

    if (offset == NULL || width == NULL ||
        !minic_riscv64_local_object(
            program, function, function_layout, local_id, &object, &object_offset) ||
        !minic_riscv64_scalar_width(object->type, &object_width) ||
        object_width > function_layout->local_storage_size - object_offset) {
        return false;
    }
    *local = object;
    *offset = object_offset;
    *width = object_width;
    return true;
}

static bool minic_riscv64_emit_s0_access(FILE *file,
                                         const char *instruction,
                                         const char *register_name,
                                         size_t offset) {
    if (instruction == NULL || register_name == NULL) {
        return false;
    }
    if (offset <= 2047U) {
        return fprintf(file, "  %s %s, %zu(s0)\n", instruction, register_name, offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  add t2, s0, t2\n"
                   "  %s %s, 0(t2)\n",
                   offset,
                   instruction,
                   register_name) >= 0;
}

void minic_riscv64_set_diagnostic(MinicDiagnostic *diagnostic,
                                  const char *path,
                                  const char *message) {
    if (diagnostic == NULL) {
        return;
    }
    diagnostic->path = path;
    diagnostic->line = 1U;
    diagnostic->column = 1U;
    (void)snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
}

bool minic_riscv64_emit_stack_allocate(FILE *file, size_t size) {
    if (size == 0U) {
        return true;
    }
    if (size <= 2048U) {
        return fprintf(file, "  addi sp, sp, -%zu\n", size) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  sub sp, sp, t2\n",
                   size) >= 0;
}

bool minic_riscv64_emit_stack_release(FILE *file, size_t size) {
    if (size == 0U) {
        return true;
    }
    if (size <= 2047U) {
        return fprintf(file, "  addi sp, sp, %zu\n", size) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  add sp, sp, t2\n",
                   size) >= 0;
}

bool minic_riscv64_emit_sp_store64(FILE *file, const char *register_name, size_t offset) {
    if (offset <= 2047U) {
        return fprintf(file, "  sd %s, %zu(sp)\n", register_name, offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  add t2, sp, t2\n"
                   "  sd %s, 0(t2)\n",
                   offset,
                   register_name) >= 0;
}

bool minic_riscv64_emit_s0_load64(FILE *file, const char *register_name, size_t offset) {
    return minic_riscv64_emit_s0_access(file, "ld", register_name, offset);
}

bool minic_riscv64_emit_sp_load64(FILE *file, const char *register_name, size_t offset) {
    if (offset <= 2047U) {
        return fprintf(file, "  ld %s, %zu(sp)\n", register_name, offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  add t2, sp, t2\n"
                   "  ld %s, 0(t2)\n",
                   offset,
                   register_name) >= 0;
}

bool minic_riscv64_emit_integer_conversion(FILE *file, MinicType type, const char *register_name) {
    if (register_name == NULL || !minic_type_is_integer(type)) {
        return false;
    }
    if (minic_type_is_bool_integer(type)) {
        return fprintf(file, "  snez %s, %s\n", register_name, register_name) >= 0;
    }
    if (minic_type_is_char_integer(type)) {
        if (minic_type_is_unsigned_integer(type)) {
            return fprintf(file, "  andi %s, %s, 255\n", register_name, register_name) >= 0;
        }
        return fprintf(file,
                       "  slli %s, %s, 56\n"
                       "  srai %s, %s, 56\n",
                       register_name,
                       register_name,
                       register_name,
                       register_name) >= 0;
    }
    if (minic_type_is_short_integer(type)) {
        return fprintf(file,
                       "  slli %s, %s, 48\n"
                       "  %s %s, %s, 48\n",
                       register_name,
                       register_name,
                       minic_type_is_unsigned_integer(type) ? "srli" : "srai",
                       register_name,
                       register_name) >= 0;
    }
    if (minic_type_is_long_integer(type)) {
        return true;
    }
    if (minic_type_is_unsigned_integer(type)) {
        return fprintf(file,
                       "  slli %s, %s, 32\n"
                       "  srli %s, %s, 32\n",
                       register_name,
                       register_name,
                       register_name,
                       register_name) >= 0;
    }
    return fprintf(file, "  addiw %s, %s, 0\n", register_name, register_name) >= 0;
}

bool minic_riscv64_emit_scalar_load(FILE *file,
                                    MinicType type,
                                    const char *destination_register,
                                    const char *address_register) {
    const char *instruction;

    instruction = minic_riscv64_load_instruction(type);
    return instruction != NULL && destination_register != NULL && address_register != NULL &&
           fprintf(file, "  %s %s, 0(%s)\n", instruction, destination_register, address_register) >=
               0;
}

bool minic_riscv64_emit_scalar_store(FILE *file,
                                     MinicType type,
                                     const char *source_register,
                                     const char *address_register) {
    const char *instruction;

    instruction = minic_riscv64_store_instruction(type);
    return instruction != NULL && source_register != NULL && address_register != NULL &&
           fprintf(file, "  %s %s, 0(%s)\n", instruction, source_register, address_register) >= 0;
}

bool minic_riscv64_emit_object_address(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicFunction *function,
                                       const MinicRiscv64FunctionLayout *function_layout,
                                       MinicLocalId local_id) {
    const MinicLocal *local;
    size_t offset;

    if (!minic_riscv64_local_object(
            program, function, function_layout, local_id, &local, &offset)) {
        return false;
    }
    (void)local;
    if (offset <= 2047U) {
        return fprintf(file, "  addi a0, s0, %zu\n", offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  add a0, s0, t2\n",
                   offset) >= 0;
}

bool minic_riscv64_emit_object_load(FILE *file,
                                    const MinicC0Program *program,
                                    const MinicFunction *function,
                                    const MinicRiscv64FunctionLayout *function_layout,
                                    MinicLocalId local_id) {
    const MinicLocal *local;
    size_t offset;
    size_t width;
    const char *instruction;

    if (!minic_riscv64_scalar_object_access(
            program, function, function_layout, local_id, &local, &offset, &width)) {
        return false;
    }
    (void)width;
    instruction = minic_riscv64_load_instruction(local->type);
    return minic_riscv64_emit_s0_access(file, instruction, "a0", offset);
}

bool minic_riscv64_emit_object_store_register(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
                                              const MinicRiscv64FunctionLayout *function_layout,
                                              MinicLocalId local_id,
                                              const char *register_name) {
    const MinicLocal *local;
    size_t offset;
    size_t width;
    const char *instruction;

    if (register_name == NULL ||
        !minic_riscv64_scalar_object_access(
            program, function, function_layout, local_id, &local, &offset, &width)) {
        return false;
    }
    (void)width;
    instruction = minic_riscv64_store_instruction(local->type);
    return minic_riscv64_emit_s0_access(file, instruction, register_name, offset);
}

bool minic_riscv64_emit_integer_aggregate_local_chunk(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicLocalId local_id,
    size_t chunk_index,
    const char *register_name) {
    const MinicLocal *local;
    const char *instruction;
    size_t chunk_count;
    size_t chunk_offset;
    size_t chunk_size;
    size_t index;
    size_t local_offset;
    size_t storage_size;

    if (register_name == NULL ||
        !minic_riscv64_local_object(
            program, function, function_layout, local_id, &local, &local_offset) ||
        !minic_riscv64_integer_aggregate_abi(program, local->type, &storage_size, &chunk_count) ||
        chunk_index >= chunk_count || chunk_index > (SIZE_MAX - local_offset) / 8U) {
        return false;
    }
    chunk_offset = local_offset + chunk_index * 8U;
    chunk_size = storage_size - chunk_index * 8U;
    if (chunk_size > 8U) {
        chunk_size = 8U;
    }
    if (chunk_offset > function_layout->local_storage_size ||
        chunk_size > function_layout->local_storage_size - chunk_offset) {
        return false;
    }
    instruction = chunk_size == 8U   ? "sd"
                  : chunk_size == 4U ? "sw"
                  : chunk_size == 2U ? "sh"
                  : chunk_size == 1U ? "sb"
                                     : NULL;
    if (instruction != NULL) {
        return minic_riscv64_emit_s0_access(file, instruction, register_name, chunk_offset);
    }
    if (fprintf(file, "  mv t1, %s\n", register_name) < 0) {
        return false;
    }
    for (index = 0U; index < chunk_size; ++index) {
        if (!minic_riscv64_emit_s0_access(file, "sb", "t1", chunk_offset + index) ||
            (index + 1U < chunk_size && fprintf(file, "  srli t1, t1, 8\n") < 0)) {
            return false;
        }
    }
    return true;
}

bool minic_riscv64_emit_object_store(FILE *file,
                                     const MinicC0Program *program,
                                     const MinicFunction *function,
                                     const MinicRiscv64FunctionLayout *function_layout,
                                     MinicLocalId local_id) {
    return minic_riscv64_emit_object_store_register(
        file, program, function, function_layout, local_id, "a0");
}

bool minic_riscv64_frame_layout_from_function_layout(
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicRiscv64FrameLayout *layout) {
    MinicRiscv64AbiCursor abi_cursor;
    MinicRiscv64AbiValue return_value;
    size_t hidden_return_size;
    size_t metadata_size;
    size_t parameter_index;
    size_t required_bytes;
    size_t varargs_size;

    if (program == NULL || function == NULL || function_layout == NULL || layout == NULL ||
        function->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        !minic_riscv64_abi_cursor_initialize_for_return(
            program, function->return_type, &abi_cursor, &return_value)) {
        return false;
    }

    for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
        const MinicLocal *parameter;
        MinicRiscv64AbiArgumentLocation location;

        parameter = minic_c0_program_local(program, function->local_begin + parameter_index);
        if (parameter == NULL || !minic_riscv64_abi_place_argument(
                                     program, parameter->type, true, &abi_cursor, &location)) {
            return false;
        }
    }
    if (function->is_variadic && abi_cursor.stack_slot_count != 0U) {
        return false;
    }

    hidden_return_size = return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ? 8U : 0U;
    metadata_size = 16U + hidden_return_size;
    varargs_size = function->is_variadic ? (8U - abi_cursor.integer_register_count) * 8U : 0U;
    if (function_layout->local_storage_size > SIZE_MAX - metadata_size ||
        function_layout->local_storage_size + metadata_size > SIZE_MAX - varargs_size) {
        return false;
    }
    required_bytes = function_layout->local_storage_size + metadata_size + varargs_size;
    if (required_bytes > SIZE_MAX - 15U) {
        return false;
    }

    layout->frame_size = (required_bytes + 15U) & ~(size_t)15U;
    layout->varargs_size = varargs_size;
    layout->varargs_offset = layout->frame_size - varargs_size;
    if (layout->varargs_offset < metadata_size ||
        function_layout->local_storage_size > layout->varargs_offset - metadata_size) {
        return false;
    }
    layout->saved_ra_offset = layout->varargs_offset - 8U;
    layout->saved_s0_offset = layout->varargs_offset - 16U;
    layout->has_indirect_return = return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT;
    layout->indirect_return_offset =
        layout->has_indirect_return ? layout->varargs_offset - 24U : 0U;
    layout->integer_parameter_count = abi_cursor.integer_register_count;
    return true;
}
