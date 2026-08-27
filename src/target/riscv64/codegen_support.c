#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/abi.h"

#include <stdint.h>
#include <stdio.h>

static const char *minic_riscv64_load_instruction(MinicType type) {
    if (minic_type_is_pointer(type) || minic_type_is_double(type)) {
        return "ld";
    }
    if (minic_type_is_float(type)) {
        return "lwu";
    }
    if (!minic_type_is_integer(type) || minic_type_is_int128_integer(type)) {
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
    if (!minic_type_is_integer(type) || minic_type_is_int128_integer(type)) {
        return NULL;
    }
    return (minic_type_is_bool_integer(type) || minic_type_is_char_integer(type)) ? "sb"
           : minic_type_is_short_integer(type)                                    ? "sh"
           : minic_type_is_long_integer(type)                                     ? "sd"
                                                                                  : "sw";
}

static bool minic_riscv64_integer_aggregate_abi(const MinicC0Program *program,
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

static bool minic_riscv64_emit_integer_conversion(FILE *file, MinicType type, const char *register_name) {
    if (register_name == NULL || !minic_type_is_integer(type) ||
        minic_type_is_int128_integer(type)) {
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

bool minic_riscv64_emit_integer_conversion_for_program(FILE *file,
                                                       const MinicC0Program *program,
                                                       MinicType type,
                                                       const char *register_name) {
    MinicType effective_type;

    if (minic_type_is_enum(type)) {
        if (!minic_c0_type_effective_integer_type(program, type, &effective_type)) {
            return false;
        }
        type = effective_type;
    }
    return minic_riscv64_emit_integer_conversion(file, type, register_name);
}

static bool minic_riscv64_emit_scalar_load(FILE *file,
                                    MinicType type,
                                    const char *destination_register,
                                    const char *address_register) {
    const char *instruction;

    instruction = minic_riscv64_load_instruction(type);
    return instruction != NULL && destination_register != NULL && address_register != NULL &&
           fprintf(file, "  %s %s, 0(%s)\n", instruction, destination_register, address_register) >=
               0;
}

static bool minic_riscv64_emit_scalar_store(FILE *file,
                                     MinicType type,
                                     const char *source_register,
                                     const char *address_register) {
    const char *instruction;

    instruction = minic_riscv64_store_instruction(type);
    return instruction != NULL && source_register != NULL && address_register != NULL &&
           fprintf(file, "  %s %s, 0(%s)\n", instruction, source_register, address_register) >= 0;
}

bool minic_riscv64_emit_scalar_load_for_program(FILE *file,
                                                const MinicC0Program *program,
                                                MinicType type,
                                                const char *destination_register,
                                                const char *address_register) {
    MinicType effective_type;

    if (minic_type_is_enum(type)) {
        if (!minic_c0_type_effective_integer_type(program, type, &effective_type)) {
            return false;
        }
        type = effective_type;
    }
    return minic_riscv64_emit_scalar_load(file, type, destination_register, address_register);
}

bool minic_riscv64_emit_scalar_store_for_program(FILE *file,
                                                 const MinicC0Program *program,
                                                 MinicType type,
                                                 const char *source_register,
                                                 const char *address_register) {
    MinicType effective_type;

    if (minic_type_is_enum(type)) {
        if (!minic_c0_type_effective_integer_type(program, type, &effective_type)) {
            return false;
        }
        type = effective_type;
    }
    return minic_riscv64_emit_scalar_store(file, type, source_register, address_register);
}

/* RV64_INT128_PAIR_V1: keep the address stable while a0 becomes the low half. */
