#include "target/riscv64/memory_emit.h"
#include "target/riscv/record_copy_bytes.h"
#include "target/riscv/typed_memory.h"
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


bool minic_riscv64_emit_memory_instruction(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    if (file == NULL || function == NULL || frame == NULL || instruction == NULL)
        return false;
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_LOAD: {
        const size_t guaranteed = minic_riscv_address_alignment(
            program, minic_default_data_layout(), function,
            instruction->value.load.address);
        if (minic_type_is_int128_integer(instruction->type) ||
            minic_type_is_long_double(instruction->type)) {
            if (!minic_riscv64_frame_load_value(file, frame, instruction->value.load.address, "t0") ||
                !minic_riscv_emit_typed_word_pair(
                    file, false, 64U, "t1", "t2", "t0", "t3",
                    guaranteed, "rv64_memory")) {
                return false;
            }
            return minic_riscv64_frame_store_int128(file, frame, instruction->result, "t1", "t2");
        }
        if (!minic_riscv64_frame_load_value(file, frame, instruction->value.load.address, "t0") ||
            !minic_riscv64_emit_safe_scalar_load(
                file, program, instruction->type, "t1", "t0", "t3",
                guaranteed)) {
            return false;
        }
        return minic_riscv64_frame_store_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreValueId stored_value;
        MinicType stored_type;
        size_t guaranteed;

        stored_value = instruction->value.store.stored_value;
        if (stored_value >= function->value_count) {
            return false;
        }
        stored_type = function->values[stored_value].type;
        guaranteed = minic_riscv_address_alignment(
            program, minic_default_data_layout(), function,
            instruction->value.store.address);
        if (minic_type_is_int128_integer(stored_type) ||
            minic_type_is_long_double(stored_type)) {
            return minic_riscv64_frame_load_value(file, frame, instruction->value.store.address, "t0") &&
                   minic_riscv64_frame_load_int128(file, frame, stored_value, "t1", "t2") &&
                   minic_riscv_emit_typed_word_pair(
                       file, true, 64U, "t1", "t2", "t0", "t3",
                       guaranteed, "rv64_memory");
        }
        return minic_riscv64_frame_load_value(file, frame, instruction->value.store.address, "t0") &&
               minic_riscv64_frame_load_value(file, frame, stored_value, "t1") &&
               minic_riscv64_emit_safe_scalar_store(
                   file, program, stored_type, "t1", "t0", "t3", guaranteed);
    }
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
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
        /* RECORD_LOAD's source pointer may name an unaligned member inside a
         * packed enclosing record even when its own record type has natural
         * 2/4/8-byte alignment. Core does not yet carry a proven pointer
         * alignment at this instruction. Copy every byte until such a proof
         * exists; direct lhu/lwu/ld plus sh/sw/sd can trap or corrupt records
         * of 2/4/8 bytes, and irregular 3/5/6/7-byte sizes need exact copies
         * regardless of alignment. This is the same byte-safe O0 primitive
         * used by RECORD_COPY, with no second target copy algorithm. */
        if (!minic_riscv64_frame_emit_address(file, frame, "t1", destination_offset)) {
            return false;
        }
        {
            if (!minic_riscv_emit_record_copy_bytes(file, "t1", "t0", record_size))
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
        if (!minic_riscv_emit_record_copy_bytes(file, "t0", "t1", copy_size))
            return false;
        return true;
    }
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return emit_field_address(file, frame, instruction);
    default:
        return false;
    }
}
