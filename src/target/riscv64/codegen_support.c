#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/abi.h"
#include "target/riscv/typed_memory.h"

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

static bool minic_riscv64_integer_aggregate_abi(
    const MinicC0Program *program, MinicType type,
    size_t *storage_size, size_t *register_chunks, bool lp64_soft_float) {
    MinicRiscv64AbiValue value;

    if (storage_size == NULL || register_chunks == NULL ||
        !minic_riscv64_abi_classify_value_profile(
            program, type, lp64_soft_float, &value) ||
        value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
        return false;
    }
    *storage_size = value.storage_size;
    *register_chunks = value.slot_count;
    return true;
}

static bool rv64_emit_integer_aggregate_load_chunk_known_shape(
    FILE *file, size_t storage_size, size_t chunk_count,
    size_t chunk_index, const char *destination_register,
    const char *address_register, size_t known_frame_offset) {
    size_t chunk_offset;
    size_t chunk_size;
    size_t index;
    bool aligned_for_native_load;

    if (file == NULL || destination_register == NULL || address_register == NULL ||
        chunk_index >= chunk_count || chunk_index > SIZE_MAX / 8U) {
        return false;
    }
    chunk_offset = chunk_index * 8U;
    if (chunk_offset >= storage_size)
        return false;
    chunk_size = storage_size - chunk_offset;
    if (chunk_size > 8U) {
        chunk_size = 8U;
    }
    /* Only a proven RV64 frame address can use a native multi-byte load.
     * A record member inside an enclosing packed struct may be unaligned
     * even when the member's own record type has natural alignment. The
     * legacy helper passes SIZE_MAX and takes the byte-gather path. */
    aligned_for_native_load = known_frame_offset != SIZE_MAX &&
        known_frame_offset <= SIZE_MAX - chunk_offset &&
        (known_frame_offset + chunk_offset) % chunk_size == 0U;
    if (chunk_size == 8U && aligned_for_native_load) {
        return fprintf(file,
                       "  ld %s, %zu(%s)\n",
                       destination_register,
                       chunk_offset,
                       address_register) >= 0;
    }
    if (chunk_size == 4U && aligned_for_native_load) {
        return fprintf(file,
                       "  lwu %s, %zu(%s)\n",
                       destination_register,
                       chunk_offset,
                       address_register) >= 0;
    }
    if (chunk_size == 2U && aligned_for_native_load) {
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

bool minic_riscv64_emit_integer_aggregate_load_chunk_profile(
    FILE *file, const MinicC0Program *program, MinicType type,
    size_t chunk_index, const char *destination_register,
    const char *address_register, bool lp64_soft_float,
    size_t known_frame_offset) {
    size_t size, chunks;
    return minic_riscv64_integer_aggregate_abi(
               program, type, &size, &chunks, lp64_soft_float) &&
           rv64_emit_integer_aggregate_load_chunk_known_shape(
               file, size, chunks, chunk_index, destination_register,
               address_register, known_frame_offset);
}

bool minic_riscv64_emit_integer_aggregate_load_chunk_core_profile(
    FILE *file, const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    size_t chunk_index, const char *destination_register,
    const char *address_register, bool lp64_soft_float,
    size_t known_frame_offset) {
    MinicRiscv64AbiValue value;
    return minic_riscv64_abi_classify_core_value_profile(
               types, type_id, lp64_soft_float, &value) &&
           value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
           rv64_emit_integer_aggregate_load_chunk_known_shape(
               file, value.storage_size, value.slot_count, chunk_index,
               destination_register, address_register, known_frame_offset);
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
    const MinicRiscvFrameAddressPolicy policy =
        minic_riscv64_machine_frame_policy(false);
    return minic_riscv_frame_emit_word(file, &policy, true, register_name, offset);
}

bool minic_riscv64_emit_sp_load64(FILE *file, const char *register_name, size_t offset) {
    const MinicRiscvFrameAddressPolicy policy =
        minic_riscv64_machine_frame_policy(false);
    return minic_riscv_frame_emit_word(file, &policy, false, register_name, offset);
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

bool minic_riscv64_emit_core_integer_conversion(
    FILE *file, const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    const char *register_name) {
    const MinicCoreType *type = minic_core_type_get(
        types, minic_core_type_unqualified(types, type_id));
    unsigned int bits;
    bool sign;
    if (file == NULL || register_name == NULL || type == NULL ||
        type->kind != MINIC_CORE_TYPE_INTEGER || !type->has_layout)
        return false;
    bits = type->value.integer.bit_width;
    sign = type->value.integer.is_signed;
    if (type->value.integer.is_bool)
        return bits == 8U && fprintf(file, "  snez %s, %s\n",
                                     register_name, register_name) >= 0;
    if (bits == 8U) {
        if (!sign) return fprintf(file, "  andi %s, %s, 255\n",
                                  register_name, register_name) >= 0;
        return fprintf(file,
                       "  slli %s, %s, 56\n"
                       "  srai %s, %s, 56\n",
                       register_name, register_name,
                       register_name, register_name) >= 0;
    }
    if (bits == 16U)
        return fprintf(file,
                       "  slli %s, %s, 48\n"
                       "  %s %s, %s, 48\n",
                       register_name, register_name,
                       sign ? "srai" : "srli",
                       register_name, register_name) >= 0;
    if (bits == 32U) {
        if (!sign)
            return fprintf(file,
                           "  slli %s, %s, 32\n"
                           "  srli %s, %s, 32\n",
                           register_name, register_name,
                           register_name, register_name) >= 0;
        return fprintf(file, "  addiw %s, %s, 0\n",
                       register_name, register_name) >= 0;
    }
    return bits == 64U;
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

static bool minic_riscv64_emit_safe_scalar(
    FILE *file, const MinicC0Program *program, MinicType type,
    bool store, const char *value_register, const char *address_register,
    const char *scratch_register, size_t proven_alignment) {
    MinicType effective = type;
    size_t width, type_alignment;
    const char *op;
    if (minic_type_is_enum(type) &&
        !minic_c0_type_effective_integer_type(program, type, &effective))
        return false;
    op = store ? minic_riscv64_store_instruction(effective) :
                 minic_riscv64_load_instruction(effective);
    if (op == NULL ||
        !minic_data_layout_type(minic_default_data_layout(), program,
                                effective, &width, &type_alignment))
        return false;
    (void)type_alignment;
    return minic_riscv_emit_typed_memory(
        file, store, 64U, width,
        !store && minic_type_is_signed_integer(effective),
        op, value_register, address_register, scratch_register,
        proven_alignment, "rv64_scalar");
}

bool minic_riscv64_emit_safe_scalar_load(
    FILE *file, const MinicC0Program *program, MinicType type,
    const char *destination_register, const char *address_register,
    const char *scratch_register, size_t proven_alignment) {
    return minic_riscv64_emit_safe_scalar(
        file, program, type, false, destination_register,
        address_register, scratch_register, proven_alignment);
}

bool minic_riscv64_emit_safe_scalar_store(
    FILE *file, const MinicC0Program *program, MinicType type,
    const char *source_register, const char *address_register,
    const char *scratch_register, size_t proven_alignment) {
    return minic_riscv64_emit_safe_scalar(
        file, program, type, true, source_register,
        address_register, scratch_register, proven_alignment);
}

static bool rv64_emit_safe_core_scalar(
    FILE *file, const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    bool store, const char *value_register, const char *address_register,
    const char *scratch_register, size_t proven_alignment) {
    const MinicCoreType *type = minic_core_type_get(
        types, minic_core_type_unqualified(types, type_id));
    const char *opcode;
    size_t bytes;
    bool is_signed = false;
    if (type == NULL || !type->has_layout) return false;
    bytes = type->size;
    if (type->kind == MINIC_CORE_TYPE_POINTER) {
        if (bytes != 8U) return false;
        opcode = store ? "sd" : "ld";
    } else if (type->kind == MINIC_CORE_TYPE_FLOAT) {
        if (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 && bytes == 4U)
            opcode = store ? "sw" : "lwu";
        else if (type->value.floating.format == MINIC_CORE_FLOAT_BINARY64 && bytes == 8U)
            opcode = store ? "sd" : "ld";
        else return false;
    } else if (type->kind == MINIC_CORE_TYPE_INTEGER) {
        if (bytes < 1U || bytes > 8U ||
            type->value.integer.bit_width != bytes * 8U ||
            (type->value.integer.is_bool && bytes != 1U)) return false;
        is_signed = !type->value.integer.is_bool && type->value.integer.is_signed;
        switch (bytes) {
        case 1U: opcode = store ? "sb" : (is_signed ? "lb" : "lbu"); break;
        case 2U: opcode = store ? "sh" : (is_signed ? "lh" : "lhu"); break;
        case 4U: opcode = store ? "sw" : (is_signed ? "lw" : "lwu"); break;
        case 8U: opcode = store ? "sd" : "ld"; break;
        default: return false;
        }
    } else return false;
    return minic_riscv_emit_typed_memory(
        file, store, 64U, bytes, !store && is_signed, opcode,
        value_register, address_register, scratch_register,
        proven_alignment, "rv64_scalar");
}

bool minic_riscv64_emit_safe_core_scalar_load(
    FILE *file, const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    const char *destination_register, const char *address_register,
    const char *scratch_register, size_t proven_alignment) {
    return rv64_emit_safe_core_scalar(file, types, type_id, false,
        destination_register, address_register, scratch_register,
        proven_alignment);
}

bool minic_riscv64_emit_safe_core_scalar_store(
    FILE *file, const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    const char *source_register, const char *address_register,
    const char *scratch_register, size_t proven_alignment) {
    return rv64_emit_safe_core_scalar(file, types, type_id, true,
        source_register, address_register, scratch_register,
        proven_alignment);
}

/* RV64_INT128_PAIR_V1: keep the address stable while a0 becomes the low half. */
