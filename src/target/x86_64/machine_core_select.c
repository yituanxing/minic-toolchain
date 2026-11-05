#include "target/x86_64/machine_core_select.h"

#include "target/x86_64/abi.h"
#include "target/x86_64/codegen_options.h"
#include "target/x86_64/machine_frame_select.h"
#include "target/x86_64/machine_opcode.h"
#include "target/x86_64/machine_target_state.h"
#include "target/common/machine_builder.h"
#include "target/common/core_block_walk.h"
#include "target/common/machine_gnu_asm.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/elf_module_symbol_info.h"
#include "target/common/lp64_scalar.h"
#include "core/symbol_identity.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The physical scratch convention deliberately matches the original native
 * x86 backend. Every Core value still lives in its finalized RBP frame slot;
 * this is instruction selection, not register allocation or optimization. */
typedef struct X86AuxBlocks {
    size_t first;
    size_t second;
    size_t third;
} X86AuxBlocks;

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
    X86AuxBlocks *instruction_aux;
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

static MinicMachineOperand x86_indexed_addr(unsigned int base,
    unsigned int index, unsigned int scale) {
    MinicMachineOperand op = x86_addr(base, 0);
    op.value.address.has_index = true;
    op.value.address.index = (MinicMachineRegister){
        index, 64U, MINIC_MACHINE_REGISTER_GENERAL};
    op.value.address.scale = scale;
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

/* Reuse the sealed x86 frame's object geometry. These are physical addresses
 * consumed by existing Core record transfers, never a new object layout. */
static bool x86_object_address(X86CoreSelector *sel,
    MinicCoreObjectId object, unsigned int reg) {
    MinicMachineOperand slot;
    size_t offset;
    const bool rsp_based = sel->frame->objects_from_aligned_rsp;
    if (object >= sel->frame->object_count ||
        sel->frame->object_offsets == NULL) return false;
    offset = sel->frame->object_offsets[object];
    if (offset > INT32_MAX ||
        !minic_x86_64_machine_frame_slot(&sel->builder,
            rsp_based ? MINIC_MACHINE_FRAME_BASE_STACK_POINTER :
                MINIC_MACHINE_FRAME_BASE_FRAME_POINTER,
            MINIC_MACHINE_FRAME_LOCAL, 1U, 1U,
            rsp_based ? (int64_t)offset : -(int64_t)offset, &slot))
        return false;
    return x86_2(sel, rsp_based ? MINIC_X86_MI_LEA64_FRSP_R :
        MINIC_X86_MI_LEA64_FRBP_R, slot, x86_reg(reg, 64U));
}

/* A memory address that came from head's integer-link-VMA cast requires
 * special physical rebasing in the legacy backend. Until the module-symbol
 * view is wired into typed selection, reject precisely that case. */
static bool x86_record_pointer(X86CoreSelector *sel,
    MinicCoreValueId value, unsigned int reg) {
    if (value >= sel->core->value_count ||
        (sel->frame->kernel_head_link_addresses &&
         sel->frame->kernel_head_memory_rebase_globals != NULL &&
         sel->frame->kernel_head_memory_rebase_globals[value] !=
             MINIC_CORE_GLOBAL_INVALID)) return false;
    return x86_core_load(sel, value, reg);
}

/* This byte transporter is migrated from the old x86_emit_record_move_bytes:
 * <=128 bytes use exact unrolled memory accesses; larger objects preserve
 * the SysV outgoing argument registers while using CLD/REP MOVSB. Core owns
 * the extent; this routine owns only the x86 physical byte traffic. */
static bool x86_record_move_bytes(X86CoreSelector *sel,
    unsigned int src_base, size_t src_offset,
    unsigned int dst_base, size_t dst_offset,
    size_t bytes, bool bytewise, bool volatile_source,
    bool volatile_destination) {
    size_t i = 0U;
    if (src_offset > SIZE_MAX - bytes ||
        dst_offset > SIZE_MAX - bytes ||
        src_offset > INT32_MAX || dst_offset > INT32_MAX ||
        bytes > (16U * 1024U * 1024U)) return false;
    if (bytes == 0U) return true;
    if (bytes <= 128U) {
        if (!bytewise) {
            for (; i + 8U <= bytes; i += 8U) {
                if (!x86_2_volatile(sel, MINIC_X86_MI_MOV64_AR,
                        x86_addr(src_base, (int64_t)(src_offset + i)),
                        x86_reg(MINIC_X86_REG_RAX, 64U),
                        volatile_source) ||
                    !x86_2_volatile(sel, MINIC_X86_MI_MOV64_RA,
                        x86_reg(MINIC_X86_REG_RAX, 64U),
                        x86_addr(dst_base, (int64_t)(dst_offset + i)),
                        volatile_destination)) return false;
            }
        }
        for (; i < bytes; ++i)
            if (!x86_2_volatile(sel, MINIC_X86_MI_MOVZBQ_AR,
                    x86_addr(src_base, (int64_t)(src_offset + i)),
                    x86_reg(MINIC_X86_REG_RAX, 64U),
                    volatile_source) ||
                !x86_2_volatile(sel, MINIC_X86_MI_MOV8_RA,
                    x86_reg(MINIC_X86_REG_RAX, 8U),
                    x86_addr(dst_base, (int64_t)(dst_offset + i)),
                    volatile_destination)) return false;
        return true;
    }
    if (src_offset > INT32_MAX - bytes ||
        dst_offset > INT32_MAX - bytes) return false;
    return x86_1(sel, MINIC_X86_MI_PUSH64_R,
            x86_reg(MINIC_X86_REG_RDI, 64U)) &&
        x86_1(sel, MINIC_X86_MI_PUSH64_R,
            x86_reg(MINIC_X86_REG_RSI, 64U)) &&
        x86_1(sel, MINIC_X86_MI_PUSH64_R,
            x86_reg(MINIC_X86_REG_RCX, 64U)) &&
        x86_2(sel, MINIC_X86_MI_LEA64_AR,
            x86_addr(src_base, (int64_t)src_offset),
            x86_reg(MINIC_X86_REG_RSI, 64U)) &&
        x86_2(sel, MINIC_X86_MI_LEA64_AR,
            x86_addr(dst_base, (int64_t)dst_offset),
            x86_reg(MINIC_X86_REG_RDI, 64U)) &&
        x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
            (int64_t)bytes, 64U, MINIC_X86_REG_RCX) &&
        x86_0(sel, MINIC_X86_MI_CLD) &&
        x86_emit(sel, MINIC_X86_MI_REP_MOVSB, NULL, 0U,
            volatile_source || volatile_destination ?
                MINIC_MACHINE_EFFECT_VOLATILE : 0U) &&
        x86_1(sel, MINIC_X86_MI_POP64_R,
            x86_reg(MINIC_X86_REG_RCX, 64U)) &&
        x86_1(sel, MINIC_X86_MI_POP64_R,
            x86_reg(MINIC_X86_REG_RSI, 64U)) &&
        x86_1(sel, MINIC_X86_MI_POP64_R,
            x86_reg(MINIC_X86_REG_RDI, 64U));
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
        type == NULL || !type->has_layout ||
        !minic_x86_64_core_scalar_width(sel->core->type_context, id,
            bytes, is_signed) || *bytes == 0U || *bytes > 8U ||
        (*bytes != 1U && *bytes != 2U &&
         *bytes != 4U && *bytes != 8U)) return false;
    *is_bool = type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->value.integer.is_bool;
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

/* Physical register names remain owned by the established x86 ABI bank.
 * The selector only resolves those names to MachineIR register numbers. */
static bool x86_abi_gp_register(MinicFunctionCallingConvention convention,
    size_t index, unsigned int *reg) {
    static const struct { const char *name; unsigned int number; } names[] = {
        {"%rdi", MINIC_X86_REG_RDI}, {"%rsi", MINIC_X86_REG_RSI},
        {"%rdx", MINIC_X86_REG_RDX}, {"%rcx", MINIC_X86_REG_RCX},
        {"%r8", MINIC_X86_REG_R8}, {"%r9", MINIC_X86_REG_R9}
    };
    const MinicX86AbiRegisters *abi =
        minic_x86_64_abi_registers(convention);
    size_t i;
    if (abi == NULL || index >= abi->gp_argument_count ||
        reg == NULL) return false;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(abi->gp_arguments[index], names[i].name) == 0) {
            *reg = names[i].number;
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
        !x86_record_pointer(sel, address, MINIC_X86_REG_R11)) return false;
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
    const MinicX86ArgumentPlan *plan = &sel->frame->incoming_arguments;
    const MinicX86ArgLocation *where;
    unsigned int physical_gp;
    size_t bytes, offset;
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
        if (!x86_abi_gp_register(plan->calling_convention,
                where->gp_index, &physical_gp)) return false;
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
                physical_gp, MINIC_X86_REG_RAX)) return false;
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
    const bool ms = convention == MINIC_CALLING_CONVENTION_MS_X64;
    const MinicCoreType *actual;
    unsigned int physical_gp;
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
    if (!x86_abi_gp_register(convention, where->gp_index,
            &physical_gp) ||
        !x86_core_load(sel, arg->value.value_id, MINIC_X86_REG_RAX))
        return false;
    return x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
        MINIC_X86_REG_RAX, physical_gp);
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

/* The old x86_one_gp_store writes exactly 1..8 ABI payload bytes, including
 * packed odd tails. CL is preserved because a later Core PARAMETER_OBJECT
 * may still consume the incoming SysV RCX argument. */
static bool x86_store_record_gp(X86CoreSelector *sel,
    size_t bytes, size_t offset) {
    MinicX86MachineOpcode opcode;
    if (bytes == 0U || bytes > 8U || offset > INT32_MAX - bytes)
        return false;
    switch (bytes) {
    case 1U: opcode = MINIC_X86_MI_MOV8_RA; break;
    case 2U: opcode = MINIC_X86_MI_MOV16_RA; break;
    case 4U: opcode = MINIC_X86_MI_MOV32_RA; break;
    case 8U: opcode = MINIC_X86_MI_MOV64_RA; break;
    default: opcode = MINIC_X86_MI_INVALID; break;
    }
    if (opcode != MINIC_X86_MI_INVALID)
        return x86_2(sel, opcode,
            x86_reg(MINIC_X86_REG_RAX, (unsigned int)(bytes * 8U)),
            x86_addr(MINIC_X86_REG_R10, (int64_t)offset));
    if (!x86_1(sel, MINIC_X86_MI_PUSH64_R,
            x86_reg(MINIC_X86_REG_RCX, 64U)) ||
        !x86_ir(sel, MINIC_X86_MI_MOV8_IR, 8, 8U,
            MINIC_X86_REG_RCX)) return false;
    for (size_t i = 0U; i < bytes; ++i) {
        if (!x86_2(sel, MINIC_X86_MI_MOV8_RA,
                x86_reg(MINIC_X86_REG_RAX, 8U),
                x86_addr(MINIC_X86_REG_R10, (int64_t)(offset + i))) ||
            (i + 1U < bytes && !x86_1(sel, MINIC_X86_MI_SHR64_CL_R,
                x86_reg(MINIC_X86_REG_RAX, 64U)))) return false;
    }
    return x86_1(sel, MINIC_X86_MI_POP64_R,
        x86_reg(MINIC_X86_REG_RCX, 64U));
}

/* A real SysV aggregate parameter is transported using the exact incoming
 * plan already classified in call_plan.c and ABI compatibility in abi.c. */
static bool x86_parameter_object(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicX86ArgumentPlan *plan = &sel->frame->incoming_arguments;
    const size_t position = ins->value.parameter_object.parameter_index;
    const MinicCoreObjectId object = ins->value.parameter_object.object_id;
    const MinicX86ArgLocation *loc;
    const MinicCoreType *record;
    unsigned int first, second;
    size_t stack_offset, payload;
    if (ins->result != MINIC_CORE_VALUE_INVALID ||
        plan->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT ||
        plan->calling_convention !=
            minic_core_function_calling_convention(sel->core) ||
        position >= plan->argument_count || plan->locations == NULL ||
        position >= minic_core_function_parameter_count(sel->core) ||
        minic_core_function_parameter_types(sel->core) == NULL ||
        object >= sel->core->object_count ||
        sel->core->objects == NULL ||
        sel->frame->object_storage.objects == NULL ||
        !minic_x86_64_core_record_abi_compatible(
            sel->core->type_context,
            sel->core->objects[object].core_type_id,
            minic_core_function_parameter_types(sel->core)[position]) ||
        (record = x86_type(sel,
            sel->core->objects[object].core_type_id)) == NULL ||
        record->kind != MINIC_CORE_TYPE_RECORD || !record->has_layout ||
        record->size > sel->frame->object_storage.objects[object].extent)
        return false;
    loc = &plan->locations[position];
    payload = loc->record_bytes;
    /* Empty GNU records legitimately have no physical write; the current
     * Machine coverage contract cannot mark a zero-byte write as an effect.
     * Reject explicitly rather than fabricate a store. */
    if (loc->kind == MINIC_X86_ARG_RECORD_EMPTY)
        return false;
    if (payload != record->size || payload == 0U ||
        (loc->kind != MINIC_X86_ARG_RECORD_ONE_GP &&
         loc->kind != MINIC_X86_ARG_RECORD_TWO_GP &&
         loc->kind != MINIC_X86_ARG_RECORD_MEMORY) ||
        !x86_object_address(sel, object, MINIC_X86_REG_R10))
        return false;
    if (loc->on_stack) {
        if (loc->stack_index > (INT32_MAX - 16U) / 8U)
            return false;
        stack_offset = 16U + loc->stack_index * 8U;
        if (stack_offset > INT32_MAX - payload) return false;
    } else stack_offset = 0U;
    if (loc->kind == MINIC_X86_ARG_RECORD_MEMORY)
        return loc->on_stack && x86_record_move_bytes(sel,
            MINIC_X86_REG_RBP, stack_offset,
            MINIC_X86_REG_R10, 0U, payload, false, false, false);
    if (loc->kind == MINIC_X86_ARG_RECORD_ONE_GP) {
        if (payload > 8U) return false;
        if (loc->on_stack) {
            if (!x86_2(sel, MINIC_X86_MI_MOV64_AR,
                    x86_addr(MINIC_X86_REG_RBP, (int64_t)stack_offset),
                    x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        } else if (!x86_abi_gp_register(plan->calling_convention,
                loc->gp_index, &first) ||
            !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                first, MINIC_X86_REG_RAX)) return false;
        return x86_store_record_gp(sel, payload, 0U);
    }
    if (payload < 9U || payload > 16U) return false;
    if (loc->on_stack) {
        if (stack_offset > INT32_MAX - 16U ||
            !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                x86_addr(MINIC_X86_REG_RBP, (int64_t)stack_offset),
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
    } else if (!x86_abi_gp_register(plan->calling_convention,
            loc->gp_index, &first) ||
        !x86_abi_gp_register(plan->calling_convention,
            loc->gp_index + 1U, &second) ||
        !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
            first, MINIC_X86_REG_RAX)) return false;
    if (!x86_store_record_gp(sel, 8U, 0U)) return false;
    if (loc->on_stack) {
        if (!x86_2(sel, MINIC_X86_MI_MOV64_AR,
                x86_addr(MINIC_X86_REG_RBP, (int64_t)(stack_offset + 8U)),
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
    } else if (!x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
            second, MINIC_X86_REG_RAX)) return false;
    return x86_store_record_gp(sel, payload - 8U, 8U);
}

static bool x86_record_copy(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    size_t bytes;
    /* A zero-byte Core copy has no physical R/W effects. Existing coverage
     * requires both, so reject until the zero-size coverage rule is sealed;
     * never invent a memory access for an empty GNU record. */
    if (!minic_multi_direct_record_copy_extent(sel->core, ins, &bytes) ||
        bytes == 0U ||
        !x86_record_pointer(sel,
            ins->value.record_copy.destination_address, MINIC_X86_REG_R10) ||
        !x86_record_pointer(sel,
            ins->value.record_copy.source_address, MINIC_X86_REG_R11))
        return false;
    return x86_record_move_bytes(sel, MINIC_X86_REG_R11, 0U,
        MINIC_X86_REG_R10, 0U, bytes, true,
        ins->value.record_copy.source_volatile,
        ins->value.record_copy.destination_volatile);
}

static bool x86_record_load(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    MinicLP64FramePlan plan = {0};
    MinicCoreValueId source;
    size_t offset, bytes;
    const MinicCoreObjectId destination =
        ins->value.record_load.destination_object;
    plan.object_count = sel->frame->object_count;
    plan.object_offsets = sel->frame->object_offsets;
    if (!minic_multi_direct_record_load_extent(sel->core, ins,
            &plan, &source, &offset, &bytes) ||
        sel->frame->object_storage.objects == NULL ||
        bytes > sel->frame->object_storage.objects[destination].extent ||
        !x86_record_pointer(sel, source, MINIC_X86_REG_R11) ||
        !x86_object_address(sel, destination, MINIC_X86_REG_R10))
        return false;
    (void)offset; /* Actual RBP/RSP choice belongs to the x86 frame view. */
    return x86_record_move_bytes(sel, MINIC_X86_REG_R11, 0U,
        MINIC_X86_REG_R10, 0U, bytes, true,
        ins->value.record_load.is_volatile, false);
}

static bool x86_vector_storage_copy(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreType *vector = x86_type(sel, ins->core_type_id);
    MinicCoreTypeId source_type, destination_type;
    const MinicCoreValueId source =
        ins->value.vector_storage_copy.source_address;
    const MinicCoreValueId destination =
        ins->value.vector_storage_copy.destination_address;
    if (ins->result != MINIC_CORE_VALUE_INVALID || vector == NULL ||
        vector->kind != MINIC_CORE_TYPE_VECTOR ||
        vector->value.vector.representation !=
            MINIC_CORE_VECTOR_STORAGE_ONLY ||
        !vector->has_layout || vector->size != 16U ||
        vector->alignment != 16U ||
        source >= sel->core->value_count ||
        destination >= sel->core->value_count ||
        ins->value.vector_storage_copy.source_volatile ||
        ins->value.vector_storage_copy.destination_volatile ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[source].core_type_id, &source_type) ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[destination].core_type_id, &destination_type) ||
        !minic_lp64_core_equal_unqualified(sel->core->type_context,
            source_type, ins->core_type_id) ||
        !minic_lp64_core_equal_unqualified(sel->core->type_context,
            destination_type, ins->core_type_id) ||
        !x86_record_pointer(sel, destination, MINIC_X86_REG_R10) ||
        !x86_record_pointer(sel, source, MINIC_X86_REG_R11)) return false;
    /* Migrate the original two-load/two-store sequence verbatim in
     * physical meaning: overlapping storage snapshots both halves first. */
    return x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_R11, 0),
            x86_reg(MINIC_X86_REG_RAX, 64U)) &&
        x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_R11, 8),
            x86_reg(MINIC_X86_REG_RDX, 64U)) &&
        x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RAX, 64U),
            x86_addr(MINIC_X86_REG_R10, 0)) &&
        x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RDX, 64U),
            x86_addr(MINIC_X86_REG_R10, 8));
}

/* Move precisely the old ELF/PIC policy into the physical selector. The
 * module's finalized symbol facts are a read-only query, never an inferred
 * linkage classification from Core value types or source declarations. */
static bool x86_symbol_address(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const char *name;
    MinicElfModuleSymbolKind kind;
    MinicElfModuleSymbolFacts facts;
    MinicX86MachineOpcode opcode;
    uint32_t relocation;
    const MinicCoreType *type = x86_type(sel, ins->core_type_id);
    if (ins->result >= sel->core->value_count || type == NULL ||
        type->kind != MINIC_CORE_TYPE_POINTER ||
        !type->has_layout || type->size != 8U) return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS) {
        if (ins->value.global_id >= sel->core->global_count ||
            sel->core->globals == NULL) return false;
        name = sel->core->globals[ins->value.global_id].name;
        kind = MINIC_ELF_MODULE_SYMBOL_OBJECT;
        /* The early head exception uses a signed 32-bit absolute `movq
         * $sym,%rax` relocation. ABS64 is not a byte-identical substitute,
         * so fail closed until that physical symbol opcode is migrated. */
        if (sel->frame->kernel_head_link_addresses &&
            (sel->frame->kernel_head_direct_data_globals == NULL ||
             !sel->frame->kernel_head_direct_data_globals[
                 ins->value.global_id])) return false;
    } else if (ins->kind == MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS) {
        if (ins->value.function_symbol_id >=
                sel->core->function_symbol_count ||
            sel->core->function_symbols == NULL) return false;
        name = sel->core->function_symbols[
            ins->value.function_symbol_id].name;
        kind = MINIC_ELF_MODULE_SYMBOL_FUNCTION;
    } else return false;
    if (!minic_core_inline_asm_symbol_valid(name)) return false;
    facts = minic_multi_elf_module_symbol_facts(
        sel->frame->module, name, kind);
    if (!sel->target->position_independent ||
        (facts.nonpreemptible && !facts.undefined_weak)) {
        opcode = MINIC_X86_MI_LEA64_RIP_SYMBOL_R;
        relocation = MINIC_X86_MACHINE_RELOC_PC32;
    } else {
        opcode = MINIC_X86_MI_MOV64_GOTPCREL_R;
        relocation = MINIC_X86_MACHINE_RELOC_GOTPCREL;
    }
    return x86_2(sel, opcode, x86_symbol(name, relocation),
            x86_reg(MINIC_X86_REG_RAX, 64U)) &&
        x86_result(sel, ins);
}

/* These are the same aligned, relaxed scalar MOVs as the proven native
 * backend; ordering levels that need stronger instructions remain rejected.
 * Reuse the existing scalar selector only after Core's atomic pointee is
 * checked through x86 ABI and Common LP64 type metadata. */
static bool x86_atomic_scalar(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const bool load = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
    const MinicCoreValueId address = load ?
        ins->value.atomic_load.address : ins->value.atomic_store.address;
    const MinicCoreValueId payload = load ? ins->result :
        ins->value.atomic_store.stored_value;
    const unsigned int order = load ? ins->value.atomic_load.memory_order :
        ins->value.atomic_store.memory_order;
    MinicCoreTypeId pointee;
    const MinicCoreType *element;
    MinicCoreInstruction mapped = *ins;
    size_t bytes;
    bool sign;
    if (order != 0U || address >= sel->core->value_count ||
        payload >= sel->core->value_count ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[address].core_type_id, &pointee) ||
        (element = x86_type(sel, pointee)) == NULL ||
        (element->kind != MINIC_CORE_TYPE_INTEGER &&
         element->kind != MINIC_CORE_TYPE_POINTER) ||
        !minic_x86_64_core_storage_width(sel->core->type_context,
            pointee, &bytes, &sign) || bytes == 0U || bytes > 8U)
        return false;
    if (load) {
        mapped.kind = MINIC_CORE_INSTRUCTION_LOAD;
        mapped.value.load.address = address;
        mapped.value.load.is_volatile = false;
    } else {
        mapped.kind = MINIC_CORE_INSTRUCTION_STORE;
        mapped.value.store.address = address;
        mapped.value.store.stored_value = payload;
        mapped.value.store.is_volatile = false;
    }
    return x86_scalar_memory(sel, &mapped);
}

/* Migrate the native zero-byte compiler ordering constraint into Common's
 * dedicated typed MachineIR effect. It is neither inline ASM nor an ISA
 * hardware fence, and retains the exact originating Core instruction. */
static bool x86_compiler_barrier(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreType *type = x86_type(sel, ins->core_type_id);
    return ins->result == MINIC_CORE_VALUE_INVALID &&
        type != NULL && type->kind == MINIC_CORE_TYPE_VOID &&
        minic_machine_builder_emit_compiler_barrier(&sel->builder);
}

/* Legacy integer scans deliberately use baseline BSF/BSR, not BMI1.
 * Make their zero input defined before scanning by setting one sentinel
 * bit. For CLZ this bit is the low bit; for CTZ it is the highest bit of
 * the Core operand width. The captured zero flag contributes one to the
 * result, exactly matching GNU's zero-extension fallback at widths 8..64.
 * No target-dependent label text or speculative undefined BSF result. */
static bool x86_integer_bitscan(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    unsigned int width;
    const bool clz = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ;
    if (!minic_core_integer_bitscan_shape(sel->core, ins, &width) ||
        ins->result >= sel->core->value_count ||
        !x86_core_load(sel, ins->value.operand, MINIC_X86_REG_RAX) ||
        !x86_rr(sel, MINIC_X86_MI_TEST64_RR, 64U,
            MINIC_X86_REG_RAX, MINIC_X86_REG_RAX) ||
        !x86_1(sel, MINIC_X86_MI_SETZ8_R,
            x86_reg(MINIC_X86_REG_R11, 8U)) ||
        !x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
            x86_reg(MINIC_X86_REG_R11, 8U),
            x86_reg(MINIC_X86_REG_R11, 64U))) return false;
    if (clz) {
        if (!x86_ir(sel, MINIC_X86_MI_OR64_IR, 1, 64U,
                MINIC_X86_REG_RAX) ||
            !x86_rr(sel, MINIC_X86_MI_BSR64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_R10) ||
            !x86_ir(sel, MINIC_X86_MI_MOV64_IR,
                (int64_t)width - 1, 64U, MINIC_X86_REG_RAX) ||
            !x86_rr(sel, MINIC_X86_MI_SUB64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX)) return false;
    } else {
        if (!x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                (int64_t)(UINT64_C(1) << (width - 1U)),
                64U, MINIC_X86_REG_R10) ||
            !x86_rr(sel, MINIC_X86_MI_OR64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX) ||
            !x86_rr(sel, MINIC_X86_MI_BSF64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RAX)) return false;
    }
    return x86_rr(sel, MINIC_X86_MI_ADD64_RR, 64U,
            MINIC_X86_REG_R11, MINIC_X86_REG_RAX) &&
        x86_result(sel, ins);
}

static bool x86_integer_extend(X86CoreSelector *sel,
    unsigned int reg, size_t width, bool sign) {
    switch (width) {
    case 1U:
        return x86_2(sel, sign ? MINIC_X86_MI_MOVSBQ_RR :
                MINIC_X86_MI_MOVZBQ_RR,
            x86_reg(reg, 8U), x86_reg(reg, 64U));
    case 2U:
        return x86_2(sel, sign ? MINIC_X86_MI_MOVSWQ_RR :
                MINIC_X86_MI_MOVZWQ_RR,
            x86_reg(reg, 16U), x86_reg(reg, 64U));
    case 4U:
        return sign ? x86_2(sel, MINIC_X86_MI_MOVSLQ_RR,
                x86_reg(reg, 32U), x86_reg(reg, 64U)) :
            x86_rr(sel, MINIC_X86_MI_MOV32_RR, 32U, reg, reg);
    case 8U: return true;
    default: return false;
    }
}

static bool x86_overflow_result_memory(X86CoreSelector *sel,
    MinicCoreValueId pointer, size_t width) {
    MinicX86MachineOpcode opcode;
    switch (width) {
    case 1U: opcode = MINIC_X86_MI_MOV8_RA; break;
    case 2U: opcode = MINIC_X86_MI_MOV16_RA; break;
    case 4U: opcode = MINIC_X86_MI_MOV32_RA; break;
    case 8U: opcode = MINIC_X86_MI_MOV64_RA; break;
    default: return false;
    }
    return x86_record_pointer(sel, pointer, MINIC_X86_REG_R10) &&
        x86_2(sel, opcode,
            x86_reg(MINIC_X86_REG_RAX, (unsigned int)(width * 8U)),
            x86_addr(MINIC_X86_REG_R10, 0));
}

/* A matched-width overflow op fits the hardware flags directly. Preserve
 * the low result in *result_address before the returned boolean, including
 * the unsigned 32-bit MUL that requires EDX:EAX, not the 64-bit product. */
static bool x86_integer_overflow_matched(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t width, bool sign) {
    MinicX86MachineOpcode opcode;
    bool use_carry = false;
    const unsigned int bits = (unsigned int)(width * 8U);
    const MinicCoreValueId left = ins->value.integer_overflow.left;
    const MinicCoreValueId right = ins->value.integer_overflow.right;
    if (!x86_core_load(sel, left, MINIC_X86_REG_RAX) ||
        !x86_core_load(sel, right, MINIC_X86_REG_R10)) return false;
    switch (ins->value.integer_overflow.operator_kind) {
    case MINIC_CORE_INTEGER_OVERFLOW_ADD:
        opcode = width == 8U ? MINIC_X86_MI_ADD64_RR : MINIC_X86_MI_ADD32_RR;
        use_carry = !sign;
        break;
    case MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT:
        opcode = width == 8U ? MINIC_X86_MI_SUB64_RR : MINIC_X86_MI_SUB32_RR;
        use_carry = !sign;
        break;
    case MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY:
        opcode = sign ? (width == 8U ? MINIC_X86_MI_IMUL64_RR :
                MINIC_X86_MI_IMUL32_RR) :
            (width == 8U ? MINIC_X86_MI_MUL64_R : MINIC_X86_MI_MUL32_R);
        break;
    default: return false;
    }
    if (!(ins->value.integer_overflow.operator_kind ==
            MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY && !sign
            ? x86_1(sel, opcode, x86_reg(MINIC_X86_REG_R10, bits))
            : x86_rr(sel, opcode, bits,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX)) ||
        !x86_1(sel, use_carry ? MINIC_X86_MI_SETC8_R :
            MINIC_X86_MI_SETO8_R,
            x86_reg(MINIC_X86_REG_R11, 8U)) ||
        !x86_overflow_result_memory(sel,
            ins->value.integer_overflow.result_address, width) ||
        !x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
            x86_reg(MINIC_X86_REG_R11, 8U),
            x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
    return x86_result(sel, ins);
}

/* The GNU mixed-type builtin uses unbounded mathematical arithmetic to
 * decide overflow. Build an exact signed high limb for ADD/SUB/MUL rather
 * than applying the result type to operands before computing flags. */
static bool x86_integer_overflow_mixed(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    MinicCoreTypeId result_type;
    const MinicCoreType *result;
    size_t left_width, right_width, result_width;
    bool left_sign, right_sign, result_sign;
    const MinicCoreValueId left = ins->value.integer_overflow.left;
    const MinicCoreValueId right = ins->value.integer_overflow.right;
    const MinicCoreValueId address =
        ins->value.integer_overflow.result_address;
    if (ins->result >= sel->core->value_count ||
        left >= sel->core->value_count ||
        right >= sel->core->value_count ||
        address >= sel->core->value_count ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[address].core_type_id, &result_type) ||
        (result = x86_type(sel, result_type)) == NULL ||
        result->kind != MINIC_CORE_TYPE_INTEGER ||
        result->value.integer.is_bool ||
        !minic_x86_64_core_scalar_width(sel->core->type_context,
            sel->core->values[left].core_type_id,
            &left_width, &left_sign) ||
        !minic_x86_64_core_scalar_width(sel->core->type_context,
            sel->core->values[right].core_type_id,
            &right_width, &right_sign) ||
        !minic_x86_64_core_scalar_width(sel->core->type_context,
            result_type, &result_width, &result_sign) ||
        !x86_core_load(sel, left, MINIC_X86_REG_RAX) ||
        !x86_core_load(sel, right, MINIC_X86_REG_R10) ||
        !x86_integer_extend(sel, MINIC_X86_REG_RAX,
            left_width, left_sign) ||
        !x86_integer_extend(sel, MINIC_X86_REG_R10,
            right_width, right_sign)) return false;
    switch (ins->value.integer_overflow.operator_kind) {
    case MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY:
        if (!x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_R11) ||
            !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_R8) ||
            !(left_sign ? x86_ir(sel, MINIC_X86_MI_SAR64_IR, 63,
                    64U, MINIC_X86_REG_R8) :
                x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                    MINIC_X86_REG_R8, MINIC_X86_REG_R8)) ||
            !x86_rr(sel, MINIC_X86_MI_AND64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_R8) ||
            !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_R9) ||
            !(right_sign ? x86_ir(sel, MINIC_X86_MI_SAR64_IR, 63,
                    64U, MINIC_X86_REG_R9) :
                x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                    MINIC_X86_REG_R9, MINIC_X86_REG_R9)) ||
            !x86_rr(sel, MINIC_X86_MI_AND64_RR, 64U,
                MINIC_X86_REG_R11, MINIC_X86_REG_R9) ||
            !x86_1(sel, MINIC_X86_MI_MUL64_R,
                x86_reg(MINIC_X86_REG_R10, 64U)) ||
            !x86_rr(sel, MINIC_X86_MI_SUB64_RR, 64U,
                MINIC_X86_REG_R8, MINIC_X86_REG_RDX) ||
            !x86_rr(sel, MINIC_X86_MI_SUB64_RR, 64U,
                MINIC_X86_REG_R9, MINIC_X86_REG_RDX) ||
            !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_RDX, MINIC_X86_REG_R8)) return false;
        break;
    case MINIC_CORE_INTEGER_OVERFLOW_ADD:
    case MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT:
        if (!x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_R8) ||
            !(left_sign ? x86_ir(sel, MINIC_X86_MI_SAR64_IR, 63,
                    64U, MINIC_X86_REG_R8) :
                x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                    MINIC_X86_REG_R8, MINIC_X86_REG_R8)) ||
            !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_R9) ||
            !(right_sign ? x86_ir(sel, MINIC_X86_MI_SAR64_IR, 63,
                    64U, MINIC_X86_REG_R9) :
                x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                    MINIC_X86_REG_R9, MINIC_X86_REG_R9)) ||
            !x86_rr(sel, ins->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_ADD ? MINIC_X86_MI_ADD64_RR :
                MINIC_X86_MI_SUB64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX) ||
            !x86_rr(sel, ins->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_ADD ? MINIC_X86_MI_ADC64_RR :
                MINIC_X86_MI_SBB64_RR, 64U,
                MINIC_X86_REG_R9, MINIC_X86_REG_R8)) return false;
        break;
    default: return false;
    }
    if (!x86_overflow_result_memory(sel, address, result_width) ||
        !x86_2(sel, result_width == 1U
                ? (result_sign ? MINIC_X86_MI_MOVSBQ_RR :
                    MINIC_X86_MI_MOVZBQ_RR)
                : result_width == 2U
                    ? (result_sign ? MINIC_X86_MI_MOVSWQ_RR :
                        MINIC_X86_MI_MOVZWQ_RR)
                    : result_width == 4U
                        ? (result_sign ? MINIC_X86_MI_MOVSLQ_RR :
                            MINIC_X86_MI_MOV32_RR)
                        : MINIC_X86_MI_MOV64_RR,
            x86_reg(MINIC_X86_REG_RAX,
                (unsigned int)(result_width * 8U)),
            x86_reg(MINIC_X86_REG_R11,
                result_width == 4U && !result_sign ? 32U : 64U)) ||
        !x86_rr(sel, MINIC_X86_MI_CMP64_RR, 64U,
            MINIC_X86_REG_R11, MINIC_X86_REG_RAX) ||
        !x86_1(sel, MINIC_X86_MI_SETNZ8_R,
            x86_reg(MINIC_X86_REG_R9, 8U)) ||
        !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
            MINIC_X86_REG_R11, MINIC_X86_REG_R10) ||
        !(result_sign ? x86_ir(sel, MINIC_X86_MI_SAR64_IR, 63,
                64U, MINIC_X86_REG_R10) :
            x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_R10)) ||
        !x86_rr(sel, MINIC_X86_MI_CMP64_RR, 64U,
            MINIC_X86_REG_R10, MINIC_X86_REG_R8) ||
        !x86_1(sel, MINIC_X86_MI_SETNZ8_R,
            x86_reg(MINIC_X86_REG_R10, 8U)) ||
        !x86_rr(sel, MINIC_X86_MI_OR8_RR, 8U,
            MINIC_X86_REG_R10, MINIC_X86_REG_R9) ||
        !x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
            x86_reg(MINIC_X86_REG_R9, 8U),
            x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
    return x86_result(sel, ins);
}

static bool x86_integer_overflow(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    size_t width;
    bool signed_result;
    if (minic_core_integer_overflow_shape(sel->core, ins,
            &width, &signed_result))
        return x86_integer_overflow_matched(sel, ins, width, signed_result);
    return x86_integer_overflow_mixed(sel, ins);
}

static bool x86_fixed_register_read(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFixedRegisterBinding *binding;
    return minic_multi_direct_fixed_register_read_shape(
            sel->target, sel->core, ins, &binding) &&
        binding->register_name_length == 3U &&
        memcmp(binding->register_name, "rsp", 3U) == 0 &&
        x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
            MINIC_X86_REG_RSP, MINIC_X86_REG_RAX) &&
        x86_core_store(sel, ins->result, MINIC_X86_REG_RAX);
}

static bool x86_block_address(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    MinicCoreBlockId destination;
    const char *symbol;
    if (!minic_multi_direct_block_address_shape(sel->core, ins,
            &destination) || !sel->reachable[destination] ||
        (symbol = sel->core->blocks[destination].local_label_symbol) == NULL ||
        !minic_core_label_symbol_valid(symbol)) return false;
    /* The exported GNU &&label is bound to the same physical block used by
     * the computed-goto CFG. No second, text-formatted local label is made. */
    return x86_2(sel, MINIC_X86_MI_LEA64_RIP_SYMBOL_R,
            x86_symbol(symbol, MINIC_X86_MACHINE_RELOC_PC32),
            x86_reg(MINIC_X86_REG_RAX, 64U)) &&
        x86_result(sel, ins);
}

static bool x86_variadic_argument_address(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicX86Frame *frame = sel->frame;
    const MinicCoreType *type = x86_type(sel, ins->core_type_id);
    MinicMachineOperand slot;
    size_t i, tag_offset, overflow_offset;
    if (type == NULL || type->kind != MINIC_CORE_TYPE_POINTER ||
        !type->has_layout || type->size != 8U ||
        ins->result >= sel->core->value_count ||
        frame->varargs_count == 0U || frame->varargs_results == NULL ||
        frame->varargs_tag_base_offset > (size_t)INT32_MAX ||
        frame->varargs_fixed_stack_slots > (INT32_MAX - 16U) / 8U)
        return false;
    for (i = 0U; i < frame->varargs_count; ++i)
        if (frame->varargs_results[i] == ins->result) break;
    if (i == frame->varargs_count ||
        i > (SIZE_MAX / 24U) - 1U ||
        frame->varargs_tag_base_offset > SIZE_MAX - (i + 1U) * 24U)
        return false;
    tag_offset = frame->varargs_tag_base_offset + (i + 1U) * 24U;
    overflow_offset = 16U + frame->varargs_fixed_stack_slots * 8U;
    if (tag_offset > frame->frame_size || tag_offset > INT32_MAX ||
        frame->varargs_regsave_offset > frame->frame_size ||
        frame->varargs_regsave_offset > INT32_MAX ||
        !minic_x86_64_machine_frame_slot(&sel->builder,
            MINIC_MACHINE_FRAME_BASE_FRAME_POINTER,
            MINIC_MACHINE_FRAME_LOCAL, 24U, 8U,
            -(int64_t)tag_offset, &slot) ||
        !x86_2(sel, MINIC_X86_MI_LEA64_FRBP_R, slot,
            x86_reg(MINIC_X86_REG_RAX, 64U)) ||
        !x86_ir(sel, MINIC_X86_MI_MOV32_IR,
            frame->varargs_gp_offset, 32U, MINIC_X86_REG_R10) ||
        !x86_2(sel, MINIC_X86_MI_MOV32_RA,
            x86_reg(MINIC_X86_REG_R10, 32U),
            x86_addr(MINIC_X86_REG_RAX, 0)) ||
        !x86_ir(sel, MINIC_X86_MI_MOV32_IR,
            frame->varargs_fp_offset, 32U, MINIC_X86_REG_R10) ||
        !x86_2(sel, MINIC_X86_MI_MOV32_RA,
            x86_reg(MINIC_X86_REG_R10, 32U),
            x86_addr(MINIC_X86_REG_RAX, 4)) ||
        !x86_2(sel, MINIC_X86_MI_LEA64_AR,
            x86_addr(MINIC_X86_REG_RBP, (int64_t)overflow_offset),
            x86_reg(MINIC_X86_REG_R10, 64U)) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_R10, 64U),
            x86_addr(MINIC_X86_REG_RAX, 8)) ||
        !x86_2(sel, MINIC_X86_MI_LEA64_AR,
            x86_addr(MINIC_X86_REG_RBP,
                -(int64_t)frame->varargs_regsave_offset),
            x86_reg(MINIC_X86_REG_R10, 64U)) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_R10, 64U),
            x86_addr(MINIC_X86_REG_RAX, 16))) return false;
    return x86_core_store(sel, ins->result, MINIC_X86_REG_RAX);
}

static bool x86_variadic_copy(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    size_t offset;
    const MinicCoreValueId dst = ins->value.variadic_copy.destination;
    const MinicCoreValueId src = ins->value.variadic_copy.source;
    const MinicCoreType *type = x86_type(sel, ins->core_type_id);
    if (ins->result != MINIC_CORE_VALUE_INVALID || type == NULL ||
        type->kind != MINIC_CORE_TYPE_VOID ||
        !x86_record_pointer(sel, dst, MINIC_X86_REG_R10) ||
        !x86_record_pointer(sel, src, MINIC_X86_REG_R11)) return false;
    /* Preserve all three 64-bit lanes, including the two pointer fields;
     * this is a value copy, not a va_list alias. */
    for (offset = 0U; offset < 24U; offset += 8U)
        if (!x86_2(sel, MINIC_X86_MI_MOV64_AR,
                x86_addr(MINIC_X86_REG_R11, (int64_t)offset),
                x86_reg(MINIC_X86_REG_RAX, 64U)) ||
            !x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(MINIC_X86_REG_RAX, 64U),
                x86_addr(MINIC_X86_REG_R10, (int64_t)offset))) return false;
    return true;
}

static bool x86_variadic_next_scalar(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    const MinicCoreValueId cursor = ins->value.operand;
    const MinicCoreType *type = x86_type(sel, ins->core_type_id);
    MinicCoreTypeId pointee;
    size_t width;
    bool sign, is_bool;
    const bool is_double = type != NULL &&
        type->kind == MINIC_CORE_TYPE_FLOAT &&
        type->value.floating.format == MINIC_CORE_FLOAT_BINARY64 &&
        type->size == 8U;
    const int64_t offset = is_double ? 4 : 0;
    const int64_t limit = is_double ? 176 : 48;
    const int64_t step = is_double ? 16 : 8;
    size_t registers, stack, done;
    if (instruction_id >= sel->core->instruction_count ||
        sel->instruction_aux == NULL) return false;
    registers = sel->instruction_aux[instruction_id].first;
    stack = sel->instruction_aux[instruction_id].second;
    done = sel->instruction_aux[instruction_id].third;
    if (ins->result >= sel->core->value_count ||
        cursor >= sel->core->value_count ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[cursor].core_type_id, &pointee) ||
        (!is_double && !x86_scalar_shape(sel, ins->core_type_id,
            &width, &sign, &is_bool)) ||
        !x86_record_pointer(sel, cursor, MINIC_X86_REG_R10) ||
        !x86_2(sel, MINIC_X86_MI_MOV32_AR,
            x86_addr(MINIC_X86_REG_R10, offset),
            x86_reg(MINIC_X86_REG_RCX, 32U)) ||
        !x86_ir(sel, MINIC_X86_MI_CMP32_IR,
            limit, 32U, MINIC_X86_REG_RCX) ||
        registers == SIZE_MAX || stack == SIZE_MAX || done == SIZE_MAX ||
        !x86_1(sel, MINIC_X86_MI_JAE_B, x86_block(stack)) ||
        !minic_machine_builder_add_successor(&sel->builder, stack) ||
        !minic_machine_builder_add_successor(&sel->builder, registers) ||
        !minic_machine_builder_set_fallthrough(&sel->builder, registers) ||
        !minic_machine_builder_position_at(&sel->builder, registers) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_R10, 16),
            x86_reg(MINIC_X86_REG_R11, 64U)) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_indexed_addr(MINIC_X86_REG_R11,
                MINIC_X86_REG_RCX, 1U),
            x86_reg(MINIC_X86_REG_RAX, 64U)) ||
        !x86_ir(sel, MINIC_X86_MI_ADD32_IR,
            step, 32U, MINIC_X86_REG_RCX) ||
        !x86_2(sel, MINIC_X86_MI_MOV32_RA,
            x86_reg(MINIC_X86_REG_RCX, 32U),
            x86_addr(MINIC_X86_REG_R10, offset)) ||
        !x86_1(sel, MINIC_X86_MI_JMP_B, x86_block(done)) ||
        !minic_machine_builder_add_successor(&sel->builder, done) ||
        !minic_machine_builder_position_at(&sel->builder, stack) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_R10, 8),
            x86_reg(MINIC_X86_REG_R11, 64U)) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_R11, 0),
            x86_reg(MINIC_X86_REG_RAX, 64U)) ||
        !x86_ir(sel, MINIC_X86_MI_ADD64_IR,
            8, 64U, MINIC_X86_REG_R11) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_R11, 64U),
            x86_addr(MINIC_X86_REG_R10, 8)) ||
        !x86_1(sel, MINIC_X86_MI_JMP_B, x86_block(done)) ||
        !minic_machine_builder_add_successor(&sel->builder, done) ||
        !minic_machine_builder_position_at(&sel->builder, done)) return false;
    return (is_double || x86_normalize(sel, ins->core_type_id)) &&
        x86_core_store(sel, ins->result, MINIC_X86_REG_RAX);
}

static bool x86_dynamic_stack_alloc(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    size_t count_width, safe, trap;
    bool count_sign;
    const MinicCoreValueId count = ins->value.operand;
    const MinicCoreType *type = x86_type(sel, ins->core_type_id);
    if (instruction_id >= sel->core->instruction_count ||
        sel->instruction_aux == NULL) return false;
    trap = sel->instruction_aux[instruction_id].first;
    safe = sel->instruction_aux[instruction_id].second;
    if (sel->frame->objects_from_aligned_rsp || type == NULL ||
        type->kind != MINIC_CORE_TYPE_POINTER ||
        ins->result >= sel->core->value_count ||
        sel->core->values[ins->result].core_type_id != ins->core_type_id ||
        count >= sel->core->value_count ||
        !minic_x86_64_core_scalar_width(sel->core->type_context,
            sel->core->values[count].core_type_id,
            &count_width, &count_sign) || count_width != 8U || count_sign ||
        !x86_core_load(sel, count, MINIC_X86_REG_R10) ||
        !x86_ir(sel, MINIC_X86_MI_ADD64_IR,
            15, 64U, MINIC_X86_REG_R10) ||
        trap == SIZE_MAX || safe == SIZE_MAX ||
        !x86_1(sel, MINIC_X86_MI_JNC_B, x86_block(safe)) ||
        !minic_machine_builder_add_successor(&sel->builder, safe) ||
        !minic_machine_builder_add_successor(&sel->builder, trap) ||
        !minic_machine_builder_set_fallthrough(&sel->builder, trap) ||
        !minic_machine_builder_position_at(&sel->builder, trap) ||
        !x86_0(sel, MINIC_X86_MI_UD2) ||
        !minic_machine_builder_position_at(&sel->builder, safe) ||
        !x86_ir(sel, MINIC_X86_MI_AND64_IR,
            -16, 64U, MINIC_X86_REG_R10) ||
        !x86_rr(sel, MINIC_X86_MI_SUB64_RR, 64U,
            MINIC_X86_REG_R10, MINIC_X86_REG_RSP) ||
        !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
            MINIC_X86_REG_RSP, MINIC_X86_REG_RAX)) return false;
    sel->builder.function->has_dynamic_stack = true;
    return x86_core_store(sel, ins->result, MINIC_X86_REG_RAX);
}

static bool x86_core_instruction(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    size_t bytes;
    bool sign, is_bool;
    if (sel == NULL || ins == NULL) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return x86_parameter(sel, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return x86_parameter_object(sel, ins);
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        return x86_record_load(sel, ins);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return x86_record_copy(sel, ins);
    case MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY:
        return x86_vector_storage_copy(sel, ins);
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return x86_symbol_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        return x86_atomic_scalar(sel, ins);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return x86_compiler_barrier(sel, ins);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
        return x86_fixed_register_read(sel, ins);
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return x86_block_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return x86_variadic_argument_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
        return x86_variadic_next_scalar(sel, ins, instruction_id);
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        return x86_variadic_copy(sel, ins);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        return x86_dynamic_stack_alloc(sel, ins, instruction_id);
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
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        return x86_integer_bitscan(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        return x86_integer_overflow(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return (ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_BITCAST ||
            !sel->frame->kernel_head_link_addresses ||
            sel->frame->kernel_head_vma_cast_globals == NULL ||
            ins->result >= sel->core->value_count ||
            sel->frame->kernel_head_vma_cast_globals[ins->result] ==
                MINIC_CORE_GLOBAL_INVALID) &&
            ins->value.operand < sel->core->value_count &&
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
    if (core->instruction_count > SIZE_MAX /
            sizeof(*sel.instruction_aux)) goto cleanup;
    sel.instruction_aux = calloc(core->instruction_count != 0U ?
        core->instruction_count : 1U, sizeof(*sel.instruction_aux));
    if (sel.blocks == NULL || sel.false_blocks == NULL ||
        sel.reachable == NULL || sel.instruction_aux == NULL ||
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
    for (i = 0U; i < core->instruction_count; ++i) {
        sel.instruction_aux[i].first = SIZE_MAX;
        sel.instruction_aux[i].second = SIZE_MAX;
        sel.instruction_aux[i].third = SIZE_MAX;
    }
    if (!minic_machine_builder_initialize(&sel.builder, machine) ||
        !minic_x86_64_machine_select_prologue(&sel.builder,
            &sel.frame_view, &prologue)) goto cleanup;
    for (i = 0U; i < count; ++i) {
        const MinicCoreBlock *block = &core->blocks[i];
        size_t machine_id, j;
        if (!sel.reachable[i]) continue;
        if (!minic_machine_builder_create_block(&sel.builder,
                block->local_label_symbol, block->indirect_entry,
                false, &machine_id) ||
            !minic_machine_builder_mark_core_block(&sel.builder, i))
            goto cleanup;
        sel.blocks[i] = machine_id;
        /* Reserve all split blocks alongside the owning Core block in
         * physical emission order. A conditional MachineIR fallthrough
         * must be the immediately following block, not an auxiliary
         * appended after the other Core blocks have been emitted. */
        for (j = 0U; j < block->instruction_count; ++j) {
            MinicCoreInstructionId id;
            const MinicCoreInstruction *ins;
            X86AuxBlocks *aux;
            if (!minic_core_block_instruction_at(core, block, j,
                    &id, &ins) || id >= core->instruction_count)
                goto cleanup;
            aux = &sel.instruction_aux[id];
            if (ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR) {
                if (!minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->first) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->second) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->third)) goto cleanup;
            } else if (ins->kind ==
                    MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC) {
                if (!minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->first) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->second)) goto cleanup;
            }
        }
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
    free(sel.instruction_aux);
    if (!ok) minic_machine_function_destroy(machine);
    return ok;
}
