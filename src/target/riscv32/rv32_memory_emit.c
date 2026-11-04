#include "target/riscv32/rv32_internal.h"
#include "target/common/elf_module_symbol_info.h"
#include "target/riscv/record_copy_bytes.h"
#include "target/riscv/typed_memory.h"
#include "target/riscv/instruction_select.h"
#include "target/data_layout.h"
#include <inttypes.h>
#include <stdint.h>

/* ILP32 wide scalar is two consecutive little-endian words.  A runtime
 * alignment check is needed for unknown typed pointers, not just explicit
 * FIELD_ADDRESS: a packed member can escape and later be loaded through a
 * regular long-long pointer.  The byte path uses only Base-I lbu/sb. */
static bool rv32_wide_memory(FILE *out, bool store, size_t alignment) {
    return minic_riscv_emit_typed_word_pair(out, store, 32U,
        "t1", "t2", "t0", "t3", alignment, "rv32_memory");
}

static bool rv32_allocation_size_type(const MinicCoreFunction *function,
                                      MinicCoreValueId count) {
    const MinicCoreType *type;
    if (function == NULL || function->type_context == NULL ||
        count >= function->value_count) return false;
    type = minic_core_type_get(function->type_context,
        minic_core_type_unqualified(function->type_context,
                                    function->values[count].core_type_id));
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
           type->has_layout && type->size == 4U &&
           type->value.integer.bit_width == 32U &&
           !type->value.integer.is_signed && !type->value.integer.is_bool;
}

static bool rv32_record_layout(const MinicCoreFunction *function,
                                MinicCoreTypeId type_id,
                                size_t *size, size_t *alignment) {
    const MinicCoreType *type;
    if (function == NULL || function->type_context == NULL ||
        size == NULL || alignment == NULL) return false;
    type = minic_core_type_get(function->type_context,
        minic_core_type_unqualified(function->type_context, type_id));
    if (type == NULL || !type->has_layout ||
        type->kind != MINIC_CORE_TYPE_RECORD ||
        type->alignment == 0U ||
        (type->alignment & (type->alignment - 1U)) != 0U) return false;
    *size = type->size;
    *alignment = type->alignment;
    return true;
}

/* Addressing, ILP32 memory and aggregate transport.  Frozen Core owns
 * pointer-alignment proof; sealed Module owns PIC symbol provenance. */
bool rv32_emit_memory_instruction(FILE *out,
                                  const MinicCoreModule *module,
                                  const MinicTargetInfo *target,
                                  const MinicCoreFunction *f,
                                  const Rv32Frame *fr,
                                  const MinicCoreInstruction *ins) {
    unsigned bits;
    size_t access_alignment;
    if (out == NULL || target == NULL || f == NULL ||
        fr == NULL || ins == NULL ||
        module == NULL || !module->finalized) return false;
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
        MinicRiscvPointerScalePlan scale;
        if (stride > UINT32_MAX ||
            !minic_riscv_plan_pointer_scale(stride, &scale)) return false;
        /* GNU permits pointer arithmetic over zero-sized record elements.
         * Core has already evaluated the index and its side effects; the
         * byte offset is identically zero for every index on ILP32. */
        if (scale.kind == MINIC_RISCV_POINTER_SCALE_ZERO)
            return rv32_slot(out, fr, ins->value.pointer_offset.base,
                             "t0", true) &&
                   rv32_slot(out, fr, ins->result, "t0", false);
        if (!rv32_slot(out, fr, ins->value.pointer_offset.index, "t1", true))
            return false;
        if (scale.kind == MINIC_RISCV_POINTER_SCALE_SHIFT) {
            /* Multiplication modulo 2^32 is identical to an XLEN-local
             * left shift for any signed or unsigned index bit pattern. */
            if (fprintf(out, "  slli t1, t1, %u\n", scale.shift) < 0)
                return false;
        } else if (scale.kind == MINIC_RISCV_POINTER_SCALE_MULTIPLY) {
            /* Odd/non-power-of-two strides still use the pinned GNU
             * libgcc multiplication implementation, not MiniC arithmetic. */
            if (fprintf(out,
                        "  mv a0, t1\n  li a1, %zu\n  call __mulsi3\n"
                        "  mv t1, a0\n", stride) < 0) return false;
        }
        /* Load base after scaling: the libcall may clobber caller-saved
         * registers. SHIFT and UNIT use the same safe frame-owned path. */
        return rv32_slot(out, fr, ins->value.pointer_offset.base, "t0", true) &&
               fprintf(out, "  %s t0, t0, t1\n",
                       ins->value.pointer_offset.subtract ? "sub" : "add") >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_LOAD:
        access_alignment = minic_riscv_address_alignment(f,
                                          ins->value.load.address);
        if (rv32_core_scalar_storage_words(f, ins->core_type_id,
                                           &bits) && bits == 2U)
            return rv32_slot(out, fr, ins->value.load.address, "t0", true) &&
                   rv32_wide_memory(out, false, access_alignment) &&
                   rv32_pair_slot(out, fr, ins->result,
                                  "t1", "t2", false);
        return rv32_slot(out, fr, ins->value.load.address, "t0", true) &&
               rv32_core_load_mem(out, f, ins->core_type_id,
                                  "t1", access_alignment) &&
               rv32_slot(out, fr, ins->result, "t1", false);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
        /* Fixed SSA and locals use s0. Each dynamic allocation leaves a
         * fresh moving-sp ABI outgoing argument region below its data. */
        const MinicCoreValueId count = ins->value.operand;
        if (!fr->has_dynamic_stack || count >= f->value_count ||
            ins->result >= f->value_count ||
            !rv32_core_pointer_type(f, ins->core_type_id) ||
            f->values[ins->result].core_type_id != ins->core_type_id ||
            !rv32_allocation_size_type(f, count) ||
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
        access_alignment = minic_riscv_address_alignment(f,
                                          ins->value.store.address);
        if (ins->value.store.stored_value < f->value_count &&
            rv32_core_scalar_storage_words(
                f, f->values[ins->value.store.stored_value].core_type_id,
                &bits) &&
            bits == 2U)
            return rv32_slot(out, fr, ins->value.store.address, "t0", true) &&
                   rv32_pair_slot(out, fr, ins->value.store.stored_value,
                                  "t1", "t2", true) &&
                   rv32_wide_memory(out, true, access_alignment);
        return ins->value.store.stored_value < f->value_count &&
               rv32_slot(out, fr, ins->value.store.address, "t0", true) &&
               rv32_slot(out, fr, ins->value.store.stored_value, "t1", true) &&
               rv32_core_store_mem(out, f,
                   f->values[ins->value.store.stored_value].core_type_id,
                   access_alignment);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        size_t copy_size, alignment;
        if (!rv32_record_layout(f, ins->core_type_id,
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
            !rv32_record_layout(f, f->objects[id].core_type_id,
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
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
        const char *symbol;
        bool preemptible;
        if (ins->value.global_id >= f->global_count) return false;
        symbol = f->globals[ins->value.global_id].name;
        preemptible = target->position_independent &&
            !minic_multi_elf_module_symbol_facts(
                module, symbol, MINIC_ELF_MODULE_SYMBOL_OBJECT).nonpreemptible;
        return fprintf(out, preemptible
                       ? "  .option push\n  .option pic\n  la t0, %s\n  .option pop\n"
                       : "  lla t0, %s\n", symbol) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
        const char *symbol;
        bool preemptible;
        if (ins->value.function_symbol_id >= f->function_symbol_count)
            return false;
        symbol = f->function_symbols[ins->value.function_symbol_id].name;
        preemptible = target->position_independent &&
            !minic_multi_elf_module_symbol_facts(
                module, symbol, MINIC_ELF_MODULE_SYMBOL_FUNCTION).nonpreemptible;
        return fprintf(out, preemptible
                       ? "  .option push\n  .option pic\n  la t0, %s\n  .option pop\n"
                       : "  lla t0, %s\n", symbol) >= 0 &&
               rv32_slot(out, fr, ins->result, "t0", false);
    }
    default:
        return false;
    }
}
