#include "target/riscv32/rv32_internal.h"
#include "target/riscv/record_copy_bytes.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <stdint.h>

/* Addressing, ILP32 memory and aggregate transport. Core's record layout
 * is obtained through target DataLayout, never host sizeof or LP64 slots. */
bool rv32_emit_memory_instruction(FILE *out, const MinicC0Program *program,
                                  const MinicTargetInfo *target,
                                  const MinicCoreFunction *f,
                                  const Rv32Frame *fr,
                                  const MinicCoreInstruction *ins) {
    unsigned bits;
    if (out == NULL || program == NULL || target == NULL ||
        f == NULL || fr == NULL || ins == NULL) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        return (size_t)ins->value.object_id < fr->object_count &&
               rv32_address(out, fr, "t0",
                            fr->object_offsets[ins->value.object_id]) &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return rv32_slot(out, fr, ins->value.field_address.base, "t0", true) &&
               fprintf(out, "  li t1, %zu\n  add t0, t0, t1\n",
                       ins->value.field_address.byte_offset) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
        size_t stride = ins->value.pointer_offset.element_size;
        if (stride > UINT32_MAX) return false;
        /* GNU permits pointer arithmetic over zero-sized record elements.
         * Core has already evaluated the index and its side effects; the
         * byte offset is identically zero for every index on ILP32. */
        if (stride == 0U)
            return rv32_slot(out, fr, ins->value.pointer_offset.base,
                             "t0", true) &&
                   rv32_slot(out, fr, ins->result, "t0", false);
        if (!rv32_slot(out, fr, ins->value.pointer_offset.index, "t1", true))
            return false;
        if (stride != 1U) {
            /* Keep negative indices and unsigned large indices correct
             * modulo 2^XLEN; libgcc multiplies raw 32-bit bit patterns. */
            if (fprintf(out,
                        "  mv a0, t1\n  li a1, %zu\n  call __mulsi3\n"
                        "  mv t1, a0\n", stride) < 0) return false;
        }
        /* The libcall is allowed to clobber all caller-saved registers,
         * so reload the base only after multiplying the index. */
        return rv32_slot(out, fr, ins->value.pointer_offset.base, "t0", true) &&
               fprintf(out, "  %s t0, t0, t1\n",
                       ins->value.pointer_offset.subtract ? "sub" : "add") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_LOAD:
        if (rv32_scalar_storage_words(f, ins->type, &bits) && bits == 2U)
            return rv32_slot(out, fr, ins->value.load.address, "t0", true) &&
                   fprintf(out, "  lw t1, 0(t0)\n  lw t2, 4(t0)\n") >= 0 &&
                   rv32_pair_slot(out, fr, ins->result,
                                  "t1", "t2", false);
        return rv32_slot(out, fr, ins->value.load.address, "t0", true) &&
               rv32_load_mem(out, f, ins->type, "t1") &&
               rv32_slot(out, fr, ins->result, "t1", false);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
        /* Fixed SSA and locals use s0. Each dynamic allocation leaves a
         * fresh moving-sp ABI outgoing argument region below its data. */
        const MinicCoreValueId count = ins->value.operand;
        if (!fr->has_dynamic_stack || count >= f->value_count ||
            ins->result >= f->value_count ||
            !minic_type_is_pointer(ins->type) ||
            !minic_type_equal(f->values[ins->result].type, ins->type) ||
            !minic_type_equal(f->values[count].type, minic_type_unsigned_long()) ||
            !rv32_slot(out, fr, count, "t0", true))
            return false;
        /* Both 16-byte rounding and outgoing-area addition check XLEN overflow. */
        return fprintf(out,
            "  mv t1, t0\n"
            "  addi t0, t0, 15\n"
            "  bltu t0, t1, 1f\n"
            "  li t1, -16\n"
            "  and t0, t0, t1\n"
            "  li t1, %zu\n"
            "  add t2, t0, t1\n"
            "  bltu t2, t0, 1f\n"
            "  sub sp, sp, t2\n"
            "  li t0, %zu\n"
            "  add t0, sp, t0\n"
            "  j 2f\n"
            "1:\n"
            "  ebreak\n"
            "2:\n",
            fr->outgoing_size, fr->outgoing_size) >= 0 &&
            rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_STORE:
        if (ins->value.store.stored_value < f->value_count &&
            rv32_scalar_storage_words(
                f, f->values[ins->value.store.stored_value].type, &bits) &&
            bits == 2U)
            return rv32_slot(out, fr, ins->value.store.address, "t0", true) &&
                   rv32_pair_slot(out, fr, ins->value.store.stored_value,
                                  "t1", "t2", true) &&
                   fprintf(out, "  sw t1, 0(t0)\n  sw t2, 4(t0)\n") >= 0;
        return ins->value.store.stored_value < f->value_count &&
               rv32_slot(out, fr, ins->value.store.address, "t0", true) &&
               rv32_slot(out, fr, ins->value.store.stored_value, "t1", true) &&
               rv32_store_mem(out, f, f->values[ins->value.store.stored_value].type);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        size_t copy_size, alignment;
        if (!minic_type_is_record(ins->type) ||
            !minic_data_layout_type(target->data_layout, program, ins->type,
                                    &copy_size, &alignment) ||
            copy_size > UINT32_MAX)
            return false;
        /* GNU C permits empty lockdep records. A zero-byte record copy is
         * a genuine no-op, as in the shared RV64 record-copy helper. */
        if (copy_size == 0U) return true;
        if (
            !rv32_slot(out, fr, ins->value.record_copy.destination_address,
                       "t0", true) ||
            !rv32_slot(out, fr, ins->value.record_copy.source_address,
                       "t1", true))
            return false;
        (void)alignment;
        return minic_riscv_emit_record_copy_bytes(out, "t0", "t1", copy_size);
    }
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        const MinicCoreObjectId id =
            ins->value.record_load.destination_object;
        size_t copy_size, alignment;
        if (id >= f->object_count ||
            !minic_type_is_record(f->objects[id].type) ||
            !minic_data_layout_type(target->data_layout, program,
                                    f->objects[id].type,
                                    &copy_size, &alignment) ||
            copy_size > UINT32_MAX)
            return false;
        if (copy_size == 0U) return true;
        if (
            !rv32_slot(out, fr, ins->value.record_load.source_address,
                       "t0", true) ||
            !rv32_address(out, fr, "t1", fr->object_offsets[id]))
            return false;
        (void)alignment;
        return minic_riscv_emit_record_copy_bytes(out, "t1", "t0", copy_size);
    }
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        return ins->value.global_id < f->global_count &&
               fprintf(out, "  lla t0, %s\n",
                       f->globals[ins->value.global_id].name) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return ins->value.function_symbol_id < f->function_symbol_count &&
               fprintf(out, "  lla t0, %s\n",
                       f->function_symbols[ins->value.function_symbol_id].name) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    default:
        return false;
    }
}
