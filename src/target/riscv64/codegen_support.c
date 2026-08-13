#include "target/riscv64/codegen_internal.h"

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

static bool minic_riscv64_integer_aggregate_member_type(const MinicC0Program *program,
                                                        MinicType type) {
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return true;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL &&
               minic_riscv64_integer_aggregate_member_type(program, array_type->element_type);
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;

        record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) {
            return false;
        }
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field;

            field = minic_c0_record_field(record, field_index);
            if (field == NULL ||
                !minic_riscv64_integer_aggregate_member_type(program, field->type)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
                                         MinicType type,
                                         size_t *storage_size,
                                         size_t *register_chunks) {
    size_t alignment;
    size_t size;

    if (program == NULL || storage_size == NULL || register_chunks == NULL ||
        !minic_type_is_record(type) ||
        !minic_riscv64_integer_aggregate_member_type(program, type) ||
        !minic_riscv64_type_layout(program, type, &size, &alignment) || size == 0U || size > 16U) {
        return false;
    }
    (void)alignment;
    *storage_size = size;
    *register_chunks = (size + 7U) / 8U;
    return true;
}

static bool minic_riscv64_integer_aggregate_chunk_size(size_t storage_size,
                                                       size_t chunk_index,
                                                       size_t *chunk_size) {
    size_t chunk_offset;
    size_t remaining;

    if (chunk_size == NULL || storage_size == 0U || storage_size > 16U || chunk_index >= 2U ||
        chunk_index > SIZE_MAX / 8U) {
        return false;
    }
    chunk_offset = chunk_index * 8U;
    if (chunk_offset >= storage_size) {
        return false;
    }
    remaining = storage_size - chunk_offset;
    *chunk_size = remaining < 8U ? remaining : 8U;
    return true;
}

bool minic_riscv64_emit_integer_aggregate_chunk_load(FILE *file,
                                                     size_t storage_size,
                                                     size_t chunk_index,
                                                     const char *destination_register,
                                                     const char *address_register) {
    size_t chunk_size;
    size_t chunk_offset;
    size_t byte_index;

    if (file == NULL || destination_register == NULL || address_register == NULL ||
        !minic_riscv64_integer_aggregate_chunk_size(storage_size, chunk_index, &chunk_size)) {
        return false;
    }
    chunk_offset = chunk_index * 8U;
    if (chunk_offset == 0U) {
        if (fprintf(file, "  mv t5, %s\n", address_register) < 0) {
            return false;
        }
    } else if (chunk_offset <= 2047U) {
        if (fprintf(file, "  addi t5, %s, %zu\n", address_register, chunk_offset) < 0) {
            return false;
        }
    } else if (fprintf(file,
                       "  li t6, %zu\n"
                       "  add t5, %s, t6\n",
                       chunk_offset,
                       address_register) < 0) {
        return false;
    }

    switch (chunk_size) {
    case 8U:
        return fprintf(file, "  ld %s, 0(t5)\n", destination_register) >= 0;
    case 4U:
        return fprintf(file, "  lwu %s, 0(t5)\n", destination_register) >= 0;
    case 2U:
        return fprintf(file, "  lhu %s, 0(t5)\n", destination_register) >= 0;
    case 1U:
        return fprintf(file, "  lbu %s, 0(t5)\n", destination_register) >= 0;
    default:
        break;
    }

    if (fprintf(file, "  li %s, 0\n", destination_register) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < chunk_size; ++byte_index) {
        if (fprintf(file, "  lbu t6, %zu(t5)\n", byte_index) < 0 ||
            (byte_index != 0U && fprintf(file, "  slli t6, t6, %zu\n", byte_index * 8U) < 0) ||
            fprintf(file, "  or %s, %s, t6\n", destination_register, destination_register) < 0) {
            return false;
        }
    }
    return true;
}

static bool minic_riscv64_local_object(const MinicC0Program *program,
                                       const MinicFunction *function,
                                       MinicLocalId local_id,
                                       const MinicLocal **local) {
    const MinicLocal *object;

    if (program == NULL || function == NULL || local == NULL || local_id < function->local_begin ||
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

static bool minic_riscv64_scalar_object_access(const MinicC0Program *program,
                                               const MinicFunction *function,
                                               MinicLocalId local_id,
                                               const MinicLocal **local,
                                               size_t *width) {
    const MinicLocal *object;
    size_t object_width;

    if (!minic_riscv64_local_object(program, function, local_id, &object) ||
        !minic_riscv64_scalar_width(object->type, &object_width) ||
        object_width > function->local_storage_size - object->storage_offset) {
        return false;
    }
    *local = object;
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
                                       MinicLocalId local_id) {
    const MinicLocal *local;

    if (!minic_riscv64_local_object(program, function, local_id, &local)) {
        return false;
    }
    if (local->storage_offset <= 2047U) {
        return fprintf(file, "  addi a0, s0, %zu\n", local->storage_offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  add a0, s0, t2\n",
                   local->storage_offset) >= 0;
}

bool minic_riscv64_emit_object_load(FILE *file,
                                    const MinicC0Program *program,
                                    const MinicFunction *function,
                                    MinicLocalId local_id) {
    const MinicLocal *local;
    size_t width;
    const char *instruction;

    if (!minic_riscv64_scalar_object_access(program, function, local_id, &local, &width)) {
        return false;
    }
    (void)width;
    instruction = minic_riscv64_load_instruction(local->type);
    return minic_riscv64_emit_s0_access(file, instruction, "a0", local->storage_offset);
}

bool minic_riscv64_emit_object_store_register(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
                                              MinicLocalId local_id,
                                              const char *register_name) {
    const MinicLocal *local;
    size_t width;
    const char *instruction;

    if (register_name == NULL ||
        !minic_riscv64_scalar_object_access(program, function, local_id, &local, &width)) {
        return false;
    }
    (void)width;
    instruction = minic_riscv64_store_instruction(local->type);
    return minic_riscv64_emit_s0_access(file, instruction, register_name, local->storage_offset);
}

bool minic_riscv64_emit_integer_aggregate_local_chunk(FILE *file,
                                                      const MinicC0Program *program,
                                                      const MinicFunction *function,
                                                      MinicLocalId local_id,
                                                      size_t chunk_index,
                                                      const char *register_name) {
    const MinicLocal *local;
    size_t chunks;
    size_t storage_size;
    size_t chunk_size;
    size_t chunk_offset;
    size_t byte_index;

    if (register_name == NULL || !minic_riscv64_local_object(program, function, local_id, &local) ||
        !minic_riscv64_integer_aggregate_abi(program, local->type, &storage_size, &chunks) ||
        chunk_index >= chunks ||
        !minic_riscv64_integer_aggregate_chunk_size(storage_size, chunk_index, &chunk_size) ||
        chunk_index > (SIZE_MAX - local->storage_offset) / 8U) {
        return false;
    }
    chunk_offset = local->storage_offset + chunk_index * 8U;
    if (chunk_offset > function->local_storage_size ||
        function->local_storage_size - chunk_offset < chunk_size) {
        return false;
    }

    switch (chunk_size) {
    case 8U:
        return minic_riscv64_emit_s0_access(file, "sd", register_name, chunk_offset);
    case 4U:
        return minic_riscv64_emit_s0_access(file, "sw", register_name, chunk_offset);
    case 2U:
        return minic_riscv64_emit_s0_access(file, "sh", register_name, chunk_offset);
    case 1U:
        return minic_riscv64_emit_s0_access(file, "sb", register_name, chunk_offset);
    default:
        break;
    }

    if (fprintf(file, "  mv t6, %s\n", register_name) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < chunk_size; ++byte_index) {
        if (!minic_riscv64_emit_s0_access(file, "sb", "t6", chunk_offset + byte_index) ||
            (byte_index + 1U < chunk_size && fprintf(file, "  srli t6, t6, 8\n") < 0)) {
            return false;
        }
    }
    return true;
}

bool minic_riscv64_emit_object_store(FILE *file,
                                     const MinicC0Program *program,
                                     const MinicFunction *function,
                                     MinicLocalId local_id) {
    return minic_riscv64_emit_object_store_register(file, program, function, local_id, "a0");
}

bool minic_riscv64_frame_layout(const MinicC0Program *program,
                                const MinicFunction *function,
                                MinicRiscv64FrameLayout *layout) {
    size_t integer_parameter_count;
    size_t parameter_index;
    size_t required_bytes;
    size_t varargs_size;

    if (program == NULL || function == NULL || layout == NULL ||
        function->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
        return false;
    }

    integer_parameter_count = 0U;
    for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
        const MinicLocal *parameter;

        parameter = minic_c0_program_local(program, function->local_begin + parameter_index);
        if (parameter == NULL) {
            return false;
        }
        if (minic_type_is_double(parameter->type) || minic_type_is_float(parameter->type)) {
            continue;
        }
        if (minic_type_is_record(parameter->type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            if (!minic_riscv64_integer_aggregate_abi(
                    program, parameter->type, &aggregate_size, &aggregate_chunks) ||
                integer_parameter_count > SIZE_MAX - aggregate_chunks) {
                return false;
            }
            (void)aggregate_size;
            integer_parameter_count += aggregate_chunks;
            continue;
        }
        if (!minic_type_is_integer(parameter->type) && !minic_type_is_pointer(parameter->type)) {
            return false;
        }
        integer_parameter_count += 1U;
    }
    if (function->is_variadic && integer_parameter_count > 8U) {
        return false;
    }

    varargs_size = function->is_variadic ? (8U - integer_parameter_count) * 8U : 0U;
    if (function->local_storage_size > SIZE_MAX - 16U ||
        function->local_storage_size + 16U > SIZE_MAX - varargs_size) {
        return false;
    }
    required_bytes = function->local_storage_size + 16U + varargs_size;
    if (required_bytes > SIZE_MAX - 15U) {
        return false;
    }

    layout->frame_size = (required_bytes + 15U) & ~(size_t)15U;
    layout->varargs_size = varargs_size;
    layout->varargs_offset = layout->frame_size - varargs_size;
    if (layout->varargs_offset < 16U ||
        function->local_storage_size > layout->varargs_offset - 16U) {
        return false;
    }
    layout->saved_ra_offset = layout->varargs_offset - 8U;
    layout->saved_s0_offset = layout->varargs_offset - 16U;
    layout->integer_parameter_count = integer_parameter_count;
    return true;
}
