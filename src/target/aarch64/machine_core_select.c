#include "target/aarch64/machine_core_select.h"
#include "target/aarch64/machine_frame_select.h"
#include "target/aarch64/machine_opcode.h"
#include "target/common/core_block_walk.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/elf_module_symbol_info.h"
#include "target/common/lp64_scalar.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Value slots are independently allocated by the ABI frame planner. The
 * selector fixes only the physical scratch set, not any virtual register or
 * C type. The frame selector protects live x9..x12 from its x16/x17 scratch. */
enum { A64_R0 = 0U, A64_T0 = 9U, A64_T1 = 10U,
       A64_T2 = 11U, A64_T3 = 12U };

typedef struct A64CoreSelection {
    MinicMachineBuilder builder;
    MinicMachineGNUAsmPolicy policy;
    const MinicDirectLP64BlockContext *ctx;
    size_t *blocks;
    size_t *false_blocks;
    size_t *true_blocks;
    bool *reachable;
    MinicA64FrameView view;
} A64CoreSelection;

static MinicMachineOperand a64_gp(unsigned int reg, unsigned int bits,
    unsigned int access) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){reg, (uint16_t)bits,
        MINIC_MACHINE_REGISTER_GENERAL};
    op.value.physical.access = access;
    return op;
}

static MinicMachineOperand a64_immediate(int64_t value) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_INTEGER;
    op.value.integer = value;
    return op;
}

static MinicMachineOperand a64_symbol(const char *name, uint32_t reloc) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    op.value.symbol.name = (char *)name;
    op.value.symbol.relocation = reloc;
    return op;
}

static MinicMachineOperand a64_block(size_t id) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_BLOCK;
    op.value.block_id = id;
    return op;
}

static MinicMachineOperand a64_address(unsigned int reg) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    op.value.address.target_mode = MINIC_A64_ADDRESS_UNSIGNED;
    op.value.address.has_base = true;
    op.value.address.base = (MinicMachineRegister){reg, 64U,
        MINIC_MACHINE_REGISTER_GENERAL};
    op.value.address.scale = 1U;
    return op;
}

/* The opcode's canonical contract, not an ad hoc effect guess, supplies all
 * implicit NZCV/LR accesses. The final Machine verifier checks the same
 * contract again and fails closed on illegal physical forms. */
static bool a64_emit(A64CoreSelection *sel, uint32_t opcode,
    const MinicMachineOperand *operands, size_t count) {
    MinicMachineOpcodeContract contract = {0};
    if (sel == NULL || sel->policy.verify.lookup_opcode == NULL ||
        !sel->policy.verify.lookup_opcode(sel->ctx->target, opcode,
            &contract, sel->policy.verify.context) ||
        contract.operand_count != count) return false;
    return minic_machine_builder_emit_selected(&sel->builder, opcode,
        contract.required_effects, operands, count,
        contract.implicit_count != 0U ? contract.implicit : NULL,
        contract.implicit_count);
}

static bool a64_rr(A64CoreSelection *sel, uint32_t opcode,
    unsigned int bits, unsigned int dst, unsigned int src) {
    const MinicMachineOperand ops[] = {
        a64_gp(dst, bits, MINIC_MACHINE_REGISTER_WRITE),
        a64_gp(src, bits, MINIC_MACHINE_REGISTER_READ)
    };
    return a64_emit(sel, opcode, ops, 2U);
}

static bool a64_extend_word(A64CoreSelection *sel,
    unsigned int dst, unsigned int src, bool sign) {
    const MinicMachineOperand ops[] = {
        a64_gp(dst, 64U, MINIC_MACHINE_REGISTER_WRITE),
        a64_gp(src, 32U, MINIC_MACHINE_REGISTER_READ)
    };
    return a64_emit(sel, sign ? MINIC_A64_MACHINE_SXTW :
        MINIC_A64_MACHINE_UXTW, ops, 2U);
}

static bool a64_rrr(A64CoreSelection *sel, uint32_t opcode,
    unsigned int bits, unsigned int dst, unsigned int left,
    unsigned int right) {
    const MinicMachineOperand ops[] = {
        a64_gp(dst, bits, MINIC_MACHINE_REGISTER_WRITE),
        a64_gp(left, bits, MINIC_MACHINE_REGISTER_READ),
        a64_gp(right, bits, MINIC_MACHINE_REGISTER_READ)
    };
    return a64_emit(sel, opcode, ops, 3U);
}

static bool a64_rri(A64CoreSelection *sel, uint32_t opcode,
    unsigned int bits, unsigned int dst, unsigned int src, int64_t imm) {
    const MinicMachineOperand ops[] = {
        a64_gp(dst, bits, MINIC_MACHINE_REGISTER_WRITE),
        a64_gp(src, bits, MINIC_MACHINE_REGISTER_READ),
        a64_immediate(imm)
    };
    return a64_emit(sel, opcode, ops, 3U);
}

static bool a64_compare(A64CoreSelection *sel, unsigned int bits,
    unsigned int left, unsigned int right) {
    const MinicMachineOperand ops[] = {
        a64_gp(left, bits, MINIC_MACHINE_REGISTER_READ),
        a64_gp(right, bits, MINIC_MACHINE_REGISTER_READ)
    };
    return a64_emit(sel, MINIC_A64_MACHINE_CMP, ops, 2U);
}

static bool a64_cmp_zero(A64CoreSelection *sel, unsigned int bits,
    unsigned int reg) {
    const MinicMachineOperand ops[] = {
        a64_gp(reg, bits, MINIC_MACHINE_REGISTER_READ), a64_immediate(0)
    };
    return a64_emit(sel, MINIC_A64_MACHINE_CMP_IMM, ops, 2U);
}

static bool a64_cset(A64CoreSelection *sel, unsigned int dst, int64_t cc) {
    const MinicMachineOperand ops[] = {
        a64_gp(dst, 32U, MINIC_MACHINE_REGISTER_WRITE), a64_immediate(cc)
    };
    return a64_emit(sel, MINIC_A64_MACHINE_CSET, ops, 2U);
}

static bool a64_load_constant(A64CoreSelection *sel, unsigned int reg,
    uint64_t value) {
    unsigned int limb;
    for (limb = 0U; limb < 4U; ++limb) {
        const unsigned int shift = limb * 16U;
        const MinicMachineOperand ops[] = {
            a64_gp(reg, 64U, limb == 0U ? MINIC_MACHINE_REGISTER_WRITE :
                (MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE)),
            a64_immediate((int64_t)((value >> shift) & UINT64_C(65535))),
            a64_immediate((int64_t)shift)
        };
        if (!a64_emit(sel, limb == 0U ? MINIC_A64_MACHINE_MOVZ :
                MINIC_A64_MACHINE_MOVK, ops, 3U)) return false;
    }
    return true;
}

static bool a64_load_core(A64CoreSelection *sel,
    MinicCoreValueId value, unsigned int reg, unsigned int bits) {
    const MinicLP64FramePlan *frame = sel->ctx->frame_plan;
    size_t offset;
    return minic_lp64_frame_plan_value_slot(frame, value, &offset) &&
        minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
            minic_a64_owned_slot(offset),
            (MinicA64FrameRegister){bits == 64U ? MINIC_A64_REG_X :
                MINIC_A64_REG_W, reg}, (MinicA64ScratchPolicy){0U, 0U});
}

static bool a64_store_core(A64CoreSelection *sel,
    MinicCoreValueId value, unsigned int reg, unsigned int bits) {
    size_t offset;
    return minic_lp64_frame_plan_value_slot(sel->ctx->frame_plan,
            value, &offset) &&
        minic_a64_machine_emit_frame_store(&sel->builder, &sel->view,
            minic_a64_owned_slot(offset),
            (MinicA64FrameRegister){bits == 64U ? MINIC_A64_REG_X :
                MINIC_A64_REG_W, reg}, (MinicA64ScratchPolicy){0U, 0U});
}

static bool a64_value_width(const MinicCoreFunction *fn,
    MinicCoreTypeId id, size_t *bytes, bool *is_signed,
    bool *is_pointer) {
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || bytes == NULL ||
        is_signed == NULL || is_pointer == NULL) return false;
    type = minic_core_type_get(fn->type_context,
        minic_core_type_unqualified(fn->type_context, id));
    if (type == NULL || !type->has_layout) return false;
    *is_pointer = type->kind == MINIC_CORE_TYPE_POINTER;
    if (*is_pointer) {
        if (type->size != 8U) return false;
        *bytes = 8U;
        *is_signed = false;
        return true;
    }
    return type->kind == MINIC_CORE_TYPE_INTEGER &&
        minic_lp64_core_integer_width(fn->type_context, id,
            bytes, is_signed) &&
        (*bytes == 1U || *bytes == 2U || *bytes == 4U || *bytes == 8U);
}

static bool a64_normalize(A64CoreSelection *sel, size_t width,
    bool sign, unsigned int reg) {
    switch (width) {
    case 1U:
        return a64_rr(sel, sign ? MINIC_A64_MACHINE_SXTB :
            MINIC_A64_MACHINE_UXTB, 32U, reg, reg);
    case 2U:
        return a64_rr(sel, sign ? MINIC_A64_MACHINE_SXTH :
            MINIC_A64_MACHINE_UXTH, 32U, reg, reg);
    case 4U:
    case 8U: return true;
    default: return false;
    }
}

static bool a64_binary(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    size_t lhs_width, rhs_width, dest_width;
    bool lhs_signed, rhs_signed, dest_signed, pointer;
    unsigned int bits;
    uint32_t opcode = 0U;
    bool compare;
    const MinicCoreValueId left = ins->value.binary.left;
    const MinicCoreValueId right = ins->value.binary.right;
    const bool pointer_less = ins->kind == MINIC_CORE_INSTRUCTION_POINTER_LESS;
    if (left >= fn->value_count || right >= fn->value_count) return false;
    compare = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS ||
        pointer_less || ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
    if (pointer_less) {
        if (!minic_multi_direct_pointer_less_shape(fn, ins)) return false;
    } else if (compare) {
        if (!minic_multi_direct_integer_compare_shape(fn, ins,
            &lhs_width, &rhs_width, &lhs_signed, &rhs_signed)) return false;
    } else if (!minic_multi_direct_integer_binary_shape(fn, ins,
        &lhs_width, &rhs_width, &lhs_signed, &rhs_signed)) return false;
    if (!a64_value_width(fn, fn->values[left].core_type_id, &lhs_width,
            &lhs_signed, &pointer) ||
        !a64_value_width(fn, fn->values[right].core_type_id, &rhs_width,
            &rhs_signed, &pointer) || lhs_width != rhs_width ||
        !a64_value_width(fn, ins->core_type_id, &dest_width,
            &dest_signed, &pointer)) return false;
    bits = lhs_width == 8U ? 64U : 32U;
    if (!a64_load_core(sel, left, A64_T0, bits) ||
        !a64_load_core(sel, right, A64_T1, bits)) return false;
    if (compare) {
        const int64_t cc = ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL
            ? 0 : lhs_signed && !pointer_less ? 11 : 3;
        return a64_compare(sel, bits, A64_T0, A64_T1) &&
            a64_cset(sel, A64_T0, cc) &&
            a64_store_core(sel, ins->result, A64_T0, 32U);
    }
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD: opcode = MINIC_A64_MACHINE_ADD; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT: opcode = MINIC_A64_MACHINE_SUB; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY: opcode = MINIC_A64_MACHINE_MUL; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND: opcode = MINIC_A64_MACHINE_AND; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR: opcode = MINIC_A64_MACHINE_ORR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR: opcode = MINIC_A64_MACHINE_EOR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
        opcode = lhs_signed ? MINIC_A64_MACHINE_SDIV : MINIC_A64_MACHINE_UDIV;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT: opcode = MINIC_A64_MACHINE_LSL; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        opcode = lhs_signed ? MINIC_A64_MACHINE_ASR : MINIC_A64_MACHINE_LSR;
        break;
    default: return false;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER) {
        const MinicMachineOperand ops[] = {
            a64_gp(A64_T0, bits, MINIC_MACHINE_REGISTER_WRITE),
            a64_gp(A64_T2, bits, MINIC_MACHINE_REGISTER_READ),
            a64_gp(A64_T1, bits, MINIC_MACHINE_REGISTER_READ),
            a64_gp(A64_T0, bits, MINIC_MACHINE_REGISTER_READ)
        };
        if (!a64_rrr(sel, opcode, bits, A64_T2, A64_T0, A64_T1) ||
            !a64_emit(sel, MINIC_A64_MACHINE_MSUB, ops, 4U)) return false;
    } else if (!a64_rrr(sel, opcode, bits, A64_T0, A64_T0, A64_T1))
        return false;
    return a64_normalize(sel, dest_width, dest_signed, A64_T0) &&
        a64_store_core(sel, ins->result, A64_T0, bits);
}

static bool a64_unary(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    size_t width;
    bool sign, is_pointer;
    uint32_t opcode;
    unsigned int bits;
    unsigned int scan_bits;
    if (ins->value.operand >= fn->value_count ||
        !a64_value_width(fn, fn->values[ins->value.operand].core_type_id,
            &width, &sign, &is_pointer)) return false;
    if ((ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ||
         ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT) &&
        (is_pointer ||
         !minic_multi_direct_integer_unary_shape(fn, ins,
             &width, &sign))) return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO &&
        !minic_multi_direct_scalar_zero_shape(fn, ins,
            &width, &sign, &is_pointer)) return false;
    bits = width == 8U ? 64U : 32U;
    if (!a64_load_core(sel, ins->value.operand, A64_T0, bits)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        opcode = MINIC_A64_MACHINE_NEG; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        opcode = MINIC_A64_MACHINE_MVN; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        if (!minic_core_integer_bitscan_shape(fn, ins, &scan_bits) ||
            scan_bits != width * 8U) return false;
        if (scan_bits == 8U &&
            !a64_rr(sel, MINIC_A64_MACHINE_UXTB, 32U, A64_T0, A64_T0))
            return false;
        if (scan_bits == 16U &&
            !a64_rr(sel, MINIC_A64_MACHINE_UXTH, 32U, A64_T0, A64_T0))
            return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CTZ &&
            !a64_rr(sel, MINIC_A64_MACHINE_RBIT, bits,
                A64_T0, A64_T0)) return false;
        opcode = MINIC_A64_MACHINE_CLZ; break;
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return a64_cmp_zero(sel, bits, A64_T0) &&
            a64_cset(sel, A64_T0, 0) &&
            a64_store_core(sel, ins->result, A64_T0, 32U);
    default: return false;
    }
    if (!a64_rr(sel, opcode, bits, A64_T0, A64_T0)) return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CTZ) {
        /* CLZ counts the complete architectural W register. Narrow Core
         * integer widths first strip high garbage, then subtract W padding
         * only for CLZ; RBIT+CLZ already yields exact narrow CTZ. */
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ &&
            width < 4U &&
            !a64_rri(sel, MINIC_A64_MACHINE_SUB_IMM, 32U,
                A64_T0, A64_T0, (int64_t)(32U - width * 8U))) return false;
        return a64_store_core(sel, ins->result, A64_T0, 32U);
    }
    return a64_normalize(sel, width, sign, A64_T0) &&
        a64_store_core(sel, ins->result, A64_T0, bits);
}

static bool a64_symbol_materialize(A64CoreSelection *sel,
    const char *name, MinicElfModuleSymbolKind symbol_kind) {
    const MinicElfModuleSymbolFacts facts = minic_multi_elf_module_symbol_facts(
        sel->ctx->module, name, symbol_kind);
    const bool got = facts.undefined_weak ||
        (sel->ctx->target->position_independent && !facts.nonpreemptible);
    const MinicMachineOperand adrp[] = {
        a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_WRITE),
        a64_symbol(name, got ? MINIC_A64_RELOC_GOT_PAGE :
            MINIC_A64_RELOC_PAGE)
    };
    if (!a64_emit(sel, MINIC_A64_MACHINE_ADRP, adrp, 2U)) return false;
    if (got) {
        MinicMachineOperand memory = a64_address(A64_T0);
        MinicMachineOperand ops[2];
        memory.value.address.target_mode = MINIC_A64_ADDRESS_GOT_LO12;
        memory.value.address.has_symbol = true;
        memory.value.address.symbol = (char *)name;
        memory.value.address.relocation = MINIC_A64_RELOC_GOT_LO12;
        ops[0] = a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_WRITE);
        ops[1] = memory;
        return a64_emit(sel, MINIC_A64_MACHINE_LDR_GOT_LO12, ops, 2U);
    }
    { const MinicMachineOperand ops[] = {
        a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_WRITE),
        a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_READ),
        a64_symbol(name, MINIC_A64_RELOC_LO12)
    };
      return a64_emit(sel, MINIC_A64_MACHINE_ADD_LO12, ops, 3U);
    }
}

static bool a64_core_instruction(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicLP64FramePlan *frame = sel->ctx->frame_plan;
    size_t width;
    bool sign, ptr;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return a64_value_width(fn, ins->core_type_id, &width, &sign, &ptr) &&
            a64_load_constant(sel, A64_T0,
                (uint64_t)ins->value.integer_value) &&
            a64_normalize(sel, width, sign, A64_T0) &&
            a64_store_core(sel, ins->result, A64_T0,
                width == 8U ? 64U : 32U);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
        const MinicCoreValueId from = ins->value.operand;
        size_t from_width, to_width;
        bool from_signed, to_signed, from_pointer, to_pointer;
        if (from >= fn->value_count ||
            !a64_value_width(fn, fn->values[from].core_type_id,
                &from_width, &from_signed, &from_pointer) ||
            !a64_value_width(fn, ins->core_type_id,
                &to_width, &to_signed, &to_pointer) ||
            from_pointer || to_pointer ||
            !a64_load_core(sel, from, A64_T0,
                from_width == 8U ? 64U : 32U)) return false;
        if (to_width == 8U && from_width < 8U &&
            !a64_extend_word(sel, A64_T0, A64_T0, from_signed)) return false;
        return a64_normalize(sel, to_width, to_signed, A64_T0) &&
            a64_store_core(sel, ins->result, A64_T0,
                to_width == 8U ? 64U : 32U);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        return a64_binary(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return a64_unary(sel, ins);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
        size_t at;
        return minic_lp64_frame_plan_object(frame, ins->value.object_id,
                &at) &&
            minic_a64_machine_emit_frame_address(&sel->builder, &sel->view,
                minic_a64_owned_slot(at), A64_T0,
                (MinicA64ScratchPolicy){0U, 0U}) &&
            a64_store_core(sel, ins->result, A64_T0, 64U);
    }
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
        MinicCoreValueId base;
        size_t offset;
        if (!minic_multi_direct_field_address_shape(fn, ins,
                &base, &offset) ||
            !a64_load_core(sel, base, A64_T0, 64U)) return false;
        if (offset != 0U) {
            if (offset <= 4095U) {
                if (!a64_rri(sel, MINIC_A64_MACHINE_ADD_IMM,
                    64U, A64_T0, A64_T0, (int64_t)offset)) return false;
            } else if (!a64_load_constant(sel, A64_T1, (uint64_t)offset) ||
                !a64_rrr(sel, MINIC_A64_MACHINE_ADD, 64U,
                    A64_T0, A64_T0, A64_T1)) return false;
        }
        return a64_store_core(sel, ins->result, A64_T0, 64U);
    }
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
        const MinicCoreValueId base = ins->value.pointer_offset.base;
        const MinicCoreValueId index = ins->value.pointer_offset.index;
        const size_t stride = ins->value.pointer_offset.element_size;
        size_t iw;
        bool signed_index;
        if (base >= fn->value_count || index >= fn->value_count ||
            !minic_multi_direct_pointer_offset_shape(fn, ins,
                SIZE_MAX, &iw, &signed_index) ||
            (iw != 1U && iw != 2U && iw != 4U && iw != 8U) ||
            !a64_load_core(sel, base, A64_T0, 64U) ||
            !a64_load_core(sel, index, A64_T1,
                iw == 8U ? 64U : 32U)) return false;
        if (iw < 8U && !a64_extend_word(sel,
                A64_T1, A64_T1, signed_index)) return false;
        if (stride != 0U) {
            if (!a64_load_constant(sel, A64_T2, (uint64_t)stride) ||
                !a64_rrr(sel, MINIC_A64_MACHINE_MUL, 64U,
                    A64_T1, A64_T1, A64_T2) ||
                !a64_rrr(sel, ins->value.pointer_offset.subtract ?
                    MINIC_A64_MACHINE_SUB : MINIC_A64_MACHINE_ADD,
                    64U, A64_T0, A64_T0, A64_T1)) return false;
        }
        return a64_store_core(sel, ins->result, A64_T0, 64U);
    }
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
        const char *name = NULL;
        const bool object = ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS;
        if (!(object ? minic_multi_direct_global_address_symbol(fn, ins, &name)
                     : minic_multi_direct_function_address_symbol(fn, ins,
                        &name)) || name == NULL ||
            !a64_symbol_materialize(sel, name, object ?
                MINIC_ELF_MODULE_SYMBOL_OBJECT :
                MINIC_ELF_MODULE_SYMBOL_FUNCTION)) return false;
        return a64_store_core(sel, ins->result, A64_T0, 64U);
    }
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE: {
        const bool load = ins->kind == MINIC_CORE_INSTRUCTION_LOAD;
        const MinicCoreValueId address = load ? ins->value.load.address :
            ins->value.store.address;
        const MinicCoreValueId source = load ? ins->result :
            ins->value.store.stored_value;
        uint32_t op;
        bool vol;
        unsigned int bits;
        MinicMachineOperand ops[2];
        if (!(load ? minic_multi_direct_scalar_load_shape(fn, ins,
                &width, &sign, &ptr) :
            minic_multi_direct_scalar_store_shape(fn, ins,
                &width, &sign, &ptr)) ||
            !a64_load_core(sel, address, A64_T0, 64U)) return false;
        switch (width) {
        case 1U: op = load ? (sign ? MINIC_A64_MACHINE_LDRSB :
            MINIC_A64_MACHINE_LDRB) : MINIC_A64_MACHINE_STRB; break;
        case 2U: op = load ? (sign ? MINIC_A64_MACHINE_LDRSH :
            MINIC_A64_MACHINE_LDRH) : MINIC_A64_MACHINE_STRH; break;
        case 4U: op = load ? MINIC_A64_MACHINE_LDR_W :
            MINIC_A64_MACHINE_STR_W; break;
        case 8U: op = load ? MINIC_A64_MACHINE_LDR_X :
            MINIC_A64_MACHINE_STR_X; break;
        default: return false;
        }
        bits = width == 8U ? 64U : 32U;
        if (!load && !a64_load_core(sel, source, A64_T1, bits)) return false;
        ops[0] = a64_gp(A64_T1, bits,
            load ? MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
        ops[1] = a64_address(A64_T0);
        vol = load ? ins->value.load.is_volatile :
            ins->value.store.is_volatile;
        /* Volatility is an independent machine effect, never an opcode. */
        if (!a64_emit(sel, op, ops, 2U)) return false;
        if (vol) {
            MinicMachineInstruction *native = &sel->builder.function->blocks[
                sel->builder.current_block].instructions[
                    sel->builder.function->blocks[
                        sel->builder.current_block].instruction_count - 1U];
            native->effects |= MINIC_MACHINE_EFFECT_VOLATILE;
        }
        return !load || a64_store_core(sel, ins->result, A64_T1, bits);
    }
    default:
        /* Deliberate whole-function admission: one unsupported Core effect
         * rejects this selector instead of silently delegating instruction
         * text to the legacy emitter. */
        return false;
    }
}

static bool a64_branch(A64CoreSelection *sel, size_t to) {
    const MinicMachineOperand target[] = {a64_block(to)};
    return a64_emit(sel, MINIC_A64_MACHINE_B, target, 1U) &&
        minic_machine_builder_add_successor(&sel->builder, to);
}

static bool a64_terminate(A64CoreSelection *sel, size_t core_id) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreBlock *block = &fn->blocks[core_id];
    const MinicCoreTerminator *term = &block->terminator;
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_BRANCH:
        return term->branch_target < fn->block_count &&
            sel->blocks[term->branch_target] != SIZE_MAX &&
            a64_branch(sel, sel->blocks[term->branch_target]);
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
        const size_t tt = term->conditional.when_true;
        const size_t ff = term->conditional.when_false;
        const MinicCoreValueId condition = term->conditional.condition;
        size_t width;
        bool sign, pointer;
        size_t true_target;
        const MinicMachineOperand conditional[] = {
            a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_READ),
            a64_block(0U)
        };
        MinicMachineOperand op[2];
        if (tt >= fn->block_count || ff >= fn->block_count ||
            condition >= fn->value_count ||
            sel->blocks[tt] == SIZE_MAX || sel->blocks[ff] == SIZE_MAX ||
            !a64_value_width(fn, fn->values[condition].core_type_id,
                &width, &sign, &pointer) ||
            sel->false_blocks[core_id] == SIZE_MAX ||
            !a64_load_core(sel, condition, A64_T0,
                width == 8U ? 64U : 32U)) return false;
        true_target = sel->true_blocks[core_id] == SIZE_MAX
            ? sel->blocks[tt] : sel->true_blocks[core_id];
        op[0] = conditional[0];
        op[0].value.physical.reg.width_bits =
            (uint16_t)(width == 8U ? 64U : 32U);
        op[1] = a64_block(true_target);
        if (!a64_emit(sel, MINIC_A64_MACHINE_CBNZ, op, 2U) ||
            !minic_machine_builder_add_successor(&sel->builder, true_target) ||
            !minic_machine_builder_add_successor(&sel->builder,
                sel->false_blocks[core_id]) ||
            !minic_machine_builder_set_fallthrough(&sel->builder,
                sel->false_blocks[core_id]) ||
            !minic_machine_builder_position_at(&sel->builder,
                sel->false_blocks[core_id]) ||
            !a64_branch(sel, sel->blocks[ff])) return false;
        if (sel->true_blocks[core_id] != SIZE_MAX) {
            if (!minic_machine_builder_position_at(&sel->builder,
                    sel->true_blocks[core_id]) ||
                !a64_branch(sel, sel->blocks[tt])) return false;
        }
        return true;
    }
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH: {
        size_t i;
        const MinicMachineOperand ops[] = {
            a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_READ)
        };
        if (!a64_load_core(sel, term->indirect_target, A64_T0, 64U) ||
            !a64_emit(sel, MINIC_A64_MACHINE_BR, ops, 1U)) return false;
        for (i = 0U; i < fn->block_count; ++i)
            if (sel->reachable[i] && fn->blocks[i].indirect_entry &&
                !minic_machine_builder_add_successor(&sel->builder,
                    sel->blocks[i])) return false;
        return true;
    }
    case MINIC_CORE_TERMINATOR_RETURN: {
        const MinicCoreTypeId ret_type = minic_core_function_return_type(fn);
        const MinicCoreType *type = fn->type_context != NULL ?
            minic_core_type_get(fn->type_context,
                minic_core_type_unqualified(fn->type_context, ret_type)) : NULL;
        if (type == NULL) return false;
        if (type->kind != MINIC_CORE_TYPE_VOID) {
            size_t width;
            bool sign, ptr;
            if (term->return_value >= fn->value_count ||
                !a64_value_width(fn, ret_type, &width, &sign, &ptr) ||
                !a64_load_core(sel, term->return_value, A64_R0,
                    width == 8U ? 64U : 32U)) return false;
        }
        return minic_a64_machine_emit_epilogue(&sel->builder,
            sel->ctx->machine_frame);
    }
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return a64_emit(sel, MINIC_A64_MACHINE_BRK, NULL, 0U);
    default: return false;
    }
}

bool minic_aarch64_machine_select_core_subset(
    MinicMachineFunction *machine, const MinicDirectLP64BlockContext *ctx) {
    const MinicCoreFunction *fn;
    A64CoreSelection sel = {0};
    size_t count, prologue, i;
    bool ok = false;
    if (machine == NULL || ctx == NULL || ctx->target == NULL ||
        ctx->target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        ctx->fn == NULL || ctx->module == NULL || ctx->frame_plan == NULL ||
        ctx->machine_frame == NULL || ctx->name == NULL) return false;
    fn = ctx->fn;
    count = fn->block_count;
    if (count == 0U || count > SIZE_MAX / sizeof(size_t) ||
        fn->entry_block >= count || fn->blocks == NULL) return false;
    sel.ctx = ctx;
    sel.blocks = malloc(count * sizeof(*sel.blocks));
    sel.false_blocks = malloc(count * sizeof(*sel.false_blocks));
    sel.true_blocks = malloc(count * sizeof(*sel.true_blocks));
    sel.reachable = calloc(count, sizeof(*sel.reachable));
    if (sel.blocks == NULL || sel.false_blocks == NULL ||
        sel.true_blocks == NULL || sel.reachable == NULL ||
        !minic_aarch64_machine_gnu_asm_policy_initialize(
            ctx->target, &sel.policy) ||
        !minic_machine_function_initialize(machine, ctx->target, ctx->name,
            MINIC_A64_MACHINE_FEATURE_BASE | MINIC_A64_MACHINE_FEATURE_FP_SIMD))
        goto cleanup;
    for (i = 0U; i < count; ++i) {
        sel.blocks[i] = SIZE_MAX;
        sel.false_blocks[i] = SIZE_MAX;
        sel.true_blocks[i] = SIZE_MAX;
    }
    if (ctx->core_facts != NULL &&
        ctx->core_facts->reachable_blocks != NULL)
        memcpy(sel.reachable, ctx->core_facts->reachable_blocks,
            count * sizeof(*sel.reachable));
    else if (!minic_core_function_mark_reachable_blocks(fn,
        sel.reachable, count)) goto cleanup;
    if (!sel.reachable[fn->entry_block]) goto cleanup;
    machine->frame_size = ctx->machine_frame->frame_size;
    machine->frame_alignment = ctx->machine_frame->frame_alignment;
    machine->has_dynamic_stack = ctx->machine_frame->has_dynamic_stack;
    machine->calling_convention = minic_core_function_calling_convention(fn);
    sel.view = (MinicA64FrameView){machine->has_dynamic_stack,
        machine->frame_size, 0U,
        ctx->machine_frame->saved_entry_sp_offset};
    if (!minic_machine_builder_initialize(&sel.builder, machine) ||
        !minic_machine_builder_create_block(&sel.builder, NULL, false,
            true, &prologue) || prologue != 0U) goto cleanup;
    /* No CFG edge ever targets Machine block0. It owns ONLY the ABI prologue.
     * Every Core entry/backedge is a normal labeled block AFTER the prologue. */
    for (i = 0U; i < count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        size_t id;
        if (!sel.reachable[i]) continue;
        if (!minic_machine_builder_create_block(&sel.builder,
                block->local_label_symbol, block->indirect_entry,
                false, &id)) goto cleanup;
        sel.blocks[i] = id;
        if (block->terminator.kind ==
                MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH) {
            if (!minic_machine_builder_create_block(&sel.builder, NULL,
                    false, true, &id)) goto cleanup;
            sel.false_blocks[i] = id;
            if (count > 256U) {
                if (!minic_machine_builder_create_block(&sel.builder, NULL,
                        false, true, &id)) goto cleanup;
                sel.true_blocks[i] = id;
            }
        }
    }
    if (!minic_machine_builder_position_at(&sel.builder, prologue) ||
        !minic_a64_machine_emit_prologue(&sel.builder, ctx->machine_frame))
        goto cleanup;
    if (!a64_branch(&sel, sel.blocks[fn->entry_block])) goto cleanup;
    for (i = 0U; i < count; ++i) {
        const MinicCoreBlock *block = &fn->blocks[i];
        size_t index;
        if (!sel.reachable[i]) continue;
        if (!block->has_terminator ||
            !minic_machine_builder_position_at(&sel.builder,
                sel.blocks[i])) goto cleanup;
        for (index = 0U; index < block->instruction_count; ++index) {
            MinicCoreInstructionId ins_id;
            const MinicCoreInstruction *ins;
            if (!minic_core_block_instruction_at(fn, block, index,
                    &ins_id, &ins)) goto cleanup;
            minic_machine_builder_set_origin(&sel.builder, i, ins_id);
            if (!a64_core_instruction(&sel, ins)) goto cleanup;
        }
        minic_machine_builder_clear_origin(&sel.builder);
        if (!a64_terminate(&sel, i)) goto cleanup;
    }
    ok = minic_machine_gnu_asm_preflight(machine, &sel.policy);
cleanup:
    free(sel.blocks);
    free(sel.false_blocks);
    free(sel.true_blocks);
    free(sel.reachable);
    if (!ok) minic_machine_function_destroy(machine);
    return ok;
}
