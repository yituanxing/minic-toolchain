#include "target/riscv/machine_core_select.h"
#include "target/riscv/machine_gnu_asm.h"
#include "target/riscv/machine_opcode.h"
#include "target/riscv/machine_target_state.h"
#include "target/riscv/instruction_select.h"
#include "target/common/machine_core_coverage.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { RV_ZERO = 0U, RV_SP = 2U, RV_T0 = 5U, RV_T1 = 6U,
       RV_T2 = 7U, RV_S0 = 8U, RV_A0 = 10U, RV_A7 = 17U,
       RV_T3 = 28U, RV_T6 = 31U };
typedef struct RvCoreSelection {
    MinicMachineBuilder builder;
    const MinicRiscvCoreMachinePlan *plan;
    unsigned xlen;
    bool *reachable;
    size_t *core_blocks;
    size_t *far_entries;
    size_t *conditional_fallthroughs;
    size_t *conditional_others;
    size_t epilogue_block;
} RvCoreSelection;

static MinicMachineOperand rv_reg(unsigned xlen, unsigned id,
                                  unsigned access) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){id,
        (uint16_t)xlen, MINIC_MACHINE_REGISTER_GENERAL};
    op.value.physical.access = access;
    return op;
}
static MinicMachineOperand rv_imm(int64_t value) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_INTEGER;
    op.value.integer = value;
    return op;
}
static MinicMachineOperand rv_target(size_t block, bool address) {
    MinicMachineOperand op = {0};
    op.kind = address ? MINIC_MACHINE_OPERAND_BLOCK_ADDRESS :
        MINIC_MACHINE_OPERAND_BLOCK;
    op.value.block_id = block;
    return op;
}
static MinicMachineOperand rv_addr(unsigned xlen, unsigned base) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    op.value.address.target_mode =
        MINIC_RISCV_MACHINE_ADDRESS_BASE_DISPLACEMENT;
    op.value.address.has_base = true;
    op.value.address.base = (MinicMachineRegister){base,
        (uint16_t)xlen, MINIC_MACHINE_REGISTER_GENERAL};
    op.value.address.scale = 1U;
    return op;
}
static bool rv_emit(RvCoreSelection *sel, uint32_t opcode,
                    const MinicMachineOperand *ops, size_t count) {
    return minic_riscv_machine_emit_selected(&sel->builder, opcode,
                                              ops, count);
}
static bool rv_emit_volatile(RvCoreSelection *sel, uint32_t opcode,
    bool is_volatile, const MinicMachineOperand *ops, size_t count) {
    return minic_riscv_machine_emit_selected_effects(&sel->builder, opcode,
        is_volatile ? MINIC_MACHINE_EFFECT_VOLATILE : 0U, ops, count);
}
static bool rv_li(RvCoreSelection *sel, unsigned dst, int64_t value) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE), rv_imm(value)
    };
    return rv_emit(sel, MINIC_RISCV_MACHINE_LI, ops, 2U);
}
static bool rv_rr(RvCoreSelection *sel, uint32_t opcode,
                  unsigned dst, unsigned src) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE),
        rv_reg(sel->xlen, src, MINIC_MACHINE_REGISTER_READ)
    };
    return rv_emit(sel, opcode, ops, 2U);
}
static bool rv_rrr(RvCoreSelection *sel, uint32_t opcode,
    unsigned dst, unsigned left, unsigned right) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE),
        rv_reg(sel->xlen, left, MINIC_MACHINE_REGISTER_READ),
        rv_reg(sel->xlen, right, MINIC_MACHINE_REGISTER_READ)
    };
    return rv_emit(sel, opcode, ops, 3U);
}
static bool rv_rri(RvCoreSelection *sel, uint32_t opcode,
    unsigned dst, unsigned src, int64_t imm) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, dst, MINIC_MACHINE_REGISTER_WRITE),
        rv_reg(sel->xlen, src, MINIC_MACHINE_REGISTER_READ), rv_imm(imm)
    };
    return rv_emit(sel, opcode, ops, 3U);
}
static bool rv_j(RvCoreSelection *sel, size_t to) {
    const size_t current = sel->builder.current_block;
    if (to == current + 1U) {
        const MinicMachineOperand ops[] = {rv_target(to, false)};
        return rv_emit(sel, MINIC_RISCV_MACHINE_J_BLOCK, ops, 1U) &&
            minic_machine_builder_add_successor(&sel->builder, to);
    }
    /* A far Core edge first lands in an auxiliary addressable dispatch block,
     * adjacent to the primary Core block. Thus Core indirect-entry metadata
     * remains truthful even when a direct edge needs a far jump. */
    {
        size_t far = to;
        size_t i;
        for (i = 0U; i < sel->plan->core->block_count; ++i)
            if (sel->core_blocks[i] == to) {
                far = sel->far_entries[i];
                break;
            }
        const MinicMachineOperand la[] = {
            rv_reg(sel->xlen, RV_T6, MINIC_MACHINE_REGISTER_WRITE),
            rv_target(far, true)
        };
        const MinicMachineOperand jump[] = {
            rv_reg(sel->xlen, RV_T6, MINIC_MACHINE_REGISTER_READ),
            rv_target(far, false)
        };
        return rv_emit(sel, MINIC_RISCV_MACHINE_LA_BLOCK, la, 2U) &&
            rv_emit(sel, MINIC_RISCV_MACHINE_JALR_BLOCK, jump, 2U) &&
            minic_machine_builder_add_successor(&sel->builder, far);
    }
}
static bool rv_branch_zero(RvCoreSelection *sel, bool nonzero,
    unsigned reg, size_t to) {
    const MinicMachineOperand ops[] = {
        rv_reg(sel->xlen, reg, MINIC_MACHINE_REGISTER_READ),
        rv_target(to, false)
    };
    return rv_emit(sel, nonzero ? MINIC_RISCV_MACHINE_BNEZ :
        MINIC_RISCV_MACHINE_BEQZ, ops, 2U) &&
        minic_machine_builder_add_successor(&sel->builder, to);
}
static bool rv_type(const RvCoreSelection *sel, MinicCoreTypeId id,
    unsigned *bytes, bool *sign, bool *pointer) {
    const MinicCoreTypeContext *types = sel->plan->core->type_context;
    const MinicCoreType *type;
    if (types == NULL || bytes == NULL || sign == NULL || pointer == NULL)
        return false;
    type = minic_core_type_get(types, minic_core_type_unqualified(types, id));
    if (type == NULL || !type->has_layout || type->size == 0U ||
        type->size > sel->xlen / 8U) return false;
    *pointer = type->kind == MINIC_CORE_TYPE_POINTER;
    if (*pointer) {
        if (type->size != sel->xlen / 8U) return false;
        *bytes = sel->xlen / 8U;
        *sign = false;
        return true;
    }
    if (type->kind != MINIC_CORE_TYPE_INTEGER ||
        (type->size != 1U && type->size != 2U &&
         type->size != 4U && type->size != 8U) ||
        type->value.integer.bit_width == 0U ||
        type->value.integer.bit_width > type->size * 8U) return false;
    *bytes = (unsigned)type->size;
    *sign = type->value.integer.is_signed;
    return true;
}
static bool rv_value_slot(const RvCoreSelection *sel, MinicCoreValueId value,
    size_t *offset) {
    const MinicRiscvCoreMachinePlan *plan = sel->plan;
    const size_t word = sel->xlen / 8U;
    if (value == MINIC_CORE_VALUE_INVALID || value >= plan->value_offset_count ||
        value >= plan->core->value_count || plan->value_offsets == NULL ||
        plan->value_offsets[value] == SIZE_MAX ||
        plan->value_offsets[value] % word != 0U ||
        plan->value_offsets[value] > plan->frame->frame_size ||
        word > plan->frame->frame_size - plan->value_offsets[value])
        return false;
    *offset = plan->value_offsets[value];
    return true;
}
static bool rv_load(RvCoreSelection *sel, MinicCoreValueId value,
                    unsigned reg) {
    size_t offset;
    return rv_value_slot(sel, value, &offset) &&
        minic_riscv_machine_select_frame_transfer(&sel->builder, reg,
            sel->plan->frame->has_dynamic_stack ? RV_S0 : RV_SP,
            offset, true, sel->xlen / 8U);
}
static bool rv_store(RvCoreSelection *sel, MinicCoreValueId value,
                     unsigned reg) {
    size_t offset;
    return rv_value_slot(sel, value, &offset) &&
        minic_riscv_machine_select_frame_transfer(&sel->builder, reg,
            sel->plan->frame->has_dynamic_stack ? RV_S0 : RV_SP,
            offset, false, sel->xlen / 8U);
}
static bool rv_normalize(RvCoreSelection *sel, unsigned reg,
                         unsigned bytes, bool sign) {
    const unsigned shift = sel->xlen - bytes * 8U;
    if (bytes == 0U || bytes > sel->xlen / 8U) return false;
    if (shift == 0U) return true;
    return rv_rri(sel, MINIC_RISCV_MACHINE_SLLI, reg, reg, (int64_t)shift) &&
        rv_rri(sel, sign ? MINIC_RISCV_MACHINE_SRAI :
            MINIC_RISCV_MACHINE_SRLI, reg, reg, (int64_t)shift);
}
static bool rv_core_constant(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    unsigned bytes;
    bool sign, pointer;
    return rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) &&
        rv_li(sel, RV_T0, ins->value.integer_value) &&
        rv_normalize(sel, RV_T0, bytes, sign) &&
        rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_unary(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    unsigned bytes;
    bool sign, pointer;
    uint32_t opcode;
    if (!rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) ||
        !rv_load(sel, ins->value.operand, RV_T0)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        opcode = MINIC_RISCV_MACHINE_NEG; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        opcode = MINIC_RISCV_MACHINE_NOT; break;
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        opcode = MINIC_RISCV_MACHINE_SEQZ; break;
    default: return false;
    }
    return rv_rr(sel, opcode, RV_T0, RV_T0) &&
        rv_normalize(sel, RV_T0, bytes, sign) &&
        rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_binary(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    unsigned bytes, left_bytes, right_bytes;
    bool sign, pointer, left_sign, right_sign, left_ptr, right_ptr;
    uint32_t op;
    if (ins->value.binary.left >= fn->value_count ||
        ins->value.binary.right >= fn->value_count ||
        !rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) ||
        !rv_type(sel, fn->values[ins->value.binary.left].core_type_id,
            &left_bytes, &left_sign, &left_ptr) ||
        !rv_type(sel, fn->values[ins->value.binary.right].core_type_id,
            &right_bytes, &right_sign, &right_ptr) ||
        !rv_load(sel, ins->value.binary.left, RV_T0) ||
        !rv_load(sel, ins->value.binary.right, RV_T1)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        op = MINIC_RISCV_MACHINE_ADD; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        op = MINIC_RISCV_MACHINE_SUB; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        op = MINIC_RISCV_MACHINE_AND; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        op = MINIC_RISCV_MACHINE_XOR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        op = MINIC_RISCV_MACHINE_OR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        op = MINIC_RISCV_MACHINE_SLL; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        op = left_sign ? MINIC_RISCV_MACHINE_SRA :
            MINIC_RISCV_MACHINE_SRL; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        op = left_sign ? MINIC_RISCV_MACHINE_SLT :
            MINIC_RISCV_MACHINE_SLTU; break;
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (!left_ptr || !right_ptr) return false;
        op = MINIC_RISCV_MACHINE_SLTU; break;
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!rv_rrr(sel, MINIC_RISCV_MACHINE_XOR,
                RV_T0, RV_T0, RV_T1)) return false;
        return rv_rr(sel, MINIC_RISCV_MACHINE_SEQZ, RV_T0, RV_T0) &&
            rv_store(sel, ins->result, RV_T0);
    default: return false;
    }
    if (!rv_rrr(sel, op, RV_T0, RV_T0, RV_T1) ||
        !rv_normalize(sel, RV_T0, bytes, sign)) return false;
    return rv_store(sel, ins->result, RV_T0);
}

static bool rv_core_parameter(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    unsigned bytes;
    bool sign, pointer;
    const size_t index = ins->value.parameter_index;
    size_t reg;
    if (sel->xlen == 32U) {
        const Rv32FunctionAbiPlan *plan = sel->plan->abi32;
        const Rv32AbiWordLocation *loc;
        if (plan == NULL || index >= plan->parameter_count ||
            plan->parameters == NULL) return false;
        loc = &plan->parameters[index];
        if (loc->words != 1U || loc->indirect_record ||
            loc->location.register_count != 1U ||
            loc->location.stack_count != 0U ||
            loc->location.register_begin >= 8U) return false;
        reg = loc->location.register_begin;
    } else {
        const MinicRiscv64FunctionAbiPlan *plan = sel->plan->abi64;
        const MinicRiscv64AbiArgumentLocation *loc;
        if (plan == NULL || index >= plan->parameter_location_count ||
            plan->parameter_locations == NULL) return false;
        loc = &plan->parameter_locations[index];
        if (loc->value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
            loc->value.slot_count != 1U ||
            loc->integer_register_count != 1U ||
            loc->floating_register_count != 0U ||
            loc->stack_slot_count != 0U ||
            loc->integer_register_begin >= 8U) return false;
        reg = loc->integer_register_begin;
    }
    return rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) &&
        rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T0, RV_A0 + (unsigned)reg) &&
        rv_normalize(sel, RV_T0, bytes, sign) &&
        rv_store(sel, ins->result, RV_T0);
}
static uint32_t rv_memory_opcode(const RvCoreSelection *sel,
    bool load, unsigned bytes, bool sign) {
    if (load) {
        if (bytes == 1U) return sign ? MINIC_RISCV_MACHINE_LB :
            MINIC_RISCV_MACHINE_LBU;
        if (bytes == 2U) return sign ? MINIC_RISCV_MACHINE_LH :
            MINIC_RISCV_MACHINE_LHU;
        if (bytes == 4U) return sel->xlen == 64U && !sign ?
            MINIC_RISCV_MACHINE_LWU : MINIC_RISCV_MACHINE_LW;
        if (bytes == 8U && sel->xlen == 64U)
            return MINIC_RISCV_MACHINE_LD;
    } else {
        if (bytes == 1U) return MINIC_RISCV_MACHINE_SB;
        if (bytes == 2U) return MINIC_RISCV_MACHINE_SH;
        if (bytes == 4U) return MINIC_RISCV_MACHINE_SW;
        if (bytes == 8U && sel->xlen == 64U)
            return MINIC_RISCV_MACHINE_SD;
    }
    return MINIC_RISCV_MACHINE_INVALID;
}
static bool rv_core_memory(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const bool load = ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
        ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
    const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD ||
        ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
    const MinicCoreValueId address = atomic ?
        (load ? ins->value.atomic_load.address : ins->value.atomic_store.address) :
        (load ? ins->value.load.address : ins->value.store.address);
    const MinicCoreValueId source = load ? MINIC_CORE_VALUE_INVALID :
        (atomic ? ins->value.atomic_store.stored_value :
            ins->value.store.stored_value);
    const bool is_volatile = !atomic && (load ?
        ins->value.load.is_volatile : ins->value.store.is_volatile);
    const MinicCoreTypeId type_id = load ? ins->core_type_id :
        (source < fn->value_count ? fn->values[source].core_type_id :
         MINIC_CORE_TYPE_INVALID);
    unsigned bytes;
    bool sign, pointer;
    uint32_t opcode;
    MinicMachineOperand ops[2];
    const MinicCoreType *memory_type;
    /* Core admits memory_order=0 (relaxed) only. Naturally aligned RV32I /
     * RV64I scalar load/store are atomic at those widths; stronger orders
     * would require a separate typed FENCE policy, never silent downgrading. */
    if ((atomic && (load ? ins->value.atomic_load.memory_order :
            ins->value.atomic_store.memory_order) != 0U) ||
        !rv_type(sel, type_id, &bytes, &sign, &pointer) ||
        (atomic && (fn->type_context == NULL ||
            (memory_type = minic_core_type_get(fn->type_context,
                minic_core_type_unqualified(fn->type_context, type_id))) == NULL ||
            !memory_type->has_layout || memory_type->alignment < bytes)) ||
        (opcode = rv_memory_opcode(sel, load, bytes, sign)) == 0U ||
        !rv_load(sel, address, RV_T0) ||
        (!load && !rv_load(sel, source, RV_T1))) return false;
    ops[0] = rv_reg(sel->xlen, RV_T1, load ?
        MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
    ops[1] = rv_addr(sel->xlen, RV_T0);
    if (!rv_emit_volatile(sel, opcode, is_volatile, ops, 2U)) return false;
    return !load || rv_store(sel, ins->result, RV_T1);
}
static bool rv_core_field_address(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const size_t offset = ins->value.field_address.byte_offset;
    if (offset > (size_t)INT64_MAX ||
        (sel->xlen == 32U && offset > (size_t)UINT32_MAX) ||
        !rv_load(sel, ins->value.field_address.base, RV_T0)) return false;
    if (offset <= 2047U) {
        if (!rv_rri(sel, MINIC_RISCV_MACHINE_ADDI,
            RV_T0, RV_T0, (int64_t)offset)) return false;
    } else if (!rv_li(sel, RV_T1, (int64_t)offset) ||
        !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
            RV_T0, RV_T0, RV_T1)) return false;
    return rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_pointer_offset(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    MinicRiscvPointerScalePlan scale;
    const MinicCoreFunction *fn = sel->plan->core;
    const size_t stride = ins->value.pointer_offset.element_size;
    size_t bits;
    unsigned bit = 0U;
    unsigned index_bytes;
    bool index_signed, index_pointer;
    if (!minic_riscv_plan_pointer_scale(stride, &scale) ||
        (sel->xlen == 32U && stride > (size_t)UINT32_MAX) ||
        ins->value.pointer_offset.index >= fn->value_count ||
        !rv_type(sel,
            fn->values[ins->value.pointer_offset.index].core_type_id,
            &index_bytes, &index_signed, &index_pointer) || index_pointer ||
        !rv_load(sel, ins->value.pointer_offset.base, RV_T0)) return false;
    if (scale.kind == MINIC_RISCV_POINTER_SCALE_ZERO)
        return rv_store(sel, ins->result, RV_T0);
    if (!rv_load(sel, ins->value.pointer_offset.index, RV_T1)) return false;
    switch (scale.kind) {
    case MINIC_RISCV_POINTER_SCALE_UNIT:
        break;
    case MINIC_RISCV_POINTER_SCALE_SHIFT:
        if (scale.shift >= sel->xlen ||
            !rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                RV_T1, RV_T1, (int64_t)scale.shift)) return false;
        break;
    case MINIC_RISCV_POINTER_SCALE_MULTIPLY:
        /* Literal stride is legalized to I-only shifts and adds. This is a
         * target-owned machine expansion; no runtime __muldi3/M extension. */
        if (!rv_li(sel, RV_T2, 0)) return false;
        for (bits = stride; bits != 0U; bits >>= 1U, ++bit) {
            if ((bits & 1U) == 0U) continue;
            if (bit == 0U) {
                if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV,
                    RV_T3, RV_T1)) return false;
            } else if (!rv_rri(sel, MINIC_RISCV_MACHINE_SLLI,
                           RV_T3, RV_T1, (int64_t)bit)) return false;
            if (!rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                RV_T2, RV_T2, RV_T3)) return false;
        }
        if (!rv_rr(sel, MINIC_RISCV_MACHINE_MV, RV_T1, RV_T2)) return false;
        break;
    default:
        return false;
    }
    return rv_rrr(sel, ins->value.pointer_offset.subtract ?
        MINIC_RISCV_MACHINE_SUB : MINIC_RISCV_MACHINE_ADD,
        RV_T0, RV_T0, RV_T1) && rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_conversion(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    unsigned bytes;
    bool sign, pointer;
    return rv_type(sel, ins->core_type_id, &bytes, &sign, &pointer) &&
        rv_load(sel, ins->value.operand, RV_T0) &&
        rv_normalize(sel, RV_T0, bytes, sign) &&
        rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_object_address(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicRiscvCoreMachinePlan *plan = sel->plan;
    const size_t object = ins->value.object_id;
    const unsigned base = plan->frame->has_dynamic_stack ? RV_S0 : RV_SP;
    size_t offset;
    if (object >= plan->object_offset_count ||
        plan->object_offsets == NULL ||
        (offset = plan->object_offsets[object]) == SIZE_MAX ||
        offset > plan->frame->frame_size ||
        offset > INT32_MAX) return false;
    if (offset <= 2047U) {
        if (!rv_rri(sel, MINIC_RISCV_MACHINE_ADDI,
            RV_T0, base, (int64_t)offset)) return false;
    } else if (!rv_li(sel, RV_T0, (int64_t)offset) ||
               !rv_rrr(sel, MINIC_RISCV_MACHINE_ADD,
                        RV_T0, base, RV_T0)) return false;
    return rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_symbol(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const char *symbol = NULL;
    uint32_t reloc = sel->plan->target->position_independent ?
        MINIC_RISCV_MACHINE_RELOC_GOT :
        MINIC_RISCV_MACHINE_RELOC_PCREL_LOCAL;
    MinicMachineOperand ops[2] = {{0}};
    if (ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS) {
        if (ins->value.global_id >= fn->global_count) return false;
        symbol = fn->globals[ins->value.global_id].name;
    } else if (ins->kind == MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS) {
        if (ins->value.function_symbol_id >=
            fn->function_symbol_count) return false;
        symbol = fn->function_symbols[ins->value.function_symbol_id].name;
    } else return false;
    if (symbol == NULL) return false;
    ops[0] = rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_WRITE);
    ops[1].kind = MINIC_MACHINE_OPERAND_SYMBOL;
    ops[1].value.symbol.name = (char *)symbol;
    ops[1].value.symbol.relocation = reloc;
    return rv_emit(sel, reloc == MINIC_RISCV_MACHINE_RELOC_GOT ?
        MINIC_RISCV_MACHINE_LA_GOT : MINIC_RISCV_MACHINE_LA_PCREL,
        ops, 2U) && rv_store(sel, ins->result, RV_T0);
}
static bool rv_core_block_address(RvCoreSelection *sel,
    const MinicCoreInstruction *ins) {
    MinicMachineOperand ops[2];
    if (ins->value.block_id >= sel->plan->core->block_count) return false;
    ops[0] = rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_WRITE);
    ops[1] = rv_target(sel->far_entries[ins->value.block_id], true);
    return rv_emit(sel, MINIC_RISCV_MACHINE_LA_BLOCK, ops, 2U) &&
        rv_store(sel, ins->result, RV_T0);
}
/* This is a consumer of the EXACT final psABI placements built for the old
 * RV32/RV64 emitter. It deliberately does not count, align, or assign a0-a7.
 * A not-yet-migrated placement rejects the entire MachineIR function. */
static bool rv_core_call_arguments(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id, size_t start, size_t count,
    bool has_scalar_result) {
    const MinicCoreFunction *fn = sel->plan->core;
    const Rv32CallAbiPlan *call32 = NULL;
    const MinicRiscv64PreparedCall *call64 = NULL;
    const MinicCoreCallView *view;
    size_t ordinal;
    size_t i;
    if (start > fn->call_argument_count ||
        count > fn->call_argument_count - start ||
        !sel->plan->frame->saves_return_address) return false;
    if (sel->xlen == 32U) {
        call32 = rv32_function_abi_call(sel->plan->abi32, instruction_id);
        if (call32 == NULL || call32->view.argument_begin != start ||
            call32->view.argument_count != count ||
            call32->indirect_return ||
            call32->view.return_form == MINIC_CORE_CALL_RETURN_OBJECT ||
            (has_scalar_result &&
             call32->view.return_form != MINIC_CORE_CALL_RETURN_VALUE) ||
            (count != 0U && call32->arguments == NULL)) return false;
    } else {
        if (sel->plan->semantic_calls64 == NULL ||
            sel->plan->abi64 == NULL ||
            (view = minic_core_call_index_find(sel->plan->semantic_calls64,
                instruction_id, &ordinal)) == NULL ||
            view->argument_begin != start ||
            view->argument_count != count ||
            view->return_form == MINIC_CORE_CALL_RETURN_OBJECT ||
            (has_scalar_result &&
             view->return_form != MINIC_CORE_CALL_RETURN_VALUE) ||
            ordinal >= sel->plan->abi64->outgoing_calls.count ||
            sel->plan->abi64->outgoing_calls.sites == NULL ||
            (call64 = sel->plan->abi64->outgoing_calls.sites[ordinal]) == NULL ||
            call64->instruction_id != instruction_id ||
            call64->argument_count != count ||
            (call64->return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
             call64->return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER) ||
            (has_scalar_result &&
             (call64->return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
              call64->return_value.slot_count != 1U))) return false;
    }
    for (i = 0U; i < count; ++i) {
        const MinicCoreCallArgument *arg = &fn->call_arguments[start + i];
        unsigned bytes;
        bool sign, pointer;
        size_t reg;
        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            arg->value.value_id >= fn->value_count ||
            !rv_type(sel, fn->values[arg->value.value_id].core_type_id,
                &bytes, &sign, &pointer)) return false;
        if (call32 != NULL) {
            const Rv32AbiWordLocation *loc = &call32->arguments[i];
            if (loc->words != 1U || loc->indirect_record ||
                loc->location.register_count != 1U ||
                loc->location.stack_count != 0U ||
                loc->location.register_begin >= 8U) return false;
            reg = loc->location.register_begin;
        } else {
            const MinicRiscv64AbiArgumentLocation *loc = &call64->arguments[i];
            if (loc->value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                loc->value.slot_count != 1U ||
                loc->integer_register_count != 1U ||
                loc->floating_register_count != 0U ||
                loc->stack_slot_count != 0U ||
                loc->integer_register_begin >= 8U) return false;
            reg = loc->integer_register_begin;
        }
        if (!rv_load(sel, arg->value.value_id, RV_A0 + (unsigned)reg))
            return false;
    }
    return true;
}

static bool rv_core_call(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreCallee *callee;
    MinicMachineOperand op = {0};
    unsigned result_bytes = 0U;
    bool result_sign = false, result_pointer = false;
    if (ins->value.call.extended_float_operation !=
            MINIC_CORE_EXT_FLOAT_NONE ||
        ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID ||
        ins->value.call.callee_id >= fn->callee_count) return false;
    callee = &fn->callees[ins->value.call.callee_id];
    if (callee->name == NULL ||
        (ins->result != MINIC_CORE_VALUE_INVALID &&
         !rv_type(sel, ins->core_type_id, &result_bytes,
             &result_sign, &result_pointer)) ||
        !rv_core_call_arguments(sel, instruction_id,
            ins->value.call.argument_begin, ins->value.call.argument_count,
            ins->result != MINIC_CORE_VALUE_INVALID)) return false;
    op.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    op.value.symbol.name = callee->name;
    op.value.symbol.relocation = MINIC_RISCV_MACHINE_RELOC_CALL;
    op.value.symbol.addend = 0;
    if (!rv_emit(sel, MINIC_RISCV_MACHINE_CALL_DIRECT, &op, 1U)) return false;
    return ins->result == MINIC_CORE_VALUE_INVALID ||
        (rv_normalize(sel, RV_A0, result_bytes, result_sign) &&
         rv_store(sel, ins->result, RV_A0));
}
static bool rv_core_indirect_call(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->plan->core;
    MinicMachineOperand callee;
    unsigned result_bytes = 0U;
    bool result_sign = false, result_pointer = false;
    if (ins->value.indirect_call.result_object != MINIC_CORE_OBJECT_INVALID ||
        ins->value.indirect_call.signature_id >= fn->call_signature_count ||
        (ins->result != MINIC_CORE_VALUE_INVALID &&
         !rv_type(sel, ins->core_type_id, &result_bytes,
             &result_sign, &result_pointer)) ||
        !rv_core_call_arguments(sel, instruction_id,
            ins->value.indirect_call.argument_begin,
            ins->value.indirect_call.argument_count,
            ins->result != MINIC_CORE_VALUE_INVALID) ||
        !rv_load(sel, ins->value.indirect_call.callee, RV_T0)) return false;
    callee = rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_READ);
    if (!rv_emit(sel, MINIC_RISCV_MACHINE_CALL_INDIRECT, &callee, 1U))
        return false;
    return ins->result == MINIC_CORE_VALUE_INVALID ||
        (rv_normalize(sel, RV_A0, result_bytes, result_sign) &&
         rv_store(sel, ins->result, RV_A0));
}
static bool rv_core_instruction(RvCoreSelection *sel,
    MinicCoreInstructionId instruction_id,
    const MinicCoreInstruction *ins) {
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return rv_core_constant(sel, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return rv_core_parameter(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return rv_core_binary(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return rv_core_unary(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return rv_core_conversion(sel, ins);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        return rv_core_object_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return rv_core_field_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        return rv_core_pointer_offset(sel, ins);
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return rv_core_symbol(sel, ins);
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return rv_core_block_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        return rv_core_memory(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL:
        return rv_core_call(sel, instruction_id, ins);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return rv_core_indirect_call(sel, instruction_id, ins);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return rv_emit(sel, MINIC_RISCV_MACHINE_BARRIER, NULL, 0U);
    default:
        return false;
    }
}

static bool rv_core_terminator(RvCoreSelection *sel, size_t core_id) {
    const MinicCoreFunction *fn = sel->plan->core;
    const MinicCoreTerminator *term = &fn->blocks[core_id].terminator;
    const size_t aux = sel->conditional_fallthroughs[core_id];
    const size_t other = sel->conditional_others[core_id];
    size_t i;
    if (!fn->blocks[core_id].has_terminator) return false;
    minic_machine_builder_clear_origin(&sel->builder);
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (term->return_object != MINIC_CORE_OBJECT_INVALID ||
            (term->return_value != MINIC_CORE_VALUE_INVALID &&
             !rv_load(sel, term->return_value, RV_A0))) return false;
        return rv_j(sel, sel->epilogue_block);
    case MINIC_CORE_TERMINATOR_BRANCH:
        return term->branch_target < fn->block_count &&
            rv_j(sel, sel->core_blocks[term->branch_target]);
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
        const size_t yes = term->conditional.when_true;
        const size_t no = term->conditional.when_false;
        if (aux == SIZE_MAX || other == SIZE_MAX ||
            yes >= fn->block_count ||
            no >= fn->block_count ||
            !rv_load(sel, term->conditional.condition, RV_T0) ||
            !rv_branch_zero(sel, false, RV_T0, other) ||
            !minic_machine_builder_add_successor(&sel->builder, aux) ||
            !minic_machine_builder_set_fallthrough(&sel->builder, aux) ||
            !minic_machine_builder_position_at(&sel->builder, aux))
            return false;
        return rv_j(sel, sel->core_blocks[yes]) &&
            minic_machine_builder_position_at(&sel->builder, other) &&
            rv_j(sel, sel->core_blocks[no]);
    }
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH: {
        const MinicMachineOperand ops[] = {
            rv_reg(sel->xlen, RV_T0, MINIC_MACHINE_REGISTER_READ),
            rv_imm(0)
        };
        if (!rv_load(sel, term->indirect_target, RV_T0) ||
            !rv_emit(sel, MINIC_RISCV_MACHINE_JALR_BRANCH, ops, 2U))
            return false;
        for (i = 0U; i < fn->block_count; ++i)
            if (fn->blocks[i].indirect_entry &&
                !minic_machine_builder_add_successor(&sel->builder,
                    sel->core_blocks[i])) return false;
        return true;
    }
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return rv_emit(sel, MINIC_RISCV_MACHINE_UNREACHABLE, NULL, 0U);
    default:
        return false;
    }
}

bool minic_riscv_machine_select_core_subset(MinicMachineFunction *machine,
    const MinicRiscvCoreMachinePlan *plan) {
    RvCoreSelection sel = {0};
    MinicRiscvIsa isa = {0};
    MinicMachineGNUAsmPolicy policy = {0};
    const MinicCoreFunction *core;
    const MinicTargetInfo *target;
    const MinicRiscvPhysicalFrame *frame;
    size_t i, id;
    bool success = false;
    if (machine == NULL || plan == NULL || plan->core == NULL ||
        plan->target == NULL || plan->frame == NULL ||
        (target = plan->target)->data_layout == NULL ||
        (core = plan->core)->name == NULL || core->block_count == 0U ||
        core->entry_block >= core->block_count || core->blocks == NULL ||
        !minic_core_function_verify(core) ||
        !minic_riscv_target_isa_load(target, &isa) ||
        (isa.xlen != 32U && isa.xlen != 64U) ||
        (isa.xlen == 32U
            ? (plan->abi32 == NULL || plan->abi64 != NULL ||
               plan->semantic_calls64 != NULL ||
               plan->abi32->semantic_calls.index == NULL ||
               plan->abi32->parameter_count !=
                   minic_core_function_parameter_count(core) ||
               plan->frame->has_hidden_result_pointer !=
                   plan->abi32->indirect_return)
            : (plan->abi64 == NULL || plan->abi32 != NULL ||
               !plan->abi64->prepared ||
               !plan->abi64->named_parameter_cursor.lp64_soft_float ||
               plan->abi64->parameter_location_count !=
                   minic_core_function_parameter_count(core) ||
               plan->frame->has_hidden_result_pointer !=
                   (plan->abi64->return_value.kind ==
                    MINIC_RISCV64_ABI_VALUE_INDIRECT) ||
               (plan->semantic_calls64 == NULL
                   ? plan->abi64->outgoing_calls.count != 0U
                   : plan->semantic_calls64->count !=
                     plan->abi64->outgoing_calls.count))) ||
        target->data_layout->pointer_size != isa.xlen / 8U ||
        (frame = plan->frame)->frame_size > (size_t)INT32_MAX ||
        frame->frame_size % 16U != 0U ||
        plan->value_offset_count != core->value_count ||
        plan->object_offset_count != core->object_count ||
        (core->value_count != 0U && plan->value_offsets == NULL) ||
        (core->object_count != 0U && plan->object_offsets == NULL) ||
        !minic_machine_function_initialize(machine, target, core->name,
            minic_core_function_calling_convention(core)))
        return false;
    machine->frame_size = frame->frame_size;
    machine->frame_alignment = 16U;
    machine->has_dynamic_stack = frame->has_dynamic_stack;
    sel.plan = plan;
    sel.xlen = isa.xlen;
    if (!minic_machine_builder_initialize(&sel.builder, machine)) goto done;
    if (core->block_count > SIZE_MAX / sizeof(*sel.core_blocks)) goto done;
    sel.core_blocks = calloc(core->block_count, sizeof(*sel.core_blocks));
    sel.far_entries = calloc(core->block_count, sizeof(*sel.far_entries));
    sel.reachable = calloc(core->block_count, sizeof(*sel.reachable));
    sel.conditional_fallthroughs = malloc(
        core->block_count * sizeof(*sel.conditional_fallthroughs));
    sel.conditional_others = malloc(
        core->block_count * sizeof(*sel.conditional_others));
    if (sel.core_blocks == NULL || sel.far_entries == NULL ||
        sel.reachable == NULL ||
        sel.conditional_fallthroughs == NULL ||
        sel.conditional_others == NULL)
        goto done;
    if (!minic_core_function_mark_reachable_blocks(
            core, sel.reachable, core->block_count)) goto done;
    if (!minic_machine_builder_create_block(&sel.builder,
        NULL, false, true, &id) || id != 0U) goto done;
    /* Core entry first (the physical ABI prologue falls through to block1),
     * then the remaining reachable blocks in deterministic Core id order. */
    for (i = 0U; i < core->block_count; ++i) {
        const size_t core_id = i == 0U ? core->entry_block :
            (i <= core->entry_block ? i - 1U : i);
        const MinicCoreBlock *block = &core->blocks[core_id];
        if (!sel.reachable[core_id]) continue;
        if (!minic_machine_builder_create_block(&sel.builder,
            NULL, true, true, &sel.far_entries[core_id])) goto done;
        if (!minic_machine_builder_create_block(&sel.builder,
            block->local_label_symbol, block->indirect_entry, false,
            &sel.core_blocks[core_id]) ||
            !minic_machine_builder_mark_core_block(&sel.builder, core_id)) goto done;
        if (block->terminator.kind ==
            MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH) {
            if (!minic_machine_builder_create_block(&sel.builder,
                NULL, false, true, &sel.conditional_fallthroughs[core_id]) ||
                !minic_machine_builder_create_block(&sel.builder,
                NULL, false, true, &sel.conditional_others[core_id]))
                goto done;
        }
    }
    if (sel.far_entries[core->entry_block] != 1U ||
        !minic_machine_builder_create_block(&sel.builder,
            NULL, true, true, &sel.epilogue_block) ||
        !minic_riscv_machine_select_prologue(&sel.builder, frame, 1U))
        goto done;
    for (i = 0U; i < core->block_count; ++i) {
        const MinicCoreBlock *block = &core->blocks[i];
        size_t j;
        if (!sel.reachable[i]) continue;
        if (!minic_machine_builder_position_at(&sel.builder,
                sel.far_entries[i]) ||
            !rv_j(&sel, sel.core_blocks[i])) goto done;
        if (!minic_machine_builder_position_at(&sel.builder,
            sel.core_blocks[i])) goto done;
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicCoreInstructionId instruction_id =
                block->instructions[j];
            if (instruction_id >= core->instruction_count) goto done;
            minic_machine_builder_set_origin(&sel.builder, i, instruction_id);
            if (!rv_core_instruction(&sel, instruction_id,
                    &core->instructions[instruction_id])) goto done;
            minic_machine_builder_clear_origin(&sel.builder);
        }
        if (!rv_core_terminator(&sel, i)) goto done;
    }
    if (!minic_riscv_machine_select_epilogue(&sel.builder, frame,
            sel.epilogue_block)) goto done;
    /* External/local-label and asm-goto entry roots belong to Core's common
     * closure. A not-yet-migrated multi-entry MachineCFG must be rejected as
     * a whole, never silently drop or falsely accept its reachable blocks. */
    if (!minic_machine_core_coverage_verify(core, sel.reachable, machine) ||
        !minic_riscv_machine_gnu_asm_policy(target, &policy) ||
        !minic_machine_gnu_asm_preflight(machine, &policy)) goto done;
    success = true;
done:
    free(sel.core_blocks);
    free(sel.far_entries);
    free(sel.reachable);
    free(sel.conditional_fallthroughs);
    free(sel.conditional_others);
    if (!success) minic_machine_function_destroy(machine);
    return success;
}
