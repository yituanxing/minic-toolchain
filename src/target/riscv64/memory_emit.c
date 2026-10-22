#include "target/riscv64/memory_emit.h"
#include "target/riscv64/frame_access.h"
#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/legalize.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <stdio.h>

/* All address and size decisions remain in existing Frame and Legalize APIs. */
static bool emit_field_address(FILE *file,
                               const MinicRiscv64CoreFrame *frame,
                               const MinicCoreInstruction *instruction) {
    size_t field_offset;

    if (!minic_riscv64_core_field_address_supported(instruction, &field_offset) ||
        !minic_riscv64_frame_load_value(file, frame, instruction->value.field_address.base, "t0")) {
        return false;
    }
    if (field_offset != 0U) {
        if (field_offset <= 2047U) {
            if (fprintf(file, "  addi t0, t0, %zu\n", field_offset) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t1, %zu\n"
                           "  add t0, t0, t1\n",
                           field_offset) < 0) {
            return false;
        }
    }
    return minic_riscv64_frame_store_value(file, frame, instruction->result, "t0");
}

/* Byte-exact forward copy. RECORD_LOAD and RECORD_COPY previously contained
 * independent copies of this 2048-byte loop with reversed register roles. */
static bool emit_record_copy_bytes(FILE *file,
                                   const char *destination_register,
                                   const char *source_register,
                                   size_t size) {
    size_t copied = 0U;
    if (file == NULL || destination_register == NULL || source_register == NULL)
        return false;
    while (copied < size) {
        size_t chunk = size - copied;
        size_t offset;
        if (chunk > 2048U) chunk = 2048U;
        for (offset = 0U; offset < chunk; ++offset) {
            if (fprintf(file,
                        "  lbu t2, %zu(%s)\n"
                        "  sb t2, %zu(%s)\n",
                        offset, source_register, offset, destination_register) < 0)
                return false;
        }
        copied += chunk;
        if (copied < size &&
            fprintf(file,
                    "  li t3, %zu\n"
                    "  add %s, %s, t3\n"
                    "  add %s, %s, t3\n",
                    chunk, destination_register, destination_register,
                    source_register, source_register) < 0)
            return false;
    }
    return true;
}

bool minic_riscv64_emit_memory_instruction(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    if (file == NULL || function == NULL || frame == NULL || instruction == NULL)
        return false;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_LOAD:
        if (minic_type_is_int128_integer(instruction->type) ||
            minic_type_is_long_double(instruction->type)) {
            if (!minic_riscv64_frame_load_value(file, frame, instruction->value.load.address, "t0") ||
                fprintf(file, "  ld t1, 0(t0)\n  ld t2, 8(t0)\n") < 0) {
                return false;
            }
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t1", "t2");
        }
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.load.address, "t0") ||
            !minic_riscv64_emit_scalar_load_for_program(
                file, program, instruction->type, "t1", "t0")) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreValueId stored_value;
        MinicType stored_type;

        stored_value = instruction->value.store.stored_value;
        if (stored_value >= function->value_count) {
            return false;
        }
        stored_type = function->values[stored_value].type;
        if (minic_type_is_int128_integer(stored_type) ||
            minic_type_is_long_double(stored_type)) {
            return minic_riscv64_frame_load_value(file, frame, instruction->value.store.address, "t0") &&
                   minic_riscv64_frame_load_int128(file, frame, stored_value, "t1", "t2") &&
                   fprintf(file, "  sd t1, 0(t0)\n  sd t2, 8(t0)\n") >= 0;
        }
        return minic_riscv64_frame_load_value(file, frame, instruction->value.store.address, "t0") &&
               minic_riscv64_frame_load_value(file, frame, stored_value, "t1") &&
               minic_riscv64_emit_scalar_store_for_program(file, program, stored_type, "t1", "t0");
    }
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        const char *opcode;
        size_t destination_offset;
        size_t record_size;

        if (!minic_riscv64_core_record_load_supported(program, function, instruction, &record_size) ||
            !minic_riscv64_core_frame_object_offset(program,
                                function,
                                frame,
                                instruction->value.record_load.destination_object,
                                &destination_offset) ||
            !minic_riscv64_frame_load_value(
                file, frame, instruction->value.record_load.source_address, "t0")) {
            return false;
        }
        if (record_size <= 8U) {
            opcode = record_size == 8U ? "ld" : record_size == 4U ? "lwu" :
                     record_size == 2U ? "lhu" : "lbu";
            if (fprintf(file, "  %s t1, 0(t0)\n", opcode) < 0 ||
                !minic_riscv64_frame_store_chunk(file, frame, "t1", destination_offset, record_size)) {
                return false;
            }
            return true;
        }

        /* M162_CORE_RV64_RECORD_LOAD: materialize arbitrary non-empty records
           into the destination CoreObject without assuming source alignment.
           This mirrors RECORD_COPY's byte-safe O0 fallback. */
        if (!minic_riscv64_frame_emit_address(file, frame, "t1", destination_offset)) {
            return false;
        }
        {
            if (!emit_record_copy_bytes(file, "t1", "t0", record_size))
                return false;
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        size_t alignment;
        size_t copy_size;

        if (!minic_riscv64_core_record_copy_supported(program, function, instruction) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    instruction->type,
                                    &copy_size,
                                    &alignment) ||
            !minic_riscv64_frame_load_value(
                file, frame, instruction->value.record_copy.destination_address, "t0") ||
            !minic_riscv64_frame_load_value(file, frame, instruction->value.record_copy.source_address, "t1")) {
            return false;
        }
        (void)alignment;
        if (!emit_record_copy_bytes(file, "t0", "t1", copy_size))
            return false;
        return true;
    }
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return emit_field_address(file, frame, instruction);
    default:
        return false;
    }
}
