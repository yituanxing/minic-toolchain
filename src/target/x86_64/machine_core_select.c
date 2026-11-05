#include "target/x86_64/machine_core_select.h"

#include "target/x86_64/abi.h"
#include "target/x86_64/codegen_options.h"
#include "target/x86_64/machine_frame_select.h"
#include "target/x86_64/machine_opcode.h"
#include "target/x86_64/machine_target_state.h"
#include "target/common/machine_builder.h"
#include "target/common/core_block_walk.h"
#include "target/common/machine_gnu_asm.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The physical scratch convention deliberately matches the original native
 * x86 backend. Every Core value still lives in its finalized RBP frame slot;
 * this is instruction selection, not register allocation or optimization. */
typedef struct X86CoreSelector {
    MinicMachineBuilder builder;
    MinicMachineGNUAsmPolicy policy;
    const MinicCoreFunction *core;
    const MinicX86Frame *frame;
    const MinicTargetInfo *target;
    MinicX86MachineFrameView frame_view;
    size_t *blocks;
    size_t *false_blocks;
    bool *reachable;
} X86CoreSelector;

static MinicMachineOperand x86_reg(unsigned int number, unsigned int bits) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){
        number, (uint16_t)bits, MINIC_MACHINE_REGISTER_GENERAL};
    return op;
}

static MinicMachineOperand x86_xmm(unsigned int number) {
    MinicMachineOperand op = x86_reg(number, 128U);
    op.value.physical.reg.bank = MINIC_MACHINE_REGISTER_VECTOR;
    return op;
}

static MinicMachineOperand x86_imm(int64_t value) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_INTEGER;
    op.value.integer = value;
    return op;
}

static MinicMachineOperand x86_symbol(const char *name,
    uint32_t relocation) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    op.value.symbol.name = (char *)name;
    op.value.symbol.relocation = relocation;
    return op;
}

static MinicMachineOperand x86_block(size_t id) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_BLOCK;
    op.value.block_id = id;
    return op;
}

static MinicMachineOperand x86_addr(unsigned int base, int64_t offset) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    op.value.address.has_base = true;
    op.value.address.base = (MinicMachineRegister){
        base, 64U, MINIC_MACHINE_REGISTER_GENERAL};
    op.value.address.displacement = offset;
    op.value.address.scale = 1U;
    return op;
}

static bool x86_emit(X86CoreSelector *sel, MinicX86MachineOpcode opcode,
    MinicMachineOperand *ops, size_t count, unsigned int extra_effects) {
    MinicMachineOpcodeContract contract = {0};
    size_t i;
    if (sel == NULL || !minic_x86_64_machine_opcode_lookup(
            sel->target, opcode, &contract, NULL) ||
        contract.operand_count != count ||
        (extra_effects & ~contract.permitted_effects) != 0U)
        return false;
    for (i = 0U; i < count; ++i) {
        if (ops[i].kind != contract.operand_kinds[i]) return false;
        if (ops[i].kind == MINIC_MACHINE_OPERAND_REGISTER)
            ops[i].value.physical.access = contract.register_access[i];
    }
    return minic_machine_builder_emit_selected(&sel->builder, opcode,
        contract.required_effects | extra_effects,
        count != 0U ? ops : NULL, count,
        contract.implicit_count != 0U ? contract.implicit : NULL,
        contract.implicit_count);
}

static bool x86_0(X86CoreSelector *sel, MinicX86MachineOpcode opcode) {
    return x86_emit(sel, opcode, NULL, 0U, 0U);
}

static bool x86_1(X86CoreSelector *sel, MinicX86MachineOpcode opcode,
    MinicMachineOperand operand) {
    return x86_emit(sel, opcode, &operand, 1U, 0U);
}

static bool x86_2(X86CoreSelector *sel, MinicX86MachineOpcode opcode,
    MinicMachineOperand first, MinicMachineOperand second) {
    MinicMachineOperand operands[2] = {first, second};
    return x86_emit(sel, opcode, operands, 2U, 0U);
}

static bool x86_2_volatile(X86CoreSelector *sel,
    MinicX86MachineOpcode opcode, MinicMachineOperand first,
    MinicMachineOperand second, bool is_volatile) {
    MinicMachineOperand operands[2] = {first, second};
    return x86_emit(sel, opcode, operands, 2U,
        is_volatile ? MINIC_MACHINE_EFFECT_VOLATILE : 0U);
}

static bool x86_rr(X86CoreSelector *sel, MinicX86MachineOpcode opcode,
    unsigned int bits, unsigned int src, unsigned int dst) {
    return x86_2(sel, opcode, x86_reg(src, bits), x86_reg(dst, bits));
}

static bool x86_ir(X86CoreSelector *sel, MinicX86MachineOpcode opcode,
    int64_t immediate, unsigned int bits, unsigned int dst) {
    return x86_2(sel, opcode, x86_imm(immediate), x86_reg(dst, bits));
}

static bool x86_slot(X86CoreSelector *sel, MinicCoreValueId value,
    MinicMachineOperand *operand) {
    size_t distance;
    if (sel == NULL || sel->frame->value_offsets == NULL ||
        value >= sel->frame->value_count) return false;
    distance = sel->frame->value_offsets[value];
    if (distance < 8U || distance > sel->frame->frame_size ||
        distance > INT32_MAX) return false;
    return minic_x86_64_machine_frame_slot(&sel->builder,
        MINIC_MACHINE_FRAME_BASE_FRAME_POINTER,
        MINIC_MACHINE_FRAME_LOCAL, 8U, 1U, -(int64_t)distance, operand);
}

static bool x86_core_load(X86CoreSelector *sel, MinicCoreValueId value,
    unsigned int reg) {
    MinicMachineOperand slot;
    return x86_slot(sel, value, &slot) &&
        x86_2(sel, MINIC_X86_MI_MOV64_FRBP_R,
            slot, x86_reg(reg, 64U));
}

static bool x86_core_store(X86CoreSelector *sel, MinicCoreValueId value,
    unsigned int reg) {
    MinicMachineOperand slot;
    return x86_slot(sel, value, &slot) &&
        x86_2(sel, MINIC_X86_MI_MOV64_R_FRBP,
            x86_reg(reg, 64U), slot);
}

static const MinicCoreType *x86_type(const X86CoreSelector *sel,
    MinicCoreTypeId id) {
    if (sel == NULL || sel->core->type_context == NULL) return NULL;
    return minic_core_type_get(sel->core->type_context,
        minic_core_type_unqualified(sel->core->type_context, id));
}

static bool x86_scalar_shape(const X86CoreSelector *sel,
    MinicCoreTypeId id, size_t *bytes, bool *is_signed, bool *is_bool) {
    const MinicCoreType *type = x86_type(sel, id);
    if (bytes == NULL || is_signed == NULL || is_bool == NULL ||
        type == NULL || !type->has_layout) return false;
    *is_bool = type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->value.integer.is_bool;
    if (type->kind == MINIC_CORE_TYPE_POINTER && type->size == 8U) {
        *bytes = 8U;
        *is_signed = false;
        return true;
    }
    if (type->kind != MINIC_CORE_TYPE_INTEGER || type->size == 0U ||
        type->size > 8U ||
        (type->size != 1U && type->size != 2U &&
         type->size != 4U && type->size != 8U)) return false;
    *bytes = type->size;
    *is_signed = type->value.integer.is_signed;
    return true;
}

static bool x86_normalize(X86CoreSelector *sel, MinicCoreTypeId type_id) {
    size_t bytes;
    bool sign, is_bool;
    if (!x86_scalar_shape(sel, type_id, &bytes, &sign, &is_bool))
        return false;
    if (is_bool)
        return x86_rr(sel, MINIC_X86_MI_TEST64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RAX) &&
            x86_1(sel, MINIC_X86_MI_SETNZ8_R,
                x86_reg(MINIC_X86_REG_RAX, 8U)) &&
            x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
                x86_reg(MINIC_X86_REG_RAX, 8U),
                x86_reg(MINIC_X86_REG_RAX, 64U));
    switch (bytes) {
    case 1U:
        return x86_2(sel, sign ? MINIC_X86_MI_MOVSBQ_RR :
                MINIC_X86_MI_MOVZBQ_RR,
            x86_reg(MINIC_X86_REG_RAX, 8U),
            x86_reg(MINIC_X86_REG_RAX, 64U));
    case 2U:
        return x86_2(sel, sign ? MINIC_X86_MI_MOVSWQ_RR :
                MINIC_X86_MI_MOVZWQ_RR,
            x86_reg(MINIC_X86_REG_RAX, 16U),
            x86_reg(MINIC_X86_REG_RAX, 64U));
    case 4U:
        if (sign)
            return x86_2(sel, MINIC_X86_MI_MOVSLQ_RR,
                x86_reg(MINIC_X86_REG_RAX, 32U),
                x86_reg(MINIC_X86_REG_RAX, 64U));
        return x86_rr(sel, MINIC_X86_MI_MOV32_RR, 32U,
            MINIC_X86_REG_RAX, MINIC_X86_REG_RAX);
    case 8U: return true;
    default: return false;
    }
}

static bool x86_result(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    return ins->result < sel->core->value_count &&
        x86_normalize(sel, ins->core_type_id) &&
        x86_core_store(sel, ins->result, MINIC_X86_REG_RAX);
}

static bool x86_float_shape(const X86CoreSelector *sel,
    MinicCoreTypeId id, bool *is_double) {
    const MinicCoreType *type = x86_type(sel, id);
    if (type == NULL || type->kind != MINIC_CORE_TYPE_FLOAT ||
        !type->has_layout || is_double == NULL) return false;
    if (type->size == 4U &&
        type->value.floating.format == MINIC_CORE_FLOAT_BINARY32) {
        *is_double = false;
        return true;
    }
    if (type->size == 8U &&
        type->value.floating.format == MINIC_CORE_FLOAT_BINARY64) {
        *is_double = true;
        return true;
    }
    return false;
}

/* Scalar FP is transported through the same integer backing slots as the
 * legacy target, but the arithmetic is genuine SSE2 in physical XMM bank. */
static bool x86_float_load_xmm(X86CoreSelector *sel,
    MinicCoreValueId id, unsigned int xmm_reg, unsigned int gp_reg,
    bool is_double) {
    return x86_core_load(sel, id, gp_reg) &&
        x86_2(sel, is_double ? MINIC_X86_MI_MOVQ_GPR_XMM :
            MINIC_X86_MI_MOVD_GPR_XMM,
            x86_reg(gp_reg, is_double ? 64U : 32U), x86_xmm(xmm_reg));
}

static bool x86_float_store_xmm(X86CoreSelector *sel,
    MinicCoreValueId id, unsigned int xmm_reg, bool is_double) {
    return x86_2(sel, is_double ? MINIC_X86_MI_MOVQ_XMM_GPR :
        MINIC_X86_MI_MOVD_XMM_GPR,
        x86_xmm(xmm_reg),
        x86_reg(MINIC_X86_REG_RAX, is_double ? 64U : 32U)) &&
        x86_core_store(sel, id, MINIC_X86_REG_RAX);
}

static bool x86_float_binary(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    bool is_double;
    MinicX86MachineOpcode opcode;
    if (ins->result >= sel->core->value_count ||
        !x86_float_shape(sel, ins->core_type_id, &is_double) ||
        ins->value.binary.left >= sel->core->value_count ||
        ins->value.binary.right >= sel->core->value_count ||
        !x86_float_load_xmm(sel, ins->value.binary.left,
            0U, MINIC_X86_REG_RAX, is_double) ||
        !x86_float_load_xmm(sel, ins->value.binary.right,
            1U, MINIC_X86_REG_R10, is_double)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        opcode = is_double ? MINIC_X86_MI_ADDSD_XMM :
            MINIC_X86_MI_ADDSS_XMM; break;
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        opcode = is_double ? MINIC_X86_MI_SUBSD_XMM :
            MINIC_X86_MI_SUBSS_XMM; break;
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
        opcode = is_double ? MINIC_X86_MI_MULSD_XMM :
            MINIC_X86_MI_MULSS_XMM; break;
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        opcode = is_double ? MINIC_X86_MI_DIVSD_XMM :
            MINIC_X86_MI_DIVSS_XMM; break;
    default: return false;
    }
    return x86_2(sel, opcode, x86_xmm(1U), x86_xmm(0U)) &&
        x86_float_store_xmm(sel, ins->result, 0U, is_double);
}

static bool x86_integer_binary(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    MinicCoreValueId left = ins->value.binary.left;
    MinicCoreValueId right = ins->value.binary.right;
    size_t bytes;
    bool sign, is_bool;
    MinicX86MachineOpcode opcode = MINIC_X86_MI_INVALID;
    if (left >= sel->core->value_count ||
        right >= sel->core->value_count ||
        !x86_scalar_shape(sel,
            sel->core->values[left].core_type_id,
            &bytes, &sign, &is_bool) ||
        !x86_core_load(sel, left, MINIC_X86_REG_RAX) ||
        !x86_core_load(sel, right, MINIC_X86_REG_R10)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        opcode = MINIC_X86_MI_ADD64_RR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        opcode = MINIC_X86_MI_SUB64_RR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        opcode = MINIC_X86_MI_IMUL64_RR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        opcode = MINIC_X86_MI_AND64_RR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        opcode = MINIC_X86_MI_OR64_RR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        opcode = MINIC_X86_MI_XOR64_RR; break;
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
        if (!(sign ? x86_0(sel, MINIC_X86_MI_CQTO) :
                 x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                     MINIC_X86_REG_RDX, MINIC_X86_REG_RDX)) ||
            !x86_1(sel, sign ? MINIC_X86_MI_IDIV64_R :
                MINIC_X86_MI_DIV64_R,
                x86_reg(MINIC_X86_REG_R10, 64U))) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER &&
            !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_RDX, MINIC_X86_REG_RAX)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        if (!x86_rr(sel, MINIC_X86_MI_MOV8_RR, 8U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RCX) ||
            !x86_1(sel, ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                ? MINIC_X86_MI_SHL64_CL_R :
                sign ? MINIC_X86_MI_SAR64_CL_R : MINIC_X86_MI_SHR64_CL_R,
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!x86_rr(sel, MINIC_X86_MI_CMP64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX) ||
            !x86_1(sel,
                ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL
                    ? MINIC_X86_MI_SETZ8_R :
                ins->kind == MINIC_CORE_INSTRUCTION_POINTER_LESS || !sign
                    ? MINIC_X86_MI_SETB8_R : MINIC_X86_MI_SETL8_R,
                x86_reg(MINIC_X86_REG_RAX, 8U)) ||
            !x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
                x86_reg(MINIC_X86_REG_RAX, 8U),
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        break;
    default: return false;
    }
    if (opcode != MINIC_X86_MI_INVALID &&
        !x86_rr(sel, opcode, 64U,
            MINIC_X86_REG_R10, MINIC_X86_REG_RAX)) return false;
    return x86_result(sel, ins);
}

static bool x86_unary(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    if (ins->value.operand >= sel->core->value_count ||
        !x86_core_load(sel, ins->value.operand, MINIC_X86_REG_RAX))
        return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        if (!x86_1(sel, MINIC_X86_MI_NEG64_R,
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        if (!x86_1(sel, MINIC_X86_MI_NOT64_R,
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (!x86_rr(sel, MINIC_X86_MI_TEST64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RAX) ||
            !x86_1(sel, MINIC_X86_MI_SETZ8_R,
                x86_reg(MINIC_X86_REG_RAX, 8U)) ||
            !x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
                x86_reg(MINIC_X86_REG_RAX, 8U),
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        break;
    default: return false;
    }
    return x86_result(sel, ins);
}

static bool x86_scalar_memory(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    size_t bytes;
    bool sign = false, is_bool = false, load, floating, fp_double = false;
    MinicCoreValueId address, payload;
    MinicMachineOperand memory;
    MinicX86MachineOpcode opcode;
    bool vol;
    load = ins->kind == MINIC_CORE_INSTRUCTION_LOAD;
    address = load ? ins->value.load.address : ins->value.store.address;
    payload = load ? ins->result : ins->value.store.stored_value;
    vol = load ? ins->value.load.is_volatile : ins->value.store.is_volatile;
    if (address >= sel->core->value_count ||
        payload >= sel->core->value_count) return false;
    floating = x86_float_shape(sel, load ? ins->core_type_id :
        sel->core->values[payload].core_type_id, &fp_double);
    if (
        (!floating && !x86_scalar_shape(sel, load ? ins->core_type_id :
            sel->core->values[payload].core_type_id,
            &bytes, &sign, &is_bool)) ||
        !x86_core_load(sel, address, MINIC_X86_REG_R11)) return false;
    if (floating) bytes = fp_double ? 8U : 4U;
    memory = x86_addr(MINIC_X86_REG_R11, 0);
    if (load) {
        switch (bytes) {
        case 1U: opcode = sign ? MINIC_X86_MI_MOVSBQ_AR :
            MINIC_X86_MI_MOVZBQ_AR; break;
        case 2U: opcode = sign ? MINIC_X86_MI_MOVSWQ_AR :
            MINIC_X86_MI_MOVZWQ_AR; break;
        case 4U: opcode = sign ? MINIC_X86_MI_MOVSLQ_AR :
            MINIC_X86_MI_MOV32_AR; break;
        case 8U: opcode = MINIC_X86_MI_MOV64_AR; break;
        default: return false;
        }
        if (!x86_2_volatile(sel, opcode, memory,
            x86_reg(MINIC_X86_REG_RAX,
                bytes == 4U && !sign ? 32U : 64U), vol)) return false;
        return floating ? x86_core_store(sel, ins->result,
            MINIC_X86_REG_RAX) : x86_result(sel, ins);
    }
    if (!x86_core_load(sel, payload, MINIC_X86_REG_RAX)) return false;
    switch (bytes) {
    case 1U: opcode = MINIC_X86_MI_MOV8_RA; break;
    case 2U: opcode = MINIC_X86_MI_MOV16_RA; break;
    case 4U: opcode = MINIC_X86_MI_MOV32_RA; break;
    case 8U: opcode = MINIC_X86_MI_MOV64_RA; break;
    default: return false;
    }
    return x86_2_volatile(sel, opcode,
        x86_reg(MINIC_X86_REG_RAX, (unsigned int)(bytes * 8U)),
        memory, vol);
}

/* Formal SSA values are materialized exactly once, at their Core PARAMETER
 * definition. Read the finalized target call plan; never invent a location
 * from the Core parameter index or infer the calling convention from ISA. */
static bool x86_parameter(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int sysv_gp[6] = {
        MINIC_X86_REG_RDI, MINIC_X86_REG_RSI, MINIC_X86_REG_RDX,
        MINIC_X86_REG_RCX, MINIC_X86_REG_R8, MINIC_X86_REG_R9};
    static const unsigned int ms_gp[4] = {
        MINIC_X86_REG_RCX, MINIC_X86_REG_RDX,
        MINIC_X86_REG_R8, MINIC_X86_REG_R9};
    const MinicX86ArgumentPlan *plan = &sel->frame->incoming_arguments;
    const MinicX86ArgLocation *where;
    const unsigned int *regs;
    size_t gp_count, bytes, offset;
    bool sign = false, is_bool = false, ms, float_double = false, floating;
    const size_t position = ins->value.parameter_index;
    floating = x86_float_shape(sel, ins->core_type_id, &float_double);
    if (position >= plan->argument_count ||
        plan->locations == NULL ||
        (!floating && !x86_scalar_shape(sel, ins->core_type_id,
            &bytes, &sign, &is_bool)) ||
        (plan->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT &&
         plan->calling_convention != MINIC_CALLING_CONVENTION_MS_X64) ||
        plan->calling_convention !=
            minic_core_function_calling_convention(sel->core)) return false;
    where = &plan->locations[position];
    if (where->kind != (floating ? MINIC_X86_ARG_SSE :
            MINIC_X86_ARG_INTEGER)) return false;
    ms = plan->calling_convention == MINIC_CALLING_CONVENTION_MS_X64;
    regs = ms ? ms_gp : sysv_gp;
    gp_count = ms ? 4U : 6U;
    if (floating && !where->on_stack) {
        if (where->fp_index >= 8U ||
            (ms && where->fp_index >= 4U)) return false;
        return x86_float_store_xmm(sel, ins->result,
            (unsigned int)where->fp_index, float_double);
    }
    if (where->on_stack) {
        if (where->stack_index > (INT32_MAX - 16U) / 8U)
            return false;
        offset = 16U + where->stack_index * 8U;
        if (!x86_2(sel, MINIC_X86_MI_MOV64_AR,
                x86_addr(MINIC_X86_REG_RBP, (int64_t)offset),
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
    } else {
        if (where->gp_index >= gp_count) return false;
        if (sel->frame->saves_incoming_gp) {
            MinicMachineOperand save;
            if (where->gp_index > (SIZE_MAX -
                    sel->frame->incoming_gp_save_offset) / 8U)
                return false;
            offset = sel->frame->incoming_gp_save_offset +
                where->gp_index * 8U;
            if (offset < 8U || offset > sel->frame->frame_size ||
                !minic_x86_64_machine_frame_slot(&sel->builder,
                    MINIC_MACHINE_FRAME_BASE_FRAME_POINTER,
                    MINIC_MACHINE_FRAME_LOCAL, 8U, 1U,
                    -(int64_t)offset, &save) ||
                !x86_2(sel, MINIC_X86_MI_MOV64_FRBP_R, save,
                    x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        } else if (!x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                regs[where->gp_index], MINIC_X86_REG_RAX)) return false;
    }
    return floating ? x86_core_store(sel, ins->result,
        MINIC_X86_REG_RAX) : x86_result(sel, ins);
}

/* The complete zero-argument SysV direct-call ABI case is useful for early
 * Core functions (and simple Linux helpers). Any argument plan, hidden sret,
 * non-default calling convention or extended-float helper is rejected until
 * target-owned call lowering can model it without a legacy instruction. */
/* Call plans are the target ABI's single classification authority. A Core
 * call with one or more arguments must have a sealed site-specific plan:
 * MachineIR cannot quietly re-classify a record or reinterpret a parameter
 * after the original call planning pass. The only standalone exception is
 * the trivial zero-argument SysV call, which has no argument locations. */
static const MinicX86ArgumentPlan *x86_call_plan(
    const X86CoreSelector *sel, size_t instruction_id) {
    const MinicX86OutgoingCallPlans *plans =
        &sel->frame->outgoing_call_plans;
    size_t i;
    if (plans->count != 0U && plans->sites == NULL) return NULL;
    for (i = 0U; i < plans->count; ++i)
        if (plans->sites[i].instruction_id == instruction_id)
            return &plans->sites[i].plan;
    return NULL;
}

static bool x86_call_argument(X86CoreSelector *sel,
    const MinicCoreCallArgument *arg, MinicCoreTypeId declared_type,
    const MinicX86ArgLocation *where, size_t stack_bytes,
    MinicFunctionCallingConvention convention) {
    static const unsigned int sysv_gp[6] = {
        MINIC_X86_REG_RDI, MINIC_X86_REG_RSI, MINIC_X86_REG_RDX,
        MINIC_X86_REG_RCX, MINIC_X86_REG_R8, MINIC_X86_REG_R9};
    static const unsigned int ms_gp[4] = {
        MINIC_X86_REG_RCX, MINIC_X86_REG_RDX,
        MINIC_X86_REG_R8, MINIC_X86_REG_R9};
    const bool ms = convention == MINIC_CALLING_CONVENTION_MS_X64;
    const MinicCoreType *actual;
    size_t bytes, stack_offset;
    bool sign, is_bool, fp_double, floating;
    if (arg == NULL || where == NULL ||
        arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
        arg->value.value_id >= sel->core->value_count ||
        (where->kind != MINIC_X86_ARG_INTEGER &&
         where->kind != MINIC_X86_ARG_SSE) ||
        declared_type == MINIC_CORE_TYPE_INVALID) return false;
    actual = x86_type(sel,
        sel->core->values[arg->value.value_id].core_type_id);
    floating = x86_float_shape(sel, declared_type, &fp_double);
    if (actual == NULL ||
        (floating ?
            !x86_float_shape(sel,
                sel->core->values[arg->value.value_id].core_type_id,
                &is_bool) || is_bool != fp_double ||
                where->kind != MINIC_X86_ARG_SSE :
            !x86_scalar_shape(sel, declared_type, &bytes,
                &sign, &is_bool) ||
                !x86_scalar_shape(sel,
                    sel->core->values[arg->value.value_id].core_type_id,
                    &stack_offset, &sign, &is_bool) ||
                bytes != stack_offset ||
                where->kind != MINIC_X86_ARG_INTEGER)) return false;
    if (where->on_stack) {
        if (where->stack_index > SIZE_MAX / 8U ||
            (stack_offset = where->stack_index * 8U) > INT32_MAX ||
            stack_offset > stack_bytes ||
            stack_bytes - stack_offset < 8U ||
            !x86_core_load(sel, arg->value.value_id, MINIC_X86_REG_RAX))
            return false;
        return x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RAX, 64U),
            x86_addr(MINIC_X86_REG_RSP, (int64_t)stack_offset));
    }
    if (floating) {
        if (where->fp_index >= (ms ? 4U : 8U)) return false;
        return x86_float_load_xmm(sel, arg->value.value_id,
            (unsigned int)where->fp_index, MINIC_X86_REG_RAX, fp_double);
    }
    if (where->gp_index >= (ms ? 4U : 6U) ||
        !x86_core_load(sel, arg->value.value_id, MINIC_X86_REG_RAX))
        return false;
    return x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
        MINIC_X86_REG_RAX,
        (ms ? ms_gp : sysv_gp)[where->gp_index]);
}

static bool x86_scalar_call(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    const size_t begin = indirect ? ins->value.indirect_call.argument_begin :
        ins->value.call.argument_begin;
    const size_t count = indirect ? ins->value.indirect_call.argument_count :
        ins->value.call.argument_count;
    const MinicCoreObjectId result_object = indirect ?
        ins->value.indirect_call.result_object : ins->value.call.result_object;
    const MinicCoreType *signature, *result_type;
    const MinicX86ArgumentPlan *plan = x86_call_plan(sel, instruction_id);
    MinicX86ArgumentPlan empty = {0};
    MinicCoreTypeId function_type;
    const char *symbol = NULL;
    size_t i;
    bool fp_double;
    if (result_object != MINIC_CORE_OBJECT_INVALID ||
        begin > sel->core->call_argument_count ||
        count > sel->core->call_argument_count - begin ||
        (count != 0U && sel->core->call_arguments == NULL) ||
        (sel->frame->frame_size & 15U) != 0U ||
        (!indirect && ins->value.call.extended_float_operation !=
            MINIC_CORE_EXT_FLOAT_NONE)) return false;
    if (indirect) {
        const MinicCoreCallSignatureId id =
            ins->value.indirect_call.signature_id;
        if (id >= sel->core->call_signature_count ||
            sel->core->call_signatures == NULL ||
            ins->value.indirect_call.callee >= sel->core->value_count)
            return false;
        function_type = sel->core->call_signatures[id].core_function_type_id;
    } else {
        const MinicCoreCalleeId id = ins->value.call.callee_id;
        if (id >= sel->core->callee_count || sel->core->callees == NULL)
            return false;
        symbol = sel->core->callees[id].name;
        if (!minic_core_inline_asm_symbol_valid(symbol)) return false;
        function_type = sel->core->callees[id].core_function_type_id;
    }
    signature = x86_type(sel, function_type);
    result_type = x86_type(sel, ins->core_type_id);
    if (signature == NULL || signature->kind != MINIC_CORE_TYPE_FUNCTION ||
        signature->value.function.return_type != ins->core_type_id ||
        signature->value.function.parameter_count > count ||
        (signature->value.function.parameter_count != 0U &&
         signature->value.function.parameters == NULL) ||
        (!signature->value.function.is_variadic &&
         signature->value.function.parameter_count != count) ||
        result_type == NULL ||
        (result_type->kind != MINIC_CORE_TYPE_VOID &&
         ins->result >= sel->core->value_count) ||
        (result_type->kind == MINIC_CORE_TYPE_VOID &&
         ins->result != MINIC_CORE_VALUE_INVALID)) return false;
    if (plan == NULL) {
        if (count != 0U || indirect ||
            signature->value.function.calling_convention !=
                MINIC_CALLING_CONVENTION_DEFAULT ||
            signature->value.function.is_variadic) return false;
        empty.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
        plan = &empty;
    }
    if (plan->calling_convention !=
            signature->value.function.calling_convention ||
        plan->argument_count != count ||
        plan->is_variadic != signature->value.function.is_variadic ||
        (count != 0U && plan->locations == NULL) ||
        plan->stack_bytes > INT32_MAX ||
        (plan->stack_bytes & 15U) != 0U ||
        (plan->calling_convention == MINIC_CALLING_CONVENTION_MS_X64 &&
         (plan->stack_bytes < 32U || plan->is_variadic))) return false;
    if (result_type->kind != MINIC_CORE_TYPE_VOID &&
        !x86_float_shape(sel, ins->core_type_id, &fp_double)) {
        size_t bytes;
        bool sign, is_bool;
        if (!x86_scalar_shape(sel, ins->core_type_id,
                &bytes, &sign, &is_bool)) return false;
    }
    if (plan->stack_bytes != 0U &&
        !x86_ir(sel, MINIC_X86_MI_SUB64_IR,
            (int64_t)plan->stack_bytes, 64U, MINIC_X86_REG_RSP))
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreCallArgument *arg =
            &sel->core->call_arguments[begin + i];
        const MinicCoreTypeId declared = i <
            signature->value.function.parameter_count ?
            signature->value.function.parameters[i] :
            arg->kind == MINIC_CORE_CALL_ARGUMENT_VALUE &&
            arg->value.value_id < sel->core->value_count ?
            sel->core->values[arg->value.value_id].core_type_id :
            MINIC_CORE_TYPE_INVALID;
        if (!x86_call_argument(sel,
                arg, declared,
                &plan->locations[i], plan->stack_bytes,
                plan->calling_convention)) return false;
    }
    if (plan->is_variadic &&
        (plan->end.fp_used > 8U ||
         !x86_ir(sel, MINIC_X86_MI_MOV8_IR,
             (int64_t)plan->end.fp_used, 8U, MINIC_X86_REG_RAX)))
        return false;
    if (indirect) {
        if ((minic_x86_64_target_codegen_options(sel->target) &
             (MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK |
              MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK)) != 0U ||
            !x86_core_load(sel, ins->value.indirect_call.callee,
                MINIC_X86_REG_R11) ||
            !x86_1(sel, plan->calling_convention ==
                MINIC_CALLING_CONVENTION_MS_X64 ?
                    MINIC_X86_MI_CALL_MS_R : MINIC_X86_MI_CALL_SYSV_R,
                x86_reg(MINIC_X86_REG_R11, 64U))) return false;
    } else if (!x86_1(sel,
            plan->calling_convention == MINIC_CALLING_CONVENTION_MS_X64 ?
                MINIC_X86_MI_CALL_MS_SYMBOL : MINIC_X86_MI_CALL_SYSV_SYMBOL,
            x86_symbol(symbol, MINIC_X86_MACHINE_RELOC_PLT32))) return false;
    if (plan->stack_bytes != 0U &&
        !x86_ir(sel, MINIC_X86_MI_ADD64_IR,
            (int64_t)plan->stack_bytes, 64U, MINIC_X86_REG_RSP))
        return false;
    if (result_type->kind == MINIC_CORE_TYPE_VOID) return true;
    if (x86_float_shape(sel, ins->core_type_id, &fp_double))
        return x86_float_store_xmm(sel, ins->result, 0U, fp_double);
    return x86_result(sel, ins);
}

static bool x86_core_instruction(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    size_t bytes;
    bool sign, is_bool;
    if (sel == NULL || ins == NULL) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return x86_parameter(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL:
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return x86_scalar_call(sel, ins, instruction_id);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return x86_scalar_shape(sel, ins->core_type_id, &bytes,
                &sign, &is_bool) &&
            x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                ins->value.integer_value, 64U, MINIC_X86_REG_RAX) &&
            x86_result(sel, ins);
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        return x86_float_shape(sel, ins->core_type_id, &is_bool) &&
            x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                (int64_t)ins->value.floating_bits,
                64U, MINIC_X86_REG_RAX) &&
            x86_core_store(sel, ins->result, MINIC_X86_REG_RAX);
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        return x86_float_binary(sel, ins);
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
        return x86_integer_binary(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return x86_unary(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return ins->value.operand < sel->core->value_count &&
            x86_scalar_shape(sel, ins->core_type_id,
                &bytes, &sign, &is_bool) &&
            x86_core_load(sel, ins->value.operand, MINIC_X86_REG_RAX) &&
            x86_result(sel, ins);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
        MinicMachineOperand slot;
        MinicCoreObjectId object = ins->value.object_id;
        size_t offset;
        bool aligned;
        if (object >= sel->frame->object_count ||
            sel->frame->object_offsets == NULL) return false;
        offset = sel->frame->object_offsets[object];
        aligned = sel->frame->objects_from_aligned_rsp;
        if (offset > INT32_MAX ||
            !minic_x86_64_machine_frame_slot(&sel->builder,
                aligned ? MINIC_MACHINE_FRAME_BASE_STACK_POINTER :
                    MINIC_MACHINE_FRAME_BASE_FRAME_POINTER,
                MINIC_MACHINE_FRAME_LOCAL, 1U, 1U,
                aligned ? (int64_t)offset : -(int64_t)offset,
                &slot) ||
            !x86_2(sel, aligned ? MINIC_X86_MI_LEA64_FRSP_R :
                MINIC_X86_MI_LEA64_FRBP_R,
                slot, x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        return x86_result(sel, ins);
    }
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
        size_t displacement = ins->value.field_address.byte_offset;
        if (ins->value.field_address.base >= sel->core->value_count ||
            !x86_core_load(sel, ins->value.field_address.base,
                MINIC_X86_REG_RAX)) return false;
        if (displacement <= INT32_MAX) {
            if (!x86_ir(sel, MINIC_X86_MI_ADD64_IR,
                    (int64_t)displacement, 64U, MINIC_X86_REG_RAX))
                return false;
        } else if (!x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                (int64_t)displacement, 64U, MINIC_X86_REG_R10) ||
            !x86_rr(sel, MINIC_X86_MI_ADD64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX)) return false;
        return x86_result(sel, ins);
    }
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
        const size_t scale = ins->value.pointer_offset.element_size;
        if (ins->value.pointer_offset.base >= sel->core->value_count ||
            ins->value.pointer_offset.index >= sel->core->value_count ||
            scale == 0U || scale > INT64_MAX ||
            !x86_core_load(sel, ins->value.pointer_offset.base,
                MINIC_X86_REG_RAX) ||
            !x86_core_load(sel, ins->value.pointer_offset.index,
                MINIC_X86_REG_R10) ||
            !x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                (int64_t)scale, 64U, MINIC_X86_REG_R11) ||
            !x86_rr(sel, MINIC_X86_MI_IMUL64_RR, 64U,
                MINIC_X86_REG_R11, MINIC_X86_REG_R10) ||
            !x86_rr(sel, ins->value.pointer_offset.subtract ?
                MINIC_X86_MI_SUB64_RR : MINIC_X86_MI_ADD64_RR,
                64U, MINIC_X86_REG_R10, MINIC_X86_REG_RAX))
            return false;
        return x86_result(sel, ins);
    }
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
        return x86_scalar_memory(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        if (ins->value.call_frame_address.level != 0U ||
            !x86_scalar_shape(sel, ins->core_type_id,
                &bytes, &sign, &is_bool)) return false;
        if (ins->value.call_frame_address.kind ==
                MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) {
            if (!x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                    MINIC_X86_REG_RBP, MINIC_X86_REG_RAX)) return false;
        } else if (ins->value.call_frame_address.kind ==
                MINIC_CORE_CALL_FRAME_ADDRESS_RETURN) {
            if (!x86_2(sel, MINIC_X86_MI_MOV64_AR,
                x86_addr(MINIC_X86_REG_RBP, 8),
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        } else return false;
        return x86_result(sel, ins);
    default: return false;
    }
}

static bool x86_jump(X86CoreSelector *sel, size_t machine_target) {
    return machine_target != SIZE_MAX &&
        x86_1(sel, MINIC_X86_MI_JMP_B, x86_block(machine_target)) &&
        minic_machine_builder_add_successor(&sel->builder, machine_target);
}

static bool x86_core_terminator(X86CoreSelector *sel, size_t core_id) {
    const MinicCoreTerminator *term = &sel->core->blocks[core_id].terminator;
    size_t i;
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_BRANCH:
        return term->branch_target < sel->core->block_count &&
            x86_jump(sel, sel->blocks[term->branch_target]);
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        if (term->conditional.condition >= sel->core->value_count ||
            term->conditional.when_true >= sel->core->block_count ||
            term->conditional.when_false >= sel->core->block_count ||
            sel->false_blocks[core_id] == SIZE_MAX ||
            !x86_core_load(sel, term->conditional.condition,
                MINIC_X86_REG_RAX) ||
            !x86_rr(sel, MINIC_X86_MI_TEST64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RAX) ||
            !x86_1(sel, MINIC_X86_MI_JNZ_B,
                x86_block(sel->blocks[term->conditional.when_true])) ||
            !minic_machine_builder_add_successor(&sel->builder,
                sel->blocks[term->conditional.when_true]) ||
            !minic_machine_builder_add_successor(&sel->builder,
                sel->false_blocks[core_id]) ||
            !minic_machine_builder_set_fallthrough(&sel->builder,
                sel->false_blocks[core_id]) ||
            !minic_machine_builder_position_at(&sel->builder,
                sel->false_blocks[core_id])) return false;
        return x86_jump(sel, sel->blocks[term->conditional.when_false]);
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        if (term->indirect_target >= sel->core->value_count ||
            (minic_x86_64_target_codegen_options(sel->target) &
             (MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK |
              MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK)) != 0U ||
            !x86_core_load(sel, term->indirect_target,
                MINIC_X86_REG_RAX) ||
            !x86_1(sel, MINIC_X86_MI_JMP_R,
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        for (i = 0U; i < sel->core->block_count; ++i)
            if (sel->reachable[i] && sel->core->blocks[i].indirect_entry &&
                !minic_machine_builder_add_successor(&sel->builder,
                    sel->blocks[i])) return false;
        return true;
    case MINIC_CORE_TERMINATOR_RETURN: {
        const MinicCoreTypeId id =
            minic_core_function_return_type(sel->core);
        const MinicCoreType *type = x86_type(sel, id);
        size_t bytes;
        bool sign, is_bool;
        if (type == NULL) return false;
        if (type->kind == MINIC_CORE_TYPE_FLOAT) {
            bool fp_double;
            if (term->return_value >= sel->core->value_count ||
                !x86_float_shape(sel, id, &fp_double) ||
                !x86_float_load_xmm(sel, term->return_value,
                    0U, MINIC_X86_REG_RAX, fp_double)) return false;
        } else if (type->kind != MINIC_CORE_TYPE_VOID &&
            (term->return_value >= sel->core->value_count ||
             !x86_scalar_shape(sel, id, &bytes, &sign, &is_bool) ||
             !x86_core_load(sel, term->return_value,
                 MINIC_X86_REG_RAX))) return false;
        return minic_x86_64_machine_select_epilogue(
            &sel->builder, &sel->frame_view);
    }
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return x86_0(sel, MINIC_X86_MI_UD2);
    default: return false;
    }
}

bool minic_x86_64_machine_select_core_subset(
    MinicMachineFunction *machine, const MinicTargetInfo *target,
    const MinicCoreFunction *core, const MinicX86Frame *frame) {
    X86CoreSelector sel = {0};
    MinicX86MachinePrologue prologue = {0};
    size_t i, count;
    bool ok = false;
    if (machine == NULL || target == NULL || core == NULL || frame == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        core->name == NULL || core->type_context == NULL ||
        core->block_count == 0U || core->blocks == NULL ||
        core->entry_block >= core->block_count ||
        frame->value_count != core->value_count ||
        frame->object_count != core->object_count ||
        frame->value_offsets == NULL ||
        frame->frame_size > INT32_MAX) return false;
    count = core->block_count;
    if (count > SIZE_MAX / sizeof(*sel.blocks)) return false;
    sel.core = core;
    sel.frame = frame;
    sel.target = target;
    sel.blocks = malloc(count * sizeof(*sel.blocks));
    sel.false_blocks = malloc(count * sizeof(*sel.false_blocks));
    sel.reachable = calloc(count, sizeof(*sel.reachable));
    if (sel.blocks == NULL || sel.false_blocks == NULL ||
        sel.reachable == NULL ||
        !minic_core_function_mark_reachable_blocks(core,
            sel.reachable, count) ||
        !sel.reachable[core->entry_block] ||
        !minic_x86_64_machine_gnu_asm_policy(target, &sel.policy) ||
        !minic_x86_64_machine_frame_view(frame,
            minic_core_function_calling_convention(core), &sel.frame_view) ||
        !minic_machine_function_initialize(machine, target, core->name,
            minic_x86_64_machine_generated_features(target))) goto cleanup;
    machine->calling_convention =
        minic_core_function_calling_convention(core);
    machine->frame_size = frame->frame_size;
    machine->frame_alignment = 16U;
    /* No dynamically based local address is silently claimed stable while
     * the stack changes: dynamic alloca is rejected by this selector. */
    machine->has_dynamic_stack = false;
    for (i = 0U; i < count; ++i) {
        sel.blocks[i] = SIZE_MAX;
        sel.false_blocks[i] = SIZE_MAX;
    }
    if (!minic_machine_builder_initialize(&sel.builder, machine) ||
        !minic_x86_64_machine_select_prologue(&sel.builder,
            &sel.frame_view, &prologue)) goto cleanup;
    for (i = 0U; i < count; ++i) {
        const MinicCoreBlock *block = &core->blocks[i];
        size_t machine_id;
        if (!sel.reachable[i]) continue;
        if (!minic_machine_builder_create_block(&sel.builder,
                block->local_label_symbol, block->indirect_entry,
                false, &machine_id) ||
            !minic_machine_builder_mark_core_block(&sel.builder, i))
            goto cleanup;
        sel.blocks[i] = machine_id;
        if (block->has_terminator &&
            block->terminator.kind ==
                MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH) {
            if (!minic_machine_builder_create_block(&sel.builder,
                    NULL, false, true, &machine_id)) goto cleanup;
            sel.false_blocks[i] = machine_id;
        }
    }
    if (!minic_machine_builder_position_at(&sel.builder,
            prologue.continuation_block) ||
        !x86_jump(&sel, sel.blocks[core->entry_block])) goto cleanup;
    for (i = 0U; i < count; ++i) {
        const MinicCoreBlock *block = &core->blocks[i];
        size_t j;
        if (!sel.reachable[i]) continue;
        if (!block->has_terminator ||
            !minic_machine_builder_position_at(&sel.builder,
                sel.blocks[i])) goto cleanup;
        for (j = 0U; j < block->instruction_count; ++j) {
            MinicCoreInstructionId id;
            const MinicCoreInstruction *ins;
            if (!minic_core_block_instruction_at(core, block, j,
                    &id, &ins)) goto cleanup;
            minic_machine_builder_set_origin(&sel.builder, i, id);
            if (!x86_core_instruction(&sel, ins, id)) goto cleanup;
        }
        minic_machine_builder_clear_origin(&sel.builder);
        if (!x86_core_terminator(&sel, i)) goto cleanup;
    }
    ok = minic_machine_gnu_asm_preflight(machine, &sel.policy);
cleanup:
    free(sel.blocks);
    free(sel.false_blocks);
    free(sel.reachable);
    if (!ok) minic_machine_function_destroy(machine);
    return ok;
}
