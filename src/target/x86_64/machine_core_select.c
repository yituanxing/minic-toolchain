#include "target/x86_64/machine_core_select.h"

#include "target/x86_64/abi.h"
#include "target/x86_64/codegen_options.h"
#include "target/x86_64/asm_contract.h"
#include "target/x86_64/machine_frame_select.h"
#include "target/x86_64/machine_opcode.h"
#include "target/x86_64/machine_target_state.h"
#include "target/common/machine_builder.h"
#include "target/common/core_block_walk.h"
#include "target/common/machine_gnu_asm.h"
#include "target/common/core_machine_asm.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/core_scalar_conversion.h"
#include "target/common/core_floating_shape.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/elf_module_symbol_info.h"
#include "target/common/lp64_scalar.h"
#include "core/symbol_identity.h"
#include "core/value_uses.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
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
    /* Common Core semantic use census; never inferred from Machine registers. */
    size_t *value_uses;
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
    if (sel == NULL || sel->value_uses == NULL ||
        value >= sel->core->value_count) return false;
    /* The producing instruction still executes; only its unused SSA frame
     * writeback can disappear. In particular, do not discard volatile loads,
     * atomics, function calls, or any other observable Core instruction. */
    if (sel->value_uses[value] == 0U) return true;
    return x86_slot(sel, value, &slot) &&
        x86_2(sel, MINIC_X86_MI_MOV64_R_FRBP,
            x86_reg(reg, 64U), slot);
}

static bool x86_i128_type(const X86CoreSelector *sel, MinicCoreTypeId id,
    bool *signed_integer) {
    const MinicCoreType *type = minic_core_type_get(sel->core->type_context,
        minic_core_type_unqualified(sel->core->type_context, id));
    if (type == NULL || type->kind != MINIC_CORE_TYPE_INTEGER ||
        !type->has_layout || type->size != 16U ||
        type->value.integer.bit_width != 128U ||
        type->value.integer.is_bool) return false;
    if (signed_integer != NULL)
        *signed_integer = type->value.integer.is_signed;
    return true;
}

static bool x86_i128_load(X86CoreSelector *sel, MinicCoreValueId id,
    unsigned int low, unsigned int high) {
    size_t distance;
    if (id >= sel->frame->value_count ||
        sel->frame->value_offsets == NULL ||
        (distance = sel->frame->value_offsets[id]) < 16U ||
        distance > sel->frame->frame_size || distance > INT32_MAX)
        return false;
    return x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_RBP, -(int64_t)distance),
            x86_reg(low, 64U)) &&
        x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_RBP, -(int64_t)(distance - 8U)),
            x86_reg(high, 64U));
}

/* An i128 truth value observes BOTH physical limbs. A nonzero high word
 * must not vanish when the low word happens to be zero (including bit127).
 * The normalized 0/1 is a regular Core scalar result in RAX. */
static bool x86_i128_boolean_result(X86CoreSelector *sel,
    MinicCoreValueId result, bool invert) {
    return x86_rr(sel, MINIC_X86_MI_OR64_RR, 64U,
            MINIC_X86_REG_RDX, MINIC_X86_REG_RAX) &&
        x86_rr(sel, MINIC_X86_MI_TEST64_RR, 64U,
            MINIC_X86_REG_RAX, MINIC_X86_REG_RAX) &&
        x86_1(sel, invert ? MINIC_X86_MI_SETZ8_R :
                MINIC_X86_MI_SETNZ8_R,
            x86_reg(MINIC_X86_REG_RAX, 8U)) &&
        x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
            x86_reg(MINIC_X86_REG_RAX, 8U),
            x86_reg(MINIC_X86_REG_RAX, 64U)) &&
        x86_core_store(sel, result, MINIC_X86_REG_RAX);
}

static bool x86_i128_store(X86CoreSelector *sel, MinicCoreValueId id,
    unsigned int low, unsigned int high) {
    size_t distance;
    if (sel->value_uses == NULL || id >= sel->core->value_count ||
        id >= sel->frame->value_count ||
        sel->frame->value_offsets == NULL ||
        (distance = sel->frame->value_offsets[id]) < 16U ||
        distance > sel->frame->frame_size || distance > INT32_MAX)
        return false;
    if (sel->value_uses[id] == 0U) return true;
    return x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(low, 64U),
            x86_addr(MINIC_X86_REG_RBP, -(int64_t)distance)) &&
        x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(high, 64U),
            x86_addr(MINIC_X86_REG_RBP, -(int64_t)(distance - 8U)));
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

/* An early kernel head numeric VMA is a link address, while memory is still
 * addressed through its physical RIP-relative mapping. Preserve arbitrary
 * field/index offsets by subtracting the link symbol before adding its
 * physical RIP-relative address. The base is a typed 32S absolute relocation,
 * not a textual assembler escape or a truncated ABS64 symbol. */
static bool x86_record_pointer(X86CoreSelector *sel,
    MinicCoreValueId value, unsigned int reg) {
    MinicCoreGlobalId global = MINIC_CORE_GLOBAL_INVALID;
    const char *symbol;
    const unsigned int scratch = reg == MINIC_X86_REG_RAX ?
        MINIC_X86_REG_R11 : MINIC_X86_REG_RAX;
    if (value >= sel->core->value_count) return false;
    if (sel->frame->kernel_head_link_addresses &&
        sel->frame->kernel_head_memory_rebase_globals != NULL)
        global = sel->frame->kernel_head_memory_rebase_globals[value];
    if (!x86_core_load(sel, value, reg)) return false;
    if (global == MINIC_CORE_GLOBAL_INVALID) return true;
    if (global >= sel->core->global_count ||
        sel->core->globals == NULL ||
        (symbol = sel->core->globals[global].name) == NULL ||
        !minic_core_inline_asm_symbol_valid(symbol)) return false;
    return x86_2(sel, MINIC_X86_MI_MOV64_IMM32S_SYMBOL_R,
            x86_symbol(symbol, MINIC_X86_MACHINE_RELOC_ABS32S),
            x86_reg(scratch, 64U)) &&
        x86_rr(sel, MINIC_X86_MI_SUB64_RR, 64U, scratch, reg) &&
        x86_2(sel, MINIC_X86_MI_LEA64_RIP_SYMBOL_R,
            x86_symbol(symbol, MINIC_X86_MACHINE_RELOC_PC32),
            x86_reg(scratch, 64U)) &&
        x86_rr(sel, MINIC_X86_MI_ADD64_RR, 64U, scratch, reg);
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
    /* REP MOVSB preserves the argument bank with three pushes. Account for
     * their live stack displacement when the caller owns an RSP operand. */
    if (src_base == MINIC_X86_REG_RSP) {
        if (src_offset > INT32_MAX - 24U) return false;
        src_offset += 24U;
    }
    if (dst_base == MINIC_X86_REG_RSP) {
        if (dst_offset > INT32_MAX - 24U) return false;
        dst_offset += 24U;
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

/* SysV AMD64 transports long double as an 80-bit x87 payload in a 16-byte
 * Core slot.  The slot's upper six bytes are ABI padding, not a binary128
 * value; all x87 operations below use selected physical MachineIR opcodes. */
static bool x86_x87_shape(const X86CoreSelector *sel, MinicCoreTypeId id) {
    const MinicCoreType *type = x86_type(sel, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_FLOAT &&
        type->has_layout && type->size == 16U &&
        type->value.floating.format == MINIC_CORE_FLOAT_X87_EXTENDED80;
}

static bool x86_x87_slot(X86CoreSelector *sel, MinicCoreValueId value,
    size_t width, size_t byte_offset, MinicMachineOperand *operand) {
    size_t distance;
    if (sel == NULL || operand == NULL ||
        value >= sel->core->value_count ||
        !x86_x87_shape(sel, sel->core->values[value].core_type_id) ||
        sel->frame->value_offsets == NULL ||
        (distance = sel->frame->value_offsets[value]) < 16U ||
        distance > sel->frame->frame_size || distance > INT32_MAX ||
        width == 0U || byte_offset > 16U || width > 16U - byte_offset)
        return false;
    return minic_x86_64_machine_frame_slot(&sel->builder,
        MINIC_MACHINE_FRAME_BASE_FRAME_POINTER,
        MINIC_MACHINE_FRAME_LOCAL, width, 1U,
        -(int64_t)(distance - byte_offset), operand);
}

static bool x86_x87_load(X86CoreSelector *sel, MinicCoreValueId value) {
    MinicMachineOperand slot;
    return x86_x87_slot(sel, value, 10U, 0U, &slot) &&
        x86_1(sel, MINIC_X86_MI_FLDT_FRAME_RBP, slot);
}

static bool x86_x87_store(X86CoreSelector *sel, MinicCoreValueId value) {
    MinicMachineOperand slot;
    return x86_x87_slot(sel, value, 10U, 0U, &slot) &&
        x86_1(sel, MINIC_X86_MI_FSTPT_FRAME_RBP, slot);
}

static bool x86_x87_constant(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const uint64_t hi = ins->value.floating128_bits.high;
    const uint64_t lo = ins->value.floating128_bits.low;
    const uint64_t fraction_hi = hi & UINT64_C(0x0000ffffffffffff);
    const unsigned int exponent = (unsigned int)((hi >> 48U) & 0x7fffU);
    const uint64_t significand =
        (exponent == 0U ? 0U : UINT64_C(1) << 63U) |
        (fraction_hi << 15U) | (lo >> 49U);
    MinicMachineOperand low, high;
    if (ins->result >= sel->core->value_count ||
        (lo & UINT64_C(0x0001ffffffffffff)) != 0U ||
        !x86_x87_slot(sel, ins->result, 8U, 0U, &low) ||
        !x86_x87_slot(sel, ins->result, 8U, 8U, &high)) return false;
    return x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
            (int64_t)significand, 64U, MINIC_X86_REG_RAX) &&
        x86_2(sel, MINIC_X86_MI_MOV64_R_FRBP,
            x86_reg(MINIC_X86_REG_RAX, 64U), low) &&
        x86_ir(sel, MINIC_X86_MI_MOV64_IR,
            (int64_t)(hi >> 48U), 64U, MINIC_X86_REG_RAX) &&
        x86_2(sel, MINIC_X86_MI_MOV64_R_FRBP,
            x86_reg(MINIC_X86_REG_RAX, 64U), high);
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

/* The native binary32/binary64 conversion and comparison family uses only
 * typed physical GP/XMM instructions. Preserve the unsigned-64 boundaries
 * and the unordered NaN mask; neither can be replaced by the signed fast
 * path. Split control flow into genuine adjacent MachineIR basic blocks. */
static bool x86_fp_conversion(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    MinicCoreFloatingShape shape = {0};
    const MinicCoreValueId operand = ins->value.operand;
    const bool to_double = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE;
    const bool to_integer = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER;
    size_t first, second, third;
    if (instruction_id >= sel->core->instruction_count ||
        sel->instruction_aux == NULL ||
        !minic_core_floating_shape(sel->core, ins, &shape) ||
        operand >= sel->core->value_count) return false;
    first = sel->instruction_aux[instruction_id].first;
    second = sel->instruction_aux[instruction_id].second;
    third = sel->instruction_aux[instruction_id].third;
    if (to_double) {
        if (shape.integer_bytes > 8U ||
            !x86_core_load(sel, operand, MINIC_X86_REG_RAX) ||
            !x86_2(sel, MINIC_X86_MI_CVTSI2SDQ_GPR_XMM,
                x86_reg(MINIC_X86_REG_RAX, 64U), x86_xmm(0U))) return false;
        if (shape.integer_bytes == 8U && !shape.integer_signed) {
            if (first == SIZE_MAX || second == SIZE_MAX ||
                !x86_rr(sel, MINIC_X86_MI_TEST64_RR, 64U,
                    MINIC_X86_REG_RAX, MINIC_X86_REG_RAX) ||
                !x86_1(sel, MINIC_X86_MI_JNS_B, x86_block(second)) ||
                !minic_machine_builder_add_successor(&sel->builder, second) ||
                !minic_machine_builder_add_successor(&sel->builder, first) ||
                !minic_machine_builder_set_fallthrough(&sel->builder, first) ||
                !minic_machine_builder_position_at(&sel->builder, first) ||
                !x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                    (int64_t)UINT64_C(0x43f0000000000000),
                    64U, MINIC_X86_REG_R10) ||
                !x86_2(sel, MINIC_X86_MI_MOVQ_GPR_XMM,
                    x86_reg(MINIC_X86_REG_R10, 64U), x86_xmm(1U)) ||
                !x86_2(sel, MINIC_X86_MI_ADDSD_XMM,
                    x86_xmm(1U), x86_xmm(0U)) ||
                !x86_1(sel, MINIC_X86_MI_JMP_B, x86_block(second)) ||
                !minic_machine_builder_add_successor(&sel->builder, second) ||
                !minic_machine_builder_position_at(&sel->builder, second))
                return false;
        }
        return x86_float_store_xmm(sel, ins->result, 0U, true);
    }
    if (to_integer) {
        if (shape.integer_bytes > 8U ||
            !x86_float_load_xmm(sel, operand, 0U,
                MINIC_X86_REG_RAX, true)) return false;
        if (shape.integer_bytes == 8U && !shape.integer_signed) {
            if (first == SIZE_MAX || second == SIZE_MAX ||
                third == SIZE_MAX ||
                !x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                    (int64_t)UINT64_C(0x43e0000000000000),
                    64U, MINIC_X86_REG_R10) ||
                !x86_2(sel, MINIC_X86_MI_MOVQ_GPR_XMM,
                    x86_reg(MINIC_X86_REG_R10, 64U), x86_xmm(1U)) ||
                !x86_2(sel, MINIC_X86_MI_UCOMISD_XMM,
                    x86_xmm(1U), x86_xmm(0U)) ||
                !x86_1(sel, MINIC_X86_MI_JB_B, x86_block(second)) ||
                !minic_machine_builder_add_successor(&sel->builder, second) ||
                !minic_machine_builder_add_successor(&sel->builder, first) ||
                !minic_machine_builder_set_fallthrough(&sel->builder, first) ||
                !minic_machine_builder_position_at(&sel->builder, first) ||
                !x86_2(sel, MINIC_X86_MI_SUBSD_XMM,
                    x86_xmm(1U), x86_xmm(0U)) ||
                !x86_2(sel, MINIC_X86_MI_CVTTSD2SIQ_XMM_GPR,
                    x86_xmm(0U), x86_reg(MINIC_X86_REG_RAX, 64U)) ||
                !x86_ir(sel, MINIC_X86_MI_BTS64_IR,
                    63, 64U, MINIC_X86_REG_RAX) ||
                !x86_1(sel, MINIC_X86_MI_JMP_B, x86_block(third)) ||
                !minic_machine_builder_add_successor(&sel->builder, third) ||
                !minic_machine_builder_position_at(&sel->builder, second) ||
                !x86_2(sel, MINIC_X86_MI_CVTTSD2SIQ_XMM_GPR,
                    x86_xmm(0U), x86_reg(MINIC_X86_REG_RAX, 64U)) ||
                !x86_1(sel, MINIC_X86_MI_JMP_B, x86_block(third)) ||
                !minic_machine_builder_add_successor(&sel->builder, third) ||
                !minic_machine_builder_position_at(&sel->builder, third))
                return false;
        } else if (!x86_2(sel, MINIC_X86_MI_CVTTSD2SIQ_XMM_GPR,
                x86_xmm(0U), x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        return x86_result(sel, ins);
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE) {
        return x86_float_load_xmm(sel, operand, 0U,
                MINIC_X86_REG_RAX, false) &&
            x86_2(sel, MINIC_X86_MI_CVTSS2SD_XMM,
                x86_xmm(0U), x86_xmm(0U)) &&
            x86_float_store_xmm(sel, ins->result, 0U, true);
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT) {
        return x86_float_load_xmm(sel, operand, 0U,
                MINIC_X86_REG_RAX, true) &&
            x86_2(sel, MINIC_X86_MI_CVTSD2SS_XMM,
                x86_xmm(0U), x86_xmm(0U)) &&
            x86_float_store_xmm(sel, ins->result, 0U, false);
    }
    return false;
}

static bool x86_fp_compare_or_negate(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    MinicCoreFloatingShape shape = {0};
    MinicX86MachineOpcode predicate;
    if (!minic_core_floating_shape(sel->core, ins, &shape)) return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE)
        return x86_core_load(sel, ins->value.operand,
                MINIC_X86_REG_RAX) &&
            x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                INT64_MIN, 64U, MINIC_X86_REG_R10) &&
            x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX) &&
            x86_core_store(sel, ins->result, MINIC_X86_REG_RAX);
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        predicate = MINIC_X86_MI_SETZ8_R; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        predicate = MINIC_X86_MI_SETB8_R; break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        predicate = MINIC_X86_MI_SETBE8_R; break;
    default: return false;
    }
    return x86_float_load_xmm(sel, ins->value.binary.left,
            0U, MINIC_X86_REG_RAX, true) &&
        x86_float_load_xmm(sel, ins->value.binary.right,
            1U, MINIC_X86_REG_R10, true) &&
        x86_2(sel, MINIC_X86_MI_UCOMISD_XMM,
            x86_xmm(1U), x86_xmm(0U)) &&
        x86_1(sel, predicate, x86_reg(MINIC_X86_REG_RAX, 8U)) &&
        x86_1(sel, MINIC_X86_MI_SETNP8_R,
            x86_reg(MINIC_X86_REG_R10, 8U)) &&
        x86_rr(sel, MINIC_X86_MI_AND8_RR, 8U,
            MINIC_X86_REG_R10, MINIC_X86_REG_RAX) &&
        x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
            x86_reg(MINIC_X86_REG_RAX, 8U),
            x86_reg(MINIC_X86_REG_RAX, 64U)) &&
        x86_result(sel, ins);
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

/* Pair-register SysV i128 arithmetic. Every operation is a physical x86
 * instruction; no opaque C runtime helper or pre-rendered assembler opcode.
 * The sealed Core signedness determines only the high-word comparison and
 * arithmetic right shift. */
static bool x86_jump(X86CoreSelector *, size_t);

static bool x86_i128_binary(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    const MinicCoreValueId left = ins->value.binary.left;
    const MinicCoreValueId right = ins->value.binary.right;
    const bool shift = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
    bool signed_integer;
    size_t small, large, done;
    MinicX86MachineOpcode lo, hi;
    if (left >= sel->core->value_count ||
        right >= sel->core->value_count ||
        !x86_i128_type(sel, sel->core->values[left].core_type_id,
            &signed_integer) ||
        (!shift && !x86_i128_type(sel,
            sel->core->values[right].core_type_id, NULL)) ||
        !x86_i128_load(sel, left, MINIC_X86_REG_RAX, MINIC_X86_REG_RDX))
        return false;
    if (shift) {
        size_t width;
        bool sign, is_bool;
        if (instruction_id >= sel->core->instruction_count ||
            !x86_scalar_shape(sel,
                sel->core->values[right].core_type_id,
                &width, &sign, &is_bool)) return false;
        small = sel->instruction_aux[instruction_id].first;
        large = sel->instruction_aux[instruction_id].second;
        done = sel->instruction_aux[instruction_id].third;
        if (small == SIZE_MAX || large == SIZE_MAX || done == SIZE_MAX ||
            !x86_core_load(sel, right, MINIC_X86_REG_RCX) ||
            !x86_ir(sel, MINIC_X86_MI_CMP64_IR, 64,
                64U, MINIC_X86_REG_RCX) ||
            !x86_1(sel, MINIC_X86_MI_JAE_B, x86_block(large)) ||
            !minic_machine_builder_add_successor(&sel->builder, large) ||
            !minic_machine_builder_add_successor(&sel->builder, small) ||
            !minic_machine_builder_set_fallthrough(&sel->builder, small) ||
            !minic_machine_builder_position_at(&sel->builder, small))
            return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) {
            if (!x86_rr(sel, MINIC_X86_MI_SHRD64_CL_RR, 64U,
                    MINIC_X86_REG_RDX, MINIC_X86_REG_RAX) ||
                !x86_1(sel, signed_integer ? MINIC_X86_MI_SAR64_CL_R :
                    MINIC_X86_MI_SHR64_CL_R,
                    x86_reg(MINIC_X86_REG_RDX, 64U))) return false;
        } else if (!x86_rr(sel, MINIC_X86_MI_SHLD64_CL_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) ||
            !x86_1(sel, MINIC_X86_MI_SHL64_CL_R,
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        if (!x86_jump(sel, done) ||
            !minic_machine_builder_position_at(&sel->builder, large) ||
            !x86_ir(sel, MINIC_X86_MI_SUB64_IR, 64,
                64U, MINIC_X86_REG_RCX)) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) {
            if (!x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                    MINIC_X86_REG_RDX, MINIC_X86_REG_RAX) ||
                !x86_1(sel, signed_integer ? MINIC_X86_MI_SAR64_CL_R :
                    MINIC_X86_MI_SHR64_CL_R,
                    x86_reg(MINIC_X86_REG_RAX, 64U)) ||
                !(signed_integer ?
                    x86_ir(sel, MINIC_X86_MI_SAR64_IR, 63,
                        64U, MINIC_X86_REG_RDX) :
                    x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                        MINIC_X86_REG_RDX, MINIC_X86_REG_RDX))) return false;
        } else if (!x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) ||
            !x86_1(sel, MINIC_X86_MI_SHL64_CL_R,
                x86_reg(MINIC_X86_REG_RDX, 64U)) ||
            !x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RAX)) return false;
        return x86_jump(sel, done) &&
            minic_machine_builder_position_at(&sel->builder, done) &&
            x86_i128_store(sel, ins->result,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RDX);
    }
    if (!x86_i128_load(sel, right,
            MINIC_X86_REG_R10, MINIC_X86_REG_R11)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
            lo = MINIC_X86_MI_ADD64_RR;
            hi = MINIC_X86_MI_ADC64_RR;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
            lo = MINIC_X86_MI_SUB64_RR;
            hi = MINIC_X86_MI_SBB64_RR;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
            lo = hi = MINIC_X86_MI_AND64_RR; break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
            lo = hi = MINIC_X86_MI_OR64_RR; break;
        default: lo = hi = MINIC_X86_MI_XOR64_RR; break;
        }
        if (!x86_rr(sel, lo, 64U, MINIC_X86_REG_R10,
                MINIC_X86_REG_RAX) ||
            !x86_rr(sel, hi, 64U, MINIC_X86_REG_R11,
                MINIC_X86_REG_RDX)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        if (!x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_RDX, MINIC_X86_REG_R8) ||
            !x86_rr(sel, MINIC_X86_MI_IMUL64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_R8) ||
            !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_R11, MINIC_X86_REG_R9) ||
            !x86_rr(sel, MINIC_X86_MI_IMUL64_RR, 64U,
                MINIC_X86_REG_RAX, MINIC_X86_REG_R9) ||
            !x86_1(sel, MINIC_X86_MI_MUL64_R,
                x86_reg(MINIC_X86_REG_R10, 64U)) ||
            !x86_rr(sel, MINIC_X86_MI_ADD64_RR, 64U,
                MINIC_X86_REG_R8, MINIC_X86_REG_RDX) ||
            !x86_rr(sel, MINIC_X86_MI_ADD64_RR, 64U,
                MINIC_X86_REG_R9, MINIC_X86_REG_RDX)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        if (!x86_rr(sel, MINIC_X86_MI_CMP64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX) ||
            !x86_1(sel, MINIC_X86_MI_SETB8_R,
                x86_reg(MINIC_X86_REG_RCX, 8U)) ||
            !x86_rr(sel, MINIC_X86_MI_CMP64_RR, 64U,
                MINIC_X86_REG_R11, MINIC_X86_REG_RDX) ||
            !x86_1(sel, signed_integer ? MINIC_X86_MI_SETL8_R :
                MINIC_X86_MI_SETB8_R,
                x86_reg(MINIC_X86_REG_RAX, 8U)) ||
            !x86_1(sel, MINIC_X86_MI_SETZ8_R,
                x86_reg(MINIC_X86_REG_RDX, 8U)) ||
            !x86_rr(sel, MINIC_X86_MI_AND8_RR, 8U,
                MINIC_X86_REG_RCX, MINIC_X86_REG_RDX) ||
            !x86_rr(sel, MINIC_X86_MI_OR8_RR, 8U,
                MINIC_X86_REG_RDX, MINIC_X86_REG_RAX) ||
            !x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
                x86_reg(MINIC_X86_REG_RAX, 8U),
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        return x86_core_store(sel, ins->result, MINIC_X86_REG_RAX);
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!x86_rr(sel, MINIC_X86_MI_CMP64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX) ||
            !x86_1(sel, MINIC_X86_MI_SETZ8_R,
                x86_reg(MINIC_X86_REG_RCX, 8U)) ||
            !x86_rr(sel, MINIC_X86_MI_CMP64_RR, 64U,
                MINIC_X86_REG_R11, MINIC_X86_REG_RDX) ||
            !x86_1(sel, MINIC_X86_MI_SETZ8_R,
                x86_reg(MINIC_X86_REG_RAX, 8U)) ||
            !x86_rr(sel, MINIC_X86_MI_AND8_RR, 8U,
                MINIC_X86_REG_RCX, MINIC_X86_REG_RAX) ||
            !x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
                x86_reg(MINIC_X86_REG_RAX, 8U),
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        return x86_core_store(sel, ins->result, MINIC_X86_REG_RAX);
    default: return false;
    }
    return x86_i128_store(sel, ins->result,
        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX);
}

static bool x86_unary(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    if (ins->value.operand < sel->core->value_count &&
        x86_i128_type(sel,
            sel->core->values[ins->value.operand].core_type_id, NULL)) {
        if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO)
            return x86_i128_load(sel, ins->value.operand,
                    MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) &&
                x86_i128_boolean_result(sel, ins->result, true);
        if (!x86_i128_type(sel, ins->core_type_id, NULL) ||
            !x86_i128_load(sel, ins->value.operand,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RDX)) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE) {
            if (!x86_1(sel, MINIC_X86_MI_NEG64_R,
                    x86_reg(MINIC_X86_REG_RAX, 64U)) ||
                !x86_ir(sel, MINIC_X86_MI_ADC64_IR, 0,
                    64U, MINIC_X86_REG_RDX) ||
                !x86_1(sel, MINIC_X86_MI_NEG64_R,
                    x86_reg(MINIC_X86_REG_RDX, 64U))) return false;
        } else if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT) {
            if (!x86_1(sel, MINIC_X86_MI_NOT64_R,
                    x86_reg(MINIC_X86_REG_RAX, 64U)) ||
                !x86_1(sel, MINIC_X86_MI_NOT64_R,
                    x86_reg(MINIC_X86_REG_RDX, 64U))) return false;
        } else return false;
        return x86_i128_store(sel, ins->result,
            MINIC_X86_REG_RAX, MINIC_X86_REG_RDX);
    }
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
    if (x86_x87_shape(sel, load ? ins->core_type_id :
            sel->core->values[payload].core_type_id)) {
        if (!x86_record_pointer(sel, address, MINIC_X86_REG_R11))
            return false;
        if (load)
            return x86_emit(sel, MINIC_X86_MI_FLDT_ADDRESS,
                    (MinicMachineOperand[]){x86_addr(MINIC_X86_REG_R11, 0)},
                    1U, vol ? MINIC_MACHINE_EFFECT_VOLATILE : 0U) &&
                x86_x87_store(sel, payload);
        return x86_x87_load(sel, payload) &&
            x86_emit(sel, MINIC_X86_MI_FSTPT_ADDRESS,
                (MinicMachineOperand[]){x86_addr(MINIC_X86_REG_R11, 0)},
                1U, vol ? MINIC_MACHINE_EFFECT_VOLATILE : 0U);
    }
    if (x86_i128_type(sel, load ? ins->core_type_id :
            sel->core->values[payload].core_type_id, NULL)) {
        if (!x86_record_pointer(sel, address, MINIC_X86_REG_R10))
            return false;
        if (load)
            return x86_2_volatile(sel, MINIC_X86_MI_MOV64_AR,
                    x86_addr(MINIC_X86_REG_R10, 0),
                    x86_reg(MINIC_X86_REG_RAX, 64U), vol) &&
                x86_2_volatile(sel, MINIC_X86_MI_MOV64_AR,
                    x86_addr(MINIC_X86_REG_R10, 8),
                    x86_reg(MINIC_X86_REG_RDX, 64U), vol) &&
                x86_i128_store(sel, ins->result,
                    MINIC_X86_REG_RAX, MINIC_X86_REG_RDX);
        return x86_i128_load(sel, payload,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) &&
            x86_2_volatile(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(MINIC_X86_REG_RAX, 64U),
                x86_addr(MINIC_X86_REG_R10, 0), vol) &&
            x86_2_volatile(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(MINIC_X86_REG_RDX, 64U),
                x86_addr(MINIC_X86_REG_R10, 8), vol);
    }
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
    bool sign = false, is_bool = false, ms, float_double = false, floating,
        wide;
    const size_t position = ins->value.parameter_index;
    floating = x86_float_shape(sel, ins->core_type_id, &float_double);
    wide = x86_i128_type(sel, ins->core_type_id, NULL);
    if (position >= plan->argument_count ||
        plan->locations == NULL ||
        (!floating && !wide && !x86_x87_shape(sel, ins->core_type_id) &&
         !x86_scalar_shape(sel, ins->core_type_id,
            &bytes, &sign, &is_bool)) ||
        (plan->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT &&
         plan->calling_convention != MINIC_CALLING_CONVENTION_MS_X64) ||
        plan->calling_convention !=
            minic_core_function_calling_convention(sel->core)) return false;
    where = &plan->locations[position];
    if (x86_x87_shape(sel, ins->core_type_id)) {
        if (plan->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT ||
            where->kind != MINIC_X86_ARG_X87_MEMORY || !where->on_stack ||
            where->stack_index > (INT32_MAX - 16U) / 8U)
            return false;
        return x86_1(sel, MINIC_X86_MI_FLDT_ADDRESS,
                x86_addr(MINIC_X86_REG_RBP,
                    (int64_t)(16U + where->stack_index * 8U))) &&
            x86_x87_store(sel, ins->result);
    }
    if (wide) {
        unsigned int first, second;
        if (plan->calling_convention !=
                MINIC_CALLING_CONVENTION_DEFAULT ||
            where->kind != MINIC_X86_ARG_I128_TWO_GP)
            return false;
        if (where->on_stack) {
            if (where->stack_index > (INT32_MAX - 24U) / 8U ||
                !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                    x86_addr(MINIC_X86_REG_RBP,
                        (int64_t)(16U + where->stack_index * 8U)),
                    x86_reg(MINIC_X86_REG_R10, 64U)) ||
                !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                    x86_addr(MINIC_X86_REG_RBP,
                        (int64_t)(24U + where->stack_index * 8U)),
                    x86_reg(MINIC_X86_REG_R11, 64U))) return false;
        } else {
            if (!x86_abi_gp_register(plan->calling_convention,
                    where->gp_index, &first) ||
                !x86_abi_gp_register(plan->calling_convention,
                    where->gp_index + 1U, &second)) return false;
            if (sel->frame->saves_incoming_gp) {
                const size_t distance =
                    sel->frame->incoming_gp_save_offset;
                if (distance > INT32_MAX -
                        (where->gp_index + 1U) * 8U ||
                    !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                        x86_addr(MINIC_X86_REG_RBP,
                            -(int64_t)(distance + where->gp_index * 8U)),
                        x86_reg(MINIC_X86_REG_R10, 64U)) ||
                    !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                        x86_addr(MINIC_X86_REG_RBP,
                            -(int64_t)(distance +
                                (where->gp_index + 1U) * 8U)),
                        x86_reg(MINIC_X86_REG_R11, 64U))) return false;
            } else if (!x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                    first, MINIC_X86_REG_R10) ||
                !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                    second, MINIC_X86_REG_R11)) return false;
        }
        return x86_i128_store(sel, ins->result,
            MINIC_X86_REG_R10, MINIC_X86_REG_R11);
    }
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

static bool x86_load_record_gp(X86CoreSelector *, size_t, size_t);
static bool x86_call_object_address(X86CoreSelector *, MinicCoreObjectId,
    size_t, unsigned int);
static bool x86_call_record_result(X86CoreSelector *, MinicCoreTypeId,
    MinicCoreObjectId);

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
        declared_type == MINIC_CORE_TYPE_INVALID) return false;
    if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
        const MinicCoreObjectId object = arg->value.object_id;
        const MinicCoreType *record = object < sel->core->object_count &&
            sel->core->objects != NULL ? x86_type(sel,
            sel->core->objects[object].core_type_id) : NULL;
        unsigned int first, second;
        if (convention != MINIC_CALLING_CONVENTION_DEFAULT ||
            record == NULL || record->kind != MINIC_CORE_TYPE_RECORD ||
            !record->has_layout ||
            !minic_x86_64_core_record_abi_compatible(
                sel->core->type_context,
                sel->core->objects[object].core_type_id, declared_type))
            return false;
        if (where->kind == MINIC_X86_ARG_RECORD_EMPTY)
            return record->size == 0U && where->record_bytes == 0U;
        if (where->record_bytes == 0U ||
            where->record_bytes != record->size ||
            (where->kind != MINIC_X86_ARG_RECORD_ONE_GP &&
             where->kind != MINIC_X86_ARG_RECORD_TWO_GP &&
             where->kind != MINIC_X86_ARG_RECORD_MEMORY) ||
            !x86_call_object_address(sel, object, stack_bytes,
                MINIC_X86_REG_R11)) return false;
        if (where->on_stack) {
            if (where->stack_index > INT32_MAX / 8U ||
                (stack_offset = where->stack_index * 8U) > stack_bytes ||
                where->record_bytes > stack_bytes - stack_offset)
                return false;
        } else stack_offset = 0U;
        if (where->kind == MINIC_X86_ARG_RECORD_MEMORY)
            return where->on_stack &&
                x86_record_move_bytes(sel, MINIC_X86_REG_R11, 0U,
                    MINIC_X86_REG_RSP, stack_offset,
                    where->record_bytes, false, false, false);
        if (where->kind == MINIC_X86_ARG_RECORD_ONE_GP) {
            if (where->record_bytes > 8U ||
                !x86_load_record_gp(sel, where->record_bytes, 0U))
                return false;
            if (!where->on_stack) {
                return x86_abi_gp_register(convention, where->gp_index,
                        &first) && x86_rr(sel, MINIC_X86_MI_MOV64_RR,
                        64U, MINIC_X86_REG_RAX, first);
            }
            return x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(MINIC_X86_REG_RAX, 64U),
                x86_addr(MINIC_X86_REG_RSP, (int64_t)stack_offset));
        }
        if (where->record_bytes <= 8U || where->record_bytes > 16U ||
            !x86_load_record_gp(sel, 8U, 0U)) return false;
        if (where->on_stack) {
            if (stack_offset > INT32_MAX - 8U ||
                !x86_2(sel, MINIC_X86_MI_MOV64_RA,
                    x86_reg(MINIC_X86_REG_RAX, 64U),
                    x86_addr(MINIC_X86_REG_RSP, (int64_t)stack_offset)))
                return false;
        } else if (!x86_abi_gp_register(convention, where->gp_index,
                &first) || !x86_abi_gp_register(convention,
                where->gp_index + 1U, &second) ||
            !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                MINIC_X86_REG_RAX, first)) return false;
        if (!x86_load_record_gp(sel, where->record_bytes - 8U, 8U))
            return false;
        if (where->on_stack)
            return x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(MINIC_X86_REG_RAX, 64U),
                x86_addr(MINIC_X86_REG_RSP,
                    (int64_t)(stack_offset + 8U)));
        return x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
            MINIC_X86_REG_RAX, second);
    }
    if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
        arg->value.value_id >= sel->core->value_count) return false;
    if (x86_x87_shape(sel, declared_type)) {
        MinicMachineOperand low, high;
        if (convention != MINIC_CALLING_CONVENTION_DEFAULT ||
            where->kind != MINIC_X86_ARG_X87_MEMORY || !where->on_stack ||
            !x86_x87_shape(sel,
                sel->core->values[arg->value.value_id].core_type_id) ||
            where->stack_index > INT32_MAX / 8U ||
            (stack_offset = where->stack_index * 8U) > stack_bytes ||
            stack_bytes - stack_offset < 16U ||
            stack_offset > INT32_MAX - 16U ||
            !x86_x87_slot(sel, arg->value.value_id, 8U, 0U, &low) ||
            !x86_x87_slot(sel, arg->value.value_id, 8U, 8U, &high))
            return false;
        return x86_2(sel, MINIC_X86_MI_MOV64_FRBP_R, low,
                x86_reg(MINIC_X86_REG_RAX, 64U)) &&
            x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(MINIC_X86_REG_RAX, 64U),
                x86_addr(MINIC_X86_REG_RSP, (int64_t)stack_offset)) &&
            x86_2(sel, MINIC_X86_MI_MOV64_FRBP_R, high,
                x86_reg(MINIC_X86_REG_RAX, 64U)) &&
            x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(MINIC_X86_REG_RAX, 64U),
                x86_addr(MINIC_X86_REG_RSP, (int64_t)(stack_offset + 8U)));
    }
    if (x86_i128_type(sel, declared_type, NULL)) {
        unsigned int first, second;
        if (convention != MINIC_CALLING_CONVENTION_DEFAULT ||
            where->kind != MINIC_X86_ARG_I128_TWO_GP ||
            !x86_i128_type(sel,
                sel->core->values[arg->value.value_id].core_type_id, NULL) ||
            !x86_i128_load(sel, arg->value.value_id,
                MINIC_X86_REG_R10, MINIC_X86_REG_R11)) return false;
        if (!where->on_stack)
            return x86_abi_gp_register(convention, where->gp_index,
                    &first) &&
                x86_abi_gp_register(convention, where->gp_index + 1U,
                    &second) &&
                x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                    MINIC_X86_REG_R10, first) &&
                x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                    MINIC_X86_REG_R11, second);
        if (where->stack_index > (INT32_MAX - 16U) / 8U ||
            (stack_offset = where->stack_index * 8U) > stack_bytes ||
            stack_bytes - stack_offset < 16U) return false;
        return x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(MINIC_X86_REG_R10, 64U),
                x86_addr(MINIC_X86_REG_RSP, (int64_t)stack_offset)) &&
            x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(MINIC_X86_REG_R11, 64U),
                x86_addr(MINIC_X86_REG_RSP, (int64_t)(stack_offset + 8U)));
    }
    if (where->kind != MINIC_X86_ARG_INTEGER &&
        where->kind != MINIC_X86_ARG_SSE) return false;
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

/* Extended80 Core calls have explicit numeric operation IDs.  They never
 * perform a symbol call, even when the Core callee uses a libgcc-style name;
 * conversely, an ordinary call to that name remains an ordinary ABI call.
 * All intermediate x87 stack traffic is represented by real x87 opcodes. */
static bool x86_x87_extended_call(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    const MinicCoreExtendedFloatOperation op =
        ins->value.call.extended_float_operation;
    const MinicCoreCallArgument *arguments;
    const MinicCoreType *left_type, *right_type = NULL;
    const MinicCoreType *result_type = x86_type(sel, ins->core_type_id);
    const MinicCoreValueId result = ins->result;
    MinicCoreValueId left, right = MINIC_CORE_VALUE_INVALID;
    MinicCoreTypeId left_id;
    bool source_ld, result_ld;
    size_t begin = ins->value.call.argument_begin;
    size_t count = ins->value.call.argument_count;
    const X86AuxBlocks *aux;
    MinicMachineOperand slot;
    if (op == MINIC_CORE_EXT_FLOAT_NONE ||
        ins->kind != MINIC_CORE_INSTRUCTION_CALL ||
        instruction_id >= sel->core->instruction_count ||
        sel->instruction_aux == NULL ||
        ins->value.call.callee_id >= sel->core->callee_count ||
        sel->core->callees == NULL ||
        result >= sel->core->value_count ||
        sel->core->values[result].core_type_id != ins->core_type_id ||
        begin > sel->core->call_argument_count ||
        count > sel->core->call_argument_count - begin ||
        (count != 1U && count != 2U) ||
        sel->core->call_arguments == NULL ||
        result_type == NULL) return false;
    arguments = sel->core->call_arguments + begin;
    if (arguments[0].kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
        (left = arguments[0].value.value_id) >= sel->core->value_count)
        return false;
    left_id = sel->core->values[left].core_type_id;
    left_type = x86_type(sel, left_id);
    if (left_type == NULL) return false;
    if (count == 2U) {
        if (arguments[1].kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            (right = arguments[1].value.value_id) >= sel->core->value_count)
            return false;
        right_type = x86_type(sel,
            sel->core->values[right].core_type_id);
        if (right_type == NULL) return false;
    }
    aux = &sel->instruction_aux[instruction_id];
    source_ld = x86_x87_shape(sel, left_id);
    result_ld = x86_x87_shape(sel, ins->core_type_id);
    if (count == 2U) {
        MinicX86MachineOpcode opcode = MINIC_X86_MI_INVALID;
        if (!source_ld || !x86_x87_shape(sel,
                sel->core->values[right].core_type_id) ||
            !x86_x87_load(sel, left) || !x86_x87_load(sel, right))
            return false;
        if (result_ld) {
            switch (op) {
            case MINIC_CORE_EXT_FLOAT_ADD: opcode = MINIC_X86_MI_FADDP_ST1; break;
            case MINIC_CORE_EXT_FLOAT_SUBTRACT: opcode = MINIC_X86_MI_FSUBRP_ST1; break;
            case MINIC_CORE_EXT_FLOAT_MULTIPLY: opcode = MINIC_X86_MI_FMULP_ST1; break;
            case MINIC_CORE_EXT_FLOAT_DIVIDE: opcode = MINIC_X86_MI_FDIVRP_ST1; break;
            default: return false;
            }
            return x86_0(sel, opcode) && x86_x87_store(sel, result);
        }
        if (result_type->kind != MINIC_CORE_TYPE_INTEGER ||
            !result_type->has_layout || result_type->size != 4U ||
            !result_type->value.integer.is_signed ||
            (op != MINIC_CORE_EXT_FLOAT_EQUAL &&
             op != MINIC_CORE_EXT_FLOAT_LESS &&
             op != MINIC_CORE_EXT_FLOAT_LESS_EQUAL)) return false;
        opcode = op == MINIC_CORE_EXT_FLOAT_EQUAL ? MINIC_X86_MI_SETZ8_R :
            op == MINIC_CORE_EXT_FLOAT_LESS ? MINIC_X86_MI_SETB8_R :
            MINIC_X86_MI_SETBE8_R;
        return x86_0(sel, MINIC_X86_MI_FXCH_ST1) &&
            x86_0(sel, MINIC_X86_MI_FUCOMIP_ST1) &&
            x86_0(sel, MINIC_X86_MI_FSTP_ST0) &&
            x86_1(sel, opcode, x86_reg(MINIC_X86_REG_RAX, 8U)) &&
            x86_1(sel, MINIC_X86_MI_SETNP8_R,
                x86_reg(MINIC_X86_REG_R10, 8U)) &&
            x86_rr(sel, MINIC_X86_MI_AND8_RR, 8U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX) &&
            x86_2(sel, MINIC_X86_MI_MOVZBQ_RR,
                x86_reg(MINIC_X86_REG_RAX, 8U),
                x86_reg(MINIC_X86_REG_RAX, 64U)) &&
            x86_result(sel, ins);
    }
    if (source_ld && result_ld) {
        return op == MINIC_CORE_EXT_FLOAT_NEGATE &&
            x86_x87_load(sel, left) &&
            x86_0(sel, MINIC_X86_MI_FCHS) &&
            x86_x87_store(sel, result);
    }
    if (result_ld && left_type->kind == MINIC_CORE_TYPE_FLOAT) {
        const bool float32 = left_type->has_layout &&
            left_type->size == 4U &&
            left_type->value.floating.format == MINIC_CORE_FLOAT_BINARY32;
        const bool float64 = left_type->has_layout &&
            left_type->size == 8U &&
            left_type->value.floating.format == MINIC_CORE_FLOAT_BINARY64;
        if ((!float32 || op != MINIC_CORE_EXT_FLOAT_FROM_BINARY32) &&
            (!float64 || op != MINIC_CORE_EXT_FLOAT_FROM_BINARY64))
            return false;
        return x86_slot(sel, left, &slot) &&
            x86_1(sel, float64 ? MINIC_X86_MI_FLDL_FRAME_RBP :
                MINIC_X86_MI_FLDS_FRAME_RBP, slot) &&
            x86_x87_store(sel, result);
    }
    if (source_ld && result_type->kind == MINIC_CORE_TYPE_FLOAT) {
        const bool float32 = result_type->has_layout &&
            result_type->size == 4U &&
            result_type->value.floating.format == MINIC_CORE_FLOAT_BINARY32;
        const bool float64 = result_type->has_layout &&
            result_type->size == 8U &&
            result_type->value.floating.format == MINIC_CORE_FLOAT_BINARY64;
        if ((!float32 || op != MINIC_CORE_EXT_FLOAT_TO_BINARY32) &&
            (!float64 || op != MINIC_CORE_EXT_FLOAT_TO_BINARY64))
            return false;
        return x86_x87_load(sel, left) && x86_slot(sel, result, &slot) &&
            x86_1(sel, float64 ? MINIC_X86_MI_FSTPL_FRAME_RBP :
                MINIC_X86_MI_FSTPS_FRAME_RBP, slot);
    }
    if (result_ld && left_type->kind == MINIC_CORE_TYPE_INTEGER &&
        left_type->has_layout &&
        (left_type->size == 4U || left_type->size == 8U)) {
        const bool unsigned_integer = !left_type->value.integer.is_signed;
        const bool from32 = left_type->size == 4U;
        if ((from32 && op != (unsigned_integer ?
                MINIC_CORE_EXT_FLOAT_FROM_UNSIGNED32 :
                MINIC_CORE_EXT_FLOAT_FROM_SIGNED32)) ||
            (!from32 && op != (unsigned_integer ?
                MINIC_CORE_EXT_FLOAT_FROM_UNSIGNED64 :
                MINIC_CORE_EXT_FLOAT_FROM_SIGNED64))) return false;
        if (unsigned_integer && !from32) {
            if (aux->first == SIZE_MAX || aux->second == SIZE_MAX ||
                !x86_core_load(sel, left, MINIC_X86_REG_R10) ||
                !x86_ir(sel, MINIC_X86_MI_SUB64_IR, 16, 64U,
                    MINIC_X86_REG_RSP) ||
                !x86_2(sel, MINIC_X86_MI_MOV64_RA,
                    x86_reg(MINIC_X86_REG_R10, 64U),
                    x86_addr(MINIC_X86_REG_RSP, 0)) ||
                !x86_1(sel, MINIC_X86_MI_FILDQ_ADDRESS,
                    x86_addr(MINIC_X86_REG_RSP, 0)) ||
                !x86_rr(sel, MINIC_X86_MI_TEST64_RR, 64U,
                    MINIC_X86_REG_R10, MINIC_X86_REG_R10) ||
                !x86_1(sel, MINIC_X86_MI_JNS_B, x86_block(aux->second)) ||
                !minic_machine_builder_add_successor(&sel->builder,
                    aux->first) ||
                !minic_machine_builder_add_successor(&sel->builder,
                    aux->second) ||
                !minic_machine_builder_set_fallthrough(&sel->builder,
                    aux->first) ||
                !minic_machine_builder_position_at(&sel->builder,
                    aux->first) ||
                !x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                    (int64_t)UINT64_C(0x43f0000000000000), 64U,
                    MINIC_X86_REG_R10) ||
                !x86_2(sel, MINIC_X86_MI_MOV64_RA,
                    x86_reg(MINIC_X86_REG_R10, 64U),
                    x86_addr(MINIC_X86_REG_RSP, 0)) ||
                !x86_1(sel, MINIC_X86_MI_FADDL_ADDRESS,
                    x86_addr(MINIC_X86_REG_RSP, 0)) ||
                !x86_1(sel, MINIC_X86_MI_JMP_B, x86_block(aux->second)) ||
                !minic_machine_builder_add_successor(&sel->builder,
                    aux->second) ||
                !minic_machine_builder_position_at(&sel->builder,
                    aux->second) ||
                !x86_ir(sel, MINIC_X86_MI_ADD64_IR, 16, 64U,
                    MINIC_X86_REG_RSP)) return false;
            return x86_x87_store(sel, result);
        }
        return x86_slot(sel, left, &slot) &&
            x86_1(sel, from32 && !unsigned_integer ?
                MINIC_X86_MI_FILDL_FRAME_RBP :
                MINIC_X86_MI_FILDQ_FRAME_RBP, slot) &&
            x86_x87_store(sel, result);
    }
    if (source_ld && result_type->kind == MINIC_CORE_TYPE_INTEGER &&
        result_type->has_layout &&
        (result_type->size == 4U || result_type->size == 8U)) {
        const bool unsigned_integer = !result_type->value.integer.is_signed;
        const bool to32 = result_type->size == 4U;
        if ((to32 && op != (unsigned_integer ?
                MINIC_CORE_EXT_FLOAT_TO_UNSIGNED32 :
                MINIC_CORE_EXT_FLOAT_TO_SIGNED32)) ||
            (!to32 && op != (unsigned_integer ?
                MINIC_CORE_EXT_FLOAT_TO_UNSIGNED64 :
                MINIC_CORE_EXT_FLOAT_TO_SIGNED64)) ||
            !x86_x87_load(sel, left) ||
            !x86_ir(sel, MINIC_X86_MI_SUB64_IR, 16, 64U,
                MINIC_X86_REG_RSP)) return false;
        if (!to32 && unsigned_integer) {
            if (aux->first == SIZE_MAX || aux->second == SIZE_MAX ||
                aux->third == SIZE_MAX ||
                !x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                    (int64_t)UINT64_C(0x43e0000000000000), 64U,
                    MINIC_X86_REG_R10) ||
                !x86_2(sel, MINIC_X86_MI_MOV64_RA,
                    x86_reg(MINIC_X86_REG_R10, 64U),
                    x86_addr(MINIC_X86_REG_RSP, 0)) ||
                !x86_1(sel, MINIC_X86_MI_FLDL_ADDRESS,
                    x86_addr(MINIC_X86_REG_RSP, 0)) ||
                !x86_0(sel, MINIC_X86_MI_FUCOMIP_ST1) ||
                !x86_1(sel, MINIC_X86_MI_JBE_B, x86_block(aux->second)) ||
                !minic_machine_builder_add_successor(&sel->builder,
                    aux->first) ||
                !minic_machine_builder_add_successor(&sel->builder,
                    aux->second) ||
                !minic_machine_builder_set_fallthrough(&sel->builder,
                    aux->first) ||
                !minic_machine_builder_position_at(&sel->builder,
                    aux->first) ||
                !x86_1(sel, MINIC_X86_MI_FISTTPQ_ADDRESS,
                    x86_addr(MINIC_X86_REG_RSP, 8)) ||
                !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                    x86_addr(MINIC_X86_REG_RSP, 8),
                    x86_reg(MINIC_X86_REG_RAX, 64U)) ||
                !x86_1(sel, MINIC_X86_MI_JMP_B, x86_block(aux->third)) ||
                !minic_machine_builder_add_successor(&sel->builder,
                    aux->third) ||
                !minic_machine_builder_position_at(&sel->builder,
                    aux->second) ||
                !x86_1(sel, MINIC_X86_MI_FLDL_ADDRESS,
                    x86_addr(MINIC_X86_REG_RSP, 0)) ||
                !x86_0(sel, MINIC_X86_MI_FSUBRP_ST1) ||
                !x86_1(sel, MINIC_X86_MI_FISTTPQ_ADDRESS,
                    x86_addr(MINIC_X86_REG_RSP, 8)) ||
                !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                    x86_addr(MINIC_X86_REG_RSP, 8),
                    x86_reg(MINIC_X86_REG_RAX, 64U)) ||
                !x86_2(sel, MINIC_X86_MI_BTS64_IR, x86_imm(63),
                    x86_reg(MINIC_X86_REG_RAX, 64U)) ||
                !x86_1(sel, MINIC_X86_MI_JMP_B, x86_block(aux->third)) ||
                !minic_machine_builder_add_successor(&sel->builder,
                    aux->third) ||
                !minic_machine_builder_position_at(&sel->builder,
                    aux->third)) return false;
        } else if (!x86_1(sel, MINIC_X86_MI_FISTTPQ_ADDRESS,
                x86_addr(MINIC_X86_REG_RSP, 8)) ||
            !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                x86_addr(MINIC_X86_REG_RSP, 8),
                x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
        return x86_ir(sel, MINIC_X86_MI_ADD64_IR, 16, 64U,
                MINIC_X86_REG_RSP) && x86_result(sel, ins);
    }
    return false;
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
    bool fp_double, record_result;
    if (begin > sel->core->call_argument_count ||
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
    record_result = result_type != NULL &&
        result_type->kind == MINIC_CORE_TYPE_RECORD;
    if (signature == NULL || signature->kind != MINIC_CORE_TYPE_FUNCTION ||
        signature->value.function.return_type != ins->core_type_id ||
        signature->value.function.parameter_count > count ||
        (signature->value.function.parameter_count != 0U &&
         signature->value.function.parameters == NULL) ||
        (!signature->value.function.is_variadic &&
         signature->value.function.parameter_count != count) ||
        result_type == NULL ||
        (record_result ?
            (ins->result != MINIC_CORE_VALUE_INVALID ||
             result_object >= sel->core->object_count ||
             sel->core->objects == NULL ||
             !minic_x86_64_core_record_abi_compatible(
                sel->core->type_context,
                sel->core->objects[result_object].core_type_id,
                ins->core_type_id)) :
            result_object != MINIC_CORE_OBJECT_INVALID) ||
        (!record_result && result_type->kind != MINIC_CORE_TYPE_VOID &&
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
    if (result_type->kind != MINIC_CORE_TYPE_VOID && !record_result &&
        !x86_i128_type(sel, ins->core_type_id, NULL) &&
        !x86_x87_shape(sel, ins->core_type_id) &&
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
            arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT &&
                arg->value.object_id < sel->core->object_count ?
                sel->core->objects[arg->value.object_id].core_type_id :
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
    if (record_result) {
        size_t bytes;
        if (plan->calling_convention != MINIC_CALLING_CONVENTION_DEFAULT)
            return false;
        if (minic_x86_64_core_memory_result_record(sel->core->type_context,
                ins->core_type_id, &bytes) &&
            !x86_call_object_address(sel, result_object, plan->stack_bytes,
                MINIC_X86_REG_RDI)) return false;
    }
    if (indirect) {
        const unsigned int options =
            minic_x86_64_target_codegen_options(sel->target);
        const bool external = (options &
            MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK) != 0U;
        const bool inlined = !external && (options &
            MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK) != 0U;
        /* Both Linux thunk modes retain the real target register as MachineIR
         * input. The inline nested-call/RET sequence is a sealed target
         * opcode, never a string synthesized by the C frontend. */
        if (((external || inlined) && plan->calling_convention ==
                 MINIC_CALLING_CONVENTION_MS_X64) ||
            !x86_core_load(sel, ins->value.indirect_call.callee,
                MINIC_X86_REG_R11) ||
            !(external ?
                x86_1(sel, MINIC_X86_MI_CALL_INDIRECT_THUNK_R11,
                    x86_symbol("__x86_indirect_thunk_r11",
                        MINIC_X86_MACHINE_RELOC_PC32)) :
              inlined ?
                x86_1(sel, MINIC_X86_MI_CALL_INLINE_THUNK_R11,
                    x86_reg(MINIC_X86_REG_R11, 64U)) :
                x86_1(sel, plan->calling_convention ==
                    MINIC_CALLING_CONVENTION_MS_X64 ?
                        MINIC_X86_MI_CALL_MS_R :
                        MINIC_X86_MI_CALL_SYSV_R,
                    x86_reg(MINIC_X86_REG_R11, 64U)))) return false;
    } else if (!x86_1(sel,
            plan->calling_convention == MINIC_CALLING_CONVENTION_MS_X64 ?
                MINIC_X86_MI_CALL_MS_SYMBOL : MINIC_X86_MI_CALL_SYSV_SYMBOL,
            x86_symbol(symbol, MINIC_X86_MACHINE_RELOC_PLT32))) return false;
    if (plan->stack_bytes != 0U &&
        !x86_ir(sel, MINIC_X86_MI_ADD64_IR,
            (int64_t)plan->stack_bytes, 64U, MINIC_X86_REG_RSP))
        return false;
    if (record_result)
        return x86_call_record_result(sel, ins->core_type_id, result_object);
    if (result_type->kind == MINIC_CORE_TYPE_VOID) return true;
    if (x86_i128_type(sel, ins->core_type_id, NULL))
        return x86_i128_store(sel, ins->result,
            MINIC_X86_REG_RAX, MINIC_X86_REG_RDX);
    if (x86_x87_shape(sel, ins->core_type_id))
        return plan->calling_convention == MINIC_CALLING_CONVENTION_DEFAULT &&
            x86_x87_store(sel, ins->result);
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

/* Transfer exactly one SysV INTEGER eightbyte from R11+offset to RAX.
 * Partial odd tails are assembled bytewise so a packed neighbor is never
 * read. The only scratch is R10; RCX is restored when CL is needed. */
static bool x86_load_record_gp(X86CoreSelector *sel,
    size_t bytes, size_t offset) {
    MinicX86MachineOpcode opcode;
    size_t i;
    if (bytes == 0U || bytes > 8U || offset > INT32_MAX - bytes)
        return false;
    switch (bytes) {
    case 1U: opcode = MINIC_X86_MI_MOVZBQ_AR; break;
    case 2U: opcode = MINIC_X86_MI_MOVZWQ_AR; break;
    case 4U: opcode = MINIC_X86_MI_MOV32_AR; break;
    case 8U: opcode = MINIC_X86_MI_MOV64_AR; break;
    default: opcode = MINIC_X86_MI_INVALID; break;
    }
    if (opcode != MINIC_X86_MI_INVALID)
        return x86_2(sel, opcode, x86_addr(MINIC_X86_REG_R11,
                (int64_t)offset), x86_reg(MINIC_X86_REG_RAX,
                bytes == 4U ? 32U : 64U));
    if (!x86_1(sel, MINIC_X86_MI_PUSH64_R,
            x86_reg(MINIC_X86_REG_RCX, 64U)) ||
        !x86_ir(sel, MINIC_X86_MI_MOV8_IR, 8, 8U,
            MINIC_X86_REG_RCX) ||
        !x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
            MINIC_X86_REG_RAX, MINIC_X86_REG_RAX)) return false;
    for (i = bytes; i != 0U; --i)
        if (!x86_1(sel, MINIC_X86_MI_SHL64_CL_R,
                x86_reg(MINIC_X86_REG_RAX, 64U)) ||
            !x86_2(sel, MINIC_X86_MI_MOVZBQ_AR,
                x86_addr(MINIC_X86_REG_R11, (int64_t)(offset + i - 1U)),
                x86_reg(MINIC_X86_REG_R10, 64U)) ||
            !x86_rr(sel, MINIC_X86_MI_OR64_RR, 64U,
                MINIC_X86_REG_R10, MINIC_X86_REG_RAX)) return false;
    return x86_1(sel, MINIC_X86_MI_POP64_R,
        x86_reg(MINIC_X86_REG_RCX, 64U));
}

/* The finalized frame may place over-aligned Core objects relative to RSP.
 * A call's outgoing reservation moves that anchor by exactly stack_bytes. */
static bool x86_call_object_address(X86CoreSelector *sel,
    MinicCoreObjectId object, size_t stack_bytes, unsigned int reg) {
    if (sel->frame->objects_from_aligned_rsp) {
        size_t offset;
        if (object >= sel->frame->object_count ||
            sel->frame->object_offsets == NULL ||
            sel->frame->object_offsets[object] > INT32_MAX - stack_bytes)
            return false;
        offset = sel->frame->object_offsets[object] + stack_bytes;
        return x86_2(sel, MINIC_X86_MI_LEA64_AR,
            x86_addr(MINIC_X86_REG_RSP, (int64_t)offset),
            x86_reg(reg, 64U));
    }
    return x86_object_address(sel, object, reg);
}

static bool x86_call_record_result(X86CoreSelector *sel,
    MinicCoreTypeId return_type, MinicCoreObjectId object) {
    const MinicCoreType *type = x86_type(sel, return_type);
    size_t bytes;
    if (type == NULL || type->kind != MINIC_CORE_TYPE_RECORD ||
        !type->has_layout || object >= sel->core->object_count ||
        !minic_x86_64_core_record_abi_compatible(
            sel->core->type_context, return_type,
            sel->core->objects[object].core_type_id)) return false;
    if (minic_x86_64_core_memory_result_record(sel->core->type_context,
            return_type, &bytes)) return bytes == type->size;
    if (!x86_object_address(sel, object, MINIC_X86_REG_R10))
        return false;
    if (minic_x86_64_core_one_gp_record(sel->core->type_context,
            return_type, &bytes))
        return x86_store_record_gp(sel, bytes, 0U);
    if (!minic_x86_64_core_two_gp_record(sel->core->type_context,
            return_type) || type->size <= 8U || type->size > 16U ||
        !x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RAX, 64U),
            x86_addr(MINIC_X86_REG_R10, 0)) ||
        !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
            MINIC_X86_REG_RDX, MINIC_X86_REG_RAX)) return false;
    return x86_store_record_gp(sel, type->size - 8U, 8U);
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
        !minic_x86_64_core_record_abi_compatible(
            sel->core->type_context,
            sel->core->objects[object].core_type_id,
            minic_core_function_parameter_types(sel->core)[position]) ||
        (record = x86_type(sel,
            sel->core->objects[object].core_type_id)) == NULL ||
        record->kind != MINIC_CORE_TYPE_RECORD || !record->has_layout)
        return false;
    loc = &plan->locations[position];
    payload = loc->record_bytes;
    /* SysV AMD64 passes a zero-size GNU record with no GP register, stack
     * slot or memory write. Its Core parameter object has a real type, but
     * never requires a fictitious frame allocation or physical transfer. */
    if (loc->kind == MINIC_X86_ARG_RECORD_EMPTY)
        return record->size == 0U && payload == 0U;
    if (sel->frame->object_storage.objects == NULL ||
        record->size > sel->frame->object_storage.objects[object].extent)
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
    if (!minic_multi_direct_record_copy_extent(sel->core, ins, &bytes))
        return false;
    /* Pointer expressions have already been evaluated into Core SSA values.
     * Copying an empty record performs no machine memory access. */
    if (bytes == 0U) return true;
    if (!x86_record_pointer(sel,
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
            &plan, &source, &offset, &bytes)) return false;
    if (bytes == 0U) return true;
    if (sel->frame->object_storage.objects == NULL ||
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
        /* An early head global without direct physical address users must
         * retain GNU's signed 32-bit absolute link-VMA `movq $sym,%rax`.
         * RIP-relative LEA would change the address before paging is live. */
        if (sel->frame->kernel_head_link_addresses &&
            (sel->frame->kernel_head_direct_data_globals == NULL ||
             !sel->frame->kernel_head_direct_data_globals[
                 ins->value.global_id]))
            return minic_core_inline_asm_symbol_valid(name) &&
                x86_2(sel, MINIC_X86_MI_MOV64_IMM32S_SYMBOL_R,
                    x86_symbol(name, MINIC_X86_MACHINE_RELOC_ABS32S),
                    x86_reg(MINIC_X86_REG_RAX, 64U)) &&
                x86_result(sel, ins);
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

/* Preserve the Source-owned occurrence and the already sealed x86 form.
 * This is a genuine physical MachineIR source effect, not a selected
 * instruction whose payload is a pre-rendered assembly line. The producer
 * must prove every declared register clobber before the builder can append
 * the instruction. Larger Linux asm families require operand legalization. */
static bool x86_source_zero_barrier(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    MinicCoreInlineAsmId id;
    MinicMachineRegisterEffect flags = {0};
    const MinicMachineRegisterEffect *implicit = NULL;
    size_t implicit_count = 0U;
    unsigned int form, effects = 0U;
    if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM)
        id = ins->value.inline_asm_id;
    else if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
        if (ins->value.structured_inline_asm.operand_count != 0U)
            return false;
        id = ins->value.structured_inline_asm.inline_asm_id;
    } else return false;
    if (id >= sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL) return false;
    source = &sel->core->inline_asms[id];
    if (!source->is_volatile || source->is_goto ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->template_text == NULL) return false;
    if (source->target_machine_form_id == 0U &&
        source->target_machine_form_variant == 0U &&
        source->template_length == 0U &&
        source->register_clobber_count == 0U &&
        !source->has_memory_clobber)
        return minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, 0U, NULL, 0U, NULL, 0U, false, false);
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_ZERO_BARRIER ||
        source->operand_plan_owner !=
            MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        (form = minic_x86_64_zero_barrier_form_exact(
            source->template_text, source->template_length)) == 0U ||
        source->target_machine_form_variant != form ||
        ((form <= 4U || form >= 8U) &&
         !source->has_memory_clobber)) return false;
    if (form == 1U) {
        const MinicCoreInlineAsmRegisterClobber *clobber;
        if (source->register_clobber_count != 1U ||
            source->register_clobbers == NULL) return false;
        clobber = &source->register_clobbers[0];
        if (clobber->name == NULL || clobber->name_length != 2U ||
            memcmp(clobber->name, "cc", 2U) != 0) return false;
        flags.reg = (MinicMachineRegister){
            0U, 64U, MINIC_MACHINE_REGISTER_FLAGS};
        flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
        implicit = &flags;
        implicit_count = 1U;
        effects = MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    } else if (source->register_clobber_count != 0U) return false;
    if (form >= 8U) {
        /* CLI/STI and STI;HLT alter architectural IF in RFLAGS. Keep that
         * physical clobber explicit; GNU 'memory' is ordering, not an
         * invented hardware memory operation. */
        flags.reg = (MinicMachineRegister){
            0U, 64U, MINIC_MACHINE_REGISTER_FLAGS};
        flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
        implicit = &flags;
        implicit_count = 1U;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source, effects, NULL, 0U, implicit, implicit_count, false, false);
}

/* Reattach the original ZSTD alignment/NOP source family to physical
 * MachineIR. All five spellings have no operand, clobber or register effect;
 * the independent machine validator checks the finite grammar again. */
static bool x86_source_zstd_padding(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    MinicCoreInlineAsmId id;
    unsigned int form;
    if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM)
        id = ins->value.inline_asm_id;
    else if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM &&
        ins->value.structured_inline_asm.operand_count == 0U)
        id = ins->value.structured_inline_asm.inline_asm_id;
    else return false;
    if (sel->core->inline_asms == NULL ||
        id >= sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[id];
    form = minic_x86_64_linux_zstd_padding_form(
        source->template_text, source->template_length);
    if (form == 0U || !source->is_volatile || source->is_goto ||
        source->source_asm_unique_id == 0U ||
        source->target_machine_form_id != MINIC_X86_MACHINE_ASM_ZSTD_PADDING ||
        source->target_machine_form_variant != form ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID)
        return false;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source, 0U, NULL, 0U, NULL, 0U, false, false);
}

/* Exhaust the exact Linux privileged zero-input inventory that is not
 * already owned by ZERO_BARRIER's CLI/STI/STI-HLT forms. No operand-less
 * opaque Core instruction is allowed to supply arbitrary ISA bytes. */
static bool x86_source_privileged_zero(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    MinicMachineRegisterEffect implicit = {0};
    const MinicMachineRegisterEffect *effects_regs = NULL;
    size_t effects_count = 0U;
    unsigned int form, effects = 0U;
    if (!sel || !ins ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != 0U ||
        !sel->core->inline_asms ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    form = source->target_machine_form_variant;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_PRIVILEGED_ZERO ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        form < 4U || form > 10U ||
        !source->is_volatile || source->is_goto ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        ((form == 5U || form == 6U) &&
            (!source->has_memory_clobber ||
             !source->implicit_memory_effect)) ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,form,
            source->template_text,source->template_length)) return false;
    if (form == 5U) /* WBINVD flushes the entire write-back cache. */
        effects = MINIC_MACHINE_EFFECT_MEMORY_READ |
                  MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    else if (form == 8U || form == 9U) {
        effects = MINIC_MACHINE_EFFECT_MEMORY_WRITE; /* fixed port I/O */
        implicit.reg = (MinicMachineRegister){
            MINIC_X86_REG_RAX,8U,MINIC_MACHINE_REGISTER_GENERAL};
        implicit.access = MINIC_MACHINE_REGISTER_READ;
        effects_regs = &implicit;
        effects_count = 1U;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source,effects,NULL,0U,effects_regs,effects_count,false,false);
}

/* The original Linux mul_u64_u64_div_u64 and KASLR product both use the
 * unsigned 128-bit RDX:RAX result of MULQ.  They share one physical operand
 * assignment: accumulator RAX, two non-RDX scratch inputs, and explicitly
 * bound output registers.  Keep the GNU source template in SOURCE_ASM and
 * select the lvalue traffic as ordinary verified machine instructions. */
static bool x86_asm_u64_lvalue(const X86CoreSelector *sel,
    MinicCoreValueId value) {
    const MinicCoreType *pointer, *pointee;
    MinicCoreTypeId pointee_id;
    if (value >= sel->core->value_count ||
        (pointer = x86_type(sel,
            sel->core->values[value].core_type_id)) == NULL ||
        pointer->kind != MINIC_CORE_TYPE_POINTER ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[value].core_type_id, &pointee_id) ||
        (pointee = x86_type(sel, pointee_id)) == NULL ||
        !pointee->has_layout || pointee->size != 8U ||
        pointee->kind != MINIC_CORE_TYPE_INTEGER ||
        pointee->value.integer.is_signed ||
        pointee->value.integer.is_bool) return false;
    return true;
}

static bool x86_asm_u64_input(const X86CoreSelector *sel,
    MinicCoreValueId value) {
    const MinicCoreType *type;
    return value < sel->core->value_count &&
        (type = x86_type(sel,
            sel->core->values[value].core_type_id)) != NULL &&
        type->kind == MINIC_CORE_TYPE_INTEGER && type->has_layout &&
        type->size == 8U && !type->value.integer.is_signed &&
        !type->value.integer.is_bool;
}

static bool x86_asm_u32_input(const X86CoreSelector *sel,
    MinicCoreValueId value) {
    const MinicCoreType *type;
    return value < sel->core->value_count &&
        (type = x86_type(sel,
            sel->core->values[value].core_type_id)) != NULL &&
        type->kind == MINIC_CORE_TYPE_INTEGER && type->has_layout &&
        type->size == 4U && !type->value.integer.is_bool;
}

static bool x86_asm_u32_lvalue(const X86CoreSelector *sel,
    MinicCoreValueId value) {
    MinicCoreTypeId pointee_id;
    const MinicCoreType *type;
    return value < sel->core->value_count &&
        minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[value].core_type_id, &pointee_id) &&
        (type = x86_type(sel, pointee_id)) != NULL &&
        type->kind == MINIC_CORE_TYPE_INTEGER && type->has_layout &&
        type->size == 4U && !type->value.integer.is_bool;
}

/* Linux native_cpuid and BusyBox SHA CPUID share the complete four-register
 * hardware overwrite.  Four or two explicitly tied inputs are loaded in
 * the same architectural EAX/EBX/ECX/EDX bank.  Preserve RBX, a SysV
 * callee-saved register, with real MachineIR PUSH/POP around SOURCE_ASM. */
static bool x86_source_cpuid_matched(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RBX,
        MINIC_X86_REG_RCX, MINIC_X86_REG_RDX
    };
    static char outputs[][3] = {"=a", "=b", "=c", "=d"};
    static char inputs[][2] = {"0", "1", "2", "3"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[8];
    size_t count, i;
    bool boot;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    count = ins->value.structured_inline_asm.operand_count;
    boot = source->target_machine_form_id == MINIC_X86_MACHINE_ASM_BOOT_FLAGS &&
        source->target_machine_form_variant == 3U;
    if ((!boot && source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_CPUID_MATCHED) ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        (count != 6U && count != 8U) ||
        source->target_machine_form_variant != (boot ? 3U : count) ||
        source->has_memory_clobber != (count == 6U && !boot) ||
        !source->is_volatile || source->is_goto ||
        source->register_clobber_count != 0U ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length)) return false;
    for (i = 0U; i < count; ++i) {
        const bool output = i < 4U;
        const size_t reg_index = output ? i :
            (count == 6U && i == 5U ? 2U : i - 4U);
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write ||
            roles[i].has_tied_output != !output ||
            (!output && roles[i].tied_output_index != reg_index) ||
            !(output ? x86_asm_u32_lvalue(sel, roles[i].value) :
                x86_asm_u32_input(sel, roles[i].value))) return false;
        bindings[i] = x86_reg(regs[reg_index], 32U);
        bindings[i].value.physical.access = output ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
        bindings[i].asm_constraint = output ? outputs[reg_index] :
            inputs[reg_index];
    }
    if (!x86_1(sel, MINIC_X86_MI_PUSH64_R,
            x86_reg(MINIC_X86_REG_RBX, 64U))) return false;
    for (i = 4U; i < count; ++i) {
        const size_t reg_index = count == 6U && i == 5U ? 2U : i - 4U;
        if (!x86_core_load(sel, roles[i].value, regs[reg_index]))
            return false;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, 0U, bindings, count, NULL, 0U, false, false))
        return false;
    for (i = 0U; i < 4U; ++i)
        if (!x86_core_load(sel, roles[i].value, MINIC_X86_REG_R11) ||
            !x86_2(sel, MINIC_X86_MI_MOV32_RA,
                x86_reg(regs[i], 32U),
                x86_addr(MINIC_X86_REG_R11, 0))) return false;
    return x86_1(sel, MINIC_X86_MI_POP64_R,
        x86_reg(MINIC_X86_REG_RBX, 64U));
}

/* Full carry-chain of BusyBox TLS GNU matched =rm, with a true memory m
 * input.  The memory lvalue address is held in R11, never recast as a
 * scalar register and never allowed to alias the RDX clobber. */
static bool x86_source_mulacc_matched(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[3] = {
        MINIC_X86_REG_R8, MINIC_X86_REG_R9, MINIC_X86_REG_RCX
    };
    static char ties[][2] = {"0", "1", "2"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[8];
    MinicMachineRegisterEffect effects[2] = {0};
    MinicCoreTypeId pointee;
    size_t i;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 8U) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_MULACC_MATCHED ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->target_machine_form_variant != 1U ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->register_clobber_count != 2U ||
        source->register_clobbers == NULL ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length)) return false;
    for (i = 0U; i < 2U; ++i) {
        const MinicCoreInlineAsmRegisterClobber *c =
            &source->register_clobbers[i];
        if (c->name == NULL ||
            !((c->name_length == 2U &&
               memcmp(c->name, "cc", 2U) == 0) ||
              (c->name_length == 2U &&
               memcmp(c->name, "dx", 2U) == 0)) ||
            (i == 1U && c->name_length ==
                source->register_clobbers[0].name_length &&
             memcmp(c->name, source->register_clobbers[0].name,
                 c->name_length) == 0)) return false;
    }
    for (i = 0U; i < 8U; ++i) {
        const bool output = i < 3U, tied = i >= 3U && i < 6U;
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                i == 7U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write ||
            roles[i].has_tied_output != tied ||
            (tied && roles[i].tied_output_index != i - 3U)) return false;
        if (output && !x86_asm_u64_lvalue(sel, roles[i].value)) return false;
        if (i >= 3U && i < 7U &&
            !x86_asm_u64_input(sel, roles[i].value)) return false;
    }
    if (!minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[roles[7].value].core_type_id, &pointee))
        return false;
    {
        /* MEMORY_INPUT is an address SSA, unlike a read-only scalar SSA.
         * It must instead point to one unsigned 8-byte object. */
        const MinicCoreType *type = x86_type(sel, pointee);
        if (type == NULL || type->kind != MINIC_CORE_TYPE_INTEGER ||
            !type->has_layout || type->size != 8U ||
            type->value.integer.is_signed || type->value.integer.is_bool)
            return false;
    }
    if (!x86_record_pointer(sel, roles[7].value, MINIC_X86_REG_R11))
        return false;
    for (i = 3U; i < 6U; ++i)
        if (!x86_core_load(sel, roles[i].value, regs[i - 3U]))
            return false;
    if (!x86_core_load(sel, roles[6].value, MINIC_X86_REG_RAX))
        return false;
    for (i = 0U; i < 3U; ++i) {
        bindings[i] = x86_reg(regs[i], 64U);
        bindings[i].asm_constraint = "=rm";
        bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        bindings[i + 3U] = x86_reg(regs[i], 64U);
        bindings[i + 3U].asm_constraint = ties[i];
        bindings[i + 3U].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    bindings[6] = x86_reg(MINIC_X86_REG_RAX, 64U);
    bindings[6].asm_constraint = "a";
    bindings[6].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    bindings[7] = x86_addr(MINIC_X86_REG_R11, 0);
    bindings[7].asm_constraint = "m";
    effects[0].reg = (MinicMachineRegister){
        MINIC_X86_REG_RDX, 64U, MINIC_MACHINE_REGISTER_GENERAL};
    effects[0].access = MINIC_MACHINE_REGISTER_CLOBBER;
    effects[1].reg = (MinicMachineRegister){
        0U, 64U, MINIC_MACHINE_REGISTER_FLAGS};
    effects[1].access = MINIC_MACHINE_REGISTER_CLOBBER;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, MINIC_MACHINE_EFFECT_MEMORY_READ,
            bindings, 8U, effects, 2U, false, false)) return false;
    for (i = 0U; i < 3U; ++i)
        if (!x86_core_load(sel, roles[i].value, MINIC_X86_REG_R10) ||
            !x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(regs[i], 64U),
                x86_addr(MINIC_X86_REG_R10, 0))) return false;
    return true;
}

/* The Linux four-limb add/sub carry chain, single in-place decrement,
 * and 256-bit modulus subtraction all share GNU =r plus matched N inputs.
 * Output lvalues are pointers to either 64-bit scalars or 64-bit pointers;
 * their contents are stored only after the complete source-ASM instruction
 * sequence.  The target reserves R11 for those lvalue-address stores, and
 * binds operands 0..3 to the same verified physical register bank as the
 * historical native implementation. */
static bool x86_asm_gp64_lvalue(const X86CoreSelector *sel,
    MinicCoreValueId value) {
    MinicCoreTypeId pointee_id;
    const MinicCoreType *type;
    if (value >= sel->core->value_count ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[value].core_type_id, &pointee_id) ||
        (type = x86_type(sel, pointee_id)) == NULL ||
        !type->has_layout || type->size != 8U) return false;
    return type->kind == MINIC_CORE_TYPE_POINTER ||
        (type->kind == MINIC_CORE_TYPE_INTEGER &&
         !type->value.integer.is_bool);
}

static bool x86_asm_gp64_input(const X86CoreSelector *sel,
    MinicCoreValueId value, bool *narrow) {
    const MinicCoreType *type;
    if (value >= sel->core->value_count ||
        (type = x86_type(sel,
            sel->core->values[value].core_type_id)) == NULL ||
        !type->has_layout ||
        (type->kind != MINIC_CORE_TYPE_POINTER &&
         type->kind != MINIC_CORE_TYPE_INTEGER) ||
        (type->kind == MINIC_CORE_TYPE_INTEGER &&
         type->value.integer.is_bool)) return false;
    if (narrow != NULL) *narrow = false;
    if (type->size == 8U) return true;
    if (type->size != 4U ||
        type->kind != MINIC_CORE_TYPE_INTEGER ||
        type->value.integer.is_signed) return false;
    if (narrow != NULL) *narrow = true;
    return true;
}

static bool x86_source_matched_memory(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[4] = {
        MINIC_X86_REG_R8, MINIC_X86_REG_R9,
        MINIC_X86_REG_R10, MINIC_X86_REG_RCX
    };
    static char ties[][2] = {"0", "1", "2"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[7];
    MinicMachineRegisterEffect flags = {0};
    size_t count, outputs, inputs, i;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    count = ins->value.structured_inline_asm.operand_count;
    outputs = count == 7U ? 4U : count == 5U ? 3U :
        count == 2U ? 1U : 0U;
    if (outputs == 0U ||
        source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_MATCHED_MEMORY ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->target_machine_form_variant != count ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length)) return false;
    inputs = count - outputs;
    for (i = 0U; i < count; ++i) {
        const bool output = i < outputs;
        bool narrow = false;
        const size_t reg_index = output ? i : i - outputs;
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write ||
            roles[i].has_tied_output != !output ||
            (!output && roles[i].tied_output_index != reg_index) ||
            !(output ? x86_asm_gp64_lvalue(sel, roles[i].value) :
                x86_asm_gp64_input(sel, roles[i].value, &narrow)))
            return false;
        bindings[i] = x86_reg(regs[reg_index], 64U);
        bindings[i].asm_constraint = output ? "=r" : ties[reg_index];
        bindings[i].value.physical.access = output ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    }
    for (i = 0U; i < inputs; ++i) {
        bool narrow = false;
        if (!x86_asm_gp64_input(sel, roles[outputs + i].value, &narrow) ||
            !x86_core_load(sel, roles[outputs + i].value, regs[i]))
            return false;
        /* A 32-bit source input such as the exact GNU 0xffffffff modulus
         * limb must enter the tied 64-bit physical register zero-extended. */
        if (narrow && !x86_rr(sel, MINIC_X86_MI_MOV32_RR,
                32U, regs[i], regs[i])) return false;
    }
    flags.reg = (MinicMachineRegister){
        0U, 64U, MINIC_MACHINE_REGISTER_FLAGS};
    flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings, count, &flags, 1U, false, false)) return false;
    for (i = 0U; i < outputs; ++i)
        if (!x86_core_load(sel, roles[i].value, MINIC_X86_REG_R11) ||
            !x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(regs[i], 64U),
                x86_addr(MINIC_X86_REG_R11, 0))) return false;
    return true;
}

/* Linux __ffs/__fls/ffz and the tied 32-bit fls family use the same native
 * BSF/BSR ISA bank, not a C integer substitute.  Keep the exact source form
 * ID as a typed MachineIR origin while selecting EAX/RAX and R10D/R10.
 * The +r GNU 32-bit lvalue in BSR64_READWRITE is loaded through its Core
 * output address, but the actual BSR consumes the full-width RAX register. */
static bool x86_source_bitscan_matched(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect flags = {0};
    MinicX86BitScanForm form;
    size_t count, i;
    unsigned int width;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    form = (MinicX86BitScanForm)source->target_machine_form_variant;
    count = ins->value.structured_inline_asm.operand_count;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_BITSCAN_MATCHED ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        form < MINIC_X86_BIT_SCAN_BSF64 ||
        form > MINIC_X86_BIT_SCAN_BSR64_READWRITE ||
        count != (form == MINIC_X86_BIT_SCAN_BSF32_TIED ||
                  form == MINIC_X86_BIT_SCAN_BSR32_TIED ? 3U : 2U) ||
        source->is_goto || source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length)) return false;
    width = form == MINIC_X86_BIT_SCAN_BSF64 ||
            form == MINIC_X86_BIT_SCAN_BSR64 ||
            form == MINIC_X86_BIT_SCAN_BSR64_READWRITE ? 64U : 32U;
    for (i = 0U; i < count; ++i) {
        const bool output = i == 0U;
        const bool readwrite = output &&
            form == MINIC_X86_BIT_SCAN_BSR64_READWRITE;
        const bool tie = i == 2U;
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != (output ? readwrite ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write != readwrite ||
            roles[i].has_tied_output != tie ||
            (tie && roles[i].tied_output_index != 0U)) return false;
        if (output) {
            if (!(width == 64U && !readwrite ?
                x86_asm_u64_lvalue(sel, roles[i].value) :
                x86_asm_u32_lvalue(sel, roles[i].value))) return false;
        } else if (!(i == 2U || width == 32U ?
                    x86_asm_u32_input(sel, roles[i].value) :
                    x86_asm_u64_input(sel, roles[i].value))) return false;
    }
    if (form == MINIC_X86_BIT_SCAN_BSF32_TIED ||
        form == MINIC_X86_BIT_SCAN_BSR32_TIED) {
        if (!x86_core_load(sel, roles[2].value, MINIC_X86_REG_RAX) ||
            !x86_rr(sel, MINIC_X86_MI_MOV32_RR,
                32U, MINIC_X86_REG_RAX, MINIC_X86_REG_RAX)) return false;
    } else if (form == MINIC_X86_BIT_SCAN_BSR64_READWRITE) {
        if (!x86_core_load(sel, roles[0].value, MINIC_X86_REG_R11) ||
            !x86_2(sel, MINIC_X86_MI_MOV32_AR,
                x86_addr(MINIC_X86_REG_R11, 0),
                x86_reg(MINIC_X86_REG_RAX, 32U))) return false;
    }
    if (!x86_core_load(sel, roles[1].value, MINIC_X86_REG_R10))
        return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX, width);
    bindings[0].asm_constraint = form ==
        MINIC_X86_BIT_SCAN_BSR64_READWRITE ? "+r" : "=r";
    bindings[0].value.physical.access =
        form == MINIC_X86_BIT_SCAN_BSR64_READWRITE ?
        (MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE) :
        MINIC_MACHINE_REGISTER_WRITE;
    bindings[1] = x86_reg(MINIC_X86_REG_R10, width);
    bindings[1].asm_constraint = width == 64U ? "rm" : "r";
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    if (count == 3U) {
        bindings[2] = x86_reg(MINIC_X86_REG_RAX, 32U);
        bindings[2].asm_constraint = "0";
        bindings[2].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    flags.reg = (MinicMachineRegister){
        0U, 64U, MINIC_MACHINE_REGISTER_FLAGS};
    flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, 0U, bindings, count, &flags, 1U, false, false))
        return false;
    if (!x86_core_load(sel, roles[0].value, MINIC_X86_REG_R11))
        return false;
    return x86_2(sel, width == 64U &&
        form != MINIC_X86_BIT_SCAN_BSR64_READWRITE ?
            MINIC_X86_MI_MOV64_RA : MINIC_X86_MI_MOV32_RA,
        x86_reg(MINIC_X86_REG_RAX,
            width == 64U && form != MINIC_X86_BIT_SCAN_BSR64_READWRITE ?
                64U : 32U),
        x86_addr(MINIC_X86_REG_R11, 0));
}

/* ALTERNATIVE() prefetchw has a source-level constant feature expression and
 * an actual GNU memory operand.  Its target-owned typed form seals the two
 * ELF altinstruction sections as well as the two hardware prefetches. */
static bool x86_source_prefetchw_alt(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreInstruction *constant;
    MinicMachineOperand bindings[2];
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 2U)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_PREFETCHW_ALT ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->target_machine_form_variant != 1U ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length) ||
        roles[0].operand_index != 0U ||
        roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        roles[1].operand_index != 1U ||
        roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        roles[0].has_fixed_register_binding ||
        roles[1].has_fixed_register_binding ||
        roles[0].early_clobber || roles[1].early_clobber ||
        roles[0].physical_read_write || roles[1].physical_read_write ||
        roles[0].has_tied_output || roles[1].has_tied_output ||
        roles[0].value >= sel->core->value_count ||
        roles[1].value >= sel->core->value_count ||
        !minic_core_function_value_is_kind(sel->core,
            roles[0].value, MINIC_CORE_TYPE_INTEGER) ||
        !minic_core_function_value_is_kind(sel->core,
            roles[1].value, MINIC_CORE_TYPE_POINTER) ||
        sel->core->values[roles[0].value].definition >=
            sel->core->instruction_count) return false;
    constant = &sel->core->instructions[
        sel->core->values[roles[0].value].definition];
    if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        constant->result != roles[0].value ||
        constant->value.integer_value != 0 ||
        !x86_core_load(sel, roles[1].value, MINIC_X86_REG_R10)) return false;
    bindings[0] = x86_imm(0);
    bindings[0].asm_constraint = "i";
    bindings[1] = x86_addr(MINIC_X86_REG_R10, 0);
    bindings[1].asm_constraint = "m";
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source, MINIC_MACHINE_EFFECT_MEMORY_READ,
        bindings, 2U, NULL, 0U, false, false);
}

/* The full Linux cmpxchg16b contract consists of two sealed GNU forms:
 * locked 128-bit CAS with ZF materialized to a C bool, and the per-CPU
 * alternative with a genuine emulation CALL and 14-byte GNU replacement.
 * Both use the exact hardware RDX:RAX expected and RCX:RBX desired pairs.
 * Reserve a full 16-byte stack slot for RBX so the alternative's call site
 * preserves the SysV 16-byte caller alignment. */
static bool x86_source_cmpxchg128(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[6];
    MinicMachineRegisterEffect implicit[2] = {0};
    MinicCoreTypeId pointee;
    const MinicCoreType *bool_type;
    size_t count, at, i;
    bool alt;
    unsigned int target_effects;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    alt = source->target_machine_form_variant == 2U;
    at = alt ? 0U : 1U;
    count = alt ? 5U : 6U;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_CMPXCHG128 ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        (source->target_machine_form_variant != 1U && !alt) ||
        ins->value.structured_inline_asm.operand_count != count ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->register_clobber_count != (alt ? 1U : 0U) ||
        (alt && (source->register_clobbers == NULL ||
            source->register_clobbers[0].name == NULL ||
            source->register_clobbers[0].name_length != 3U ||
            memcmp(source->register_clobbers[0].name, "rsi", 3U) != 0)) ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length)) return false;
    for (i = 0U; i < count; ++i) {
        const bool readwrite = i >= at && i <= at + 2U;
        const MinicCoreStructuredInlineAsmOperandKind kind =
            !alt && i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            i == at ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i == at + 1U || i == at + 2U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != kind || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].has_fixed_register_binding ||
            roles[i].physical_read_write != readwrite ||
            roles[i].has_tied_output) return false;
        if (i == at) {
            if (!minic_lp64_core_pointer_pointee(
                    sel->core->type_context,
                    sel->core->values[roles[i].value].core_type_id,
                    &pointee) || !x86_i128_type(sel, pointee, NULL))
                return false;
        } else if (!alt && i == 0U) {
            if (!minic_lp64_core_pointer_pointee(
                    sel->core->type_context,
                    sel->core->values[roles[i].value].core_type_id,
                    &pointee) ||
                (bool_type = x86_type(sel, pointee)) == NULL ||
                bool_type->kind != MINIC_CORE_TYPE_INTEGER ||
                !bool_type->has_layout || bool_type->size != 1U ||
                !bool_type->value.integer.is_bool) return false;
        } else if (i == at + 1U || i == at + 2U) {
            if (!x86_asm_u64_lvalue(sel, roles[i].value)) return false;
        } else if (!x86_asm_u64_input(sel, roles[i].value)) return false;
    }
    if (!x86_ir(sel, MINIC_X86_MI_SUB64_IR,
            16, 64U, MINIC_X86_REG_RSP) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RBX, 64U),
            x86_addr(MINIC_X86_REG_RSP, 0))) return false;
    if (!x86_core_load(sel, roles[at + 1U].value, MINIC_X86_REG_R11) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_R11, 0),
            x86_reg(MINIC_X86_REG_RAX, 64U)) ||
        !x86_core_load(sel, roles[at + 2U].value, MINIC_X86_REG_R11) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_R11, 0),
            x86_reg(MINIC_X86_REG_RDX, 64U)) ||
        !x86_core_load(sel, roles[at + 3U].value, MINIC_X86_REG_RBX) ||
        !x86_core_load(sel, roles[at + 4U].value, MINIC_X86_REG_RCX) ||
        !x86_core_load(sel, roles[at].value, MINIC_X86_REG_R11))
        return false;
    if (!alt) {
        bindings[0] = x86_reg(0U, 64U);
        bindings[0].value.physical.reg.bank = MINIC_MACHINE_REGISTER_FLAGS;
        bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        bindings[0].asm_constraint = "=@cce";
    }
    bindings[at] = x86_addr(MINIC_X86_REG_R11, 0);
    bindings[at].asm_constraint = "+m";
    bindings[at].asm_name = alt ? "var" : "ptr";
    bindings[at+1U] = x86_reg(MINIC_X86_REG_RAX, 64U);
    bindings[at+1U].asm_constraint = "+a";
    bindings[at+1U].value.physical.access =
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
    bindings[at+2U] = x86_reg(MINIC_X86_REG_RDX, 64U);
    bindings[at+2U].asm_constraint = "+d";
    bindings[at+2U].value.physical.access =
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
    bindings[at+3U] = x86_reg(MINIC_X86_REG_RBX, 64U);
    bindings[at+3U].asm_constraint = "b";
    bindings[at+3U].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    bindings[at+4U] = x86_reg(MINIC_X86_REG_RCX, 64U);
    bindings[at+4U].asm_constraint = "c";
    bindings[at+4U].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    implicit[0].reg = (MinicMachineRegister){
        0U, 64U, MINIC_MACHINE_REGISTER_FLAGS};
    implicit[0].access = MINIC_MACHINE_REGISTER_CLOBBER;
    if (alt) {
        implicit[1].reg = (MinicMachineRegister){
            MINIC_X86_REG_RSI, 64U, MINIC_MACHINE_REGISTER_GENERAL};
        implicit[1].access = MINIC_MACHINE_REGISTER_CLOBBER;
    }
    target_effects = MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE |
        (alt ? MINIC_MACHINE_EFFECT_CALL : 0U);
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, target_effects, bindings, count, implicit,
            alt ? 2U : 1U, false, false)) return false;
    if (!alt && !x86_1(sel, MINIC_X86_MI_SETZ8_R,
            x86_reg(MINIC_X86_REG_R10, 8U))) return false;
    for (i = at + 1U; i <= at + 2U; ++i)
        if (!x86_core_load(sel, roles[i].value, MINIC_X86_REG_R11) ||
            !x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(i == at + 1U ? MINIC_X86_REG_RAX :
                    MINIC_X86_REG_RDX, 64U),
                x86_addr(MINIC_X86_REG_R11, 0))) return false;
    if (!alt &&
        (!x86_core_load(sel, roles[0].value, MINIC_X86_REG_R11) ||
         !x86_2(sel, MINIC_X86_MI_MOV8_RA,
            x86_reg(MINIC_X86_REG_R10, 8U),
            x86_addr(MINIC_X86_REG_R11, 0)))) return false;
    return x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_RSP, 0),
            x86_reg(MINIC_X86_REG_RBX, 64U)) &&
        x86_ir(sel, MINIC_X86_MI_ADD64_IR,
            16, 64U, MINIC_X86_REG_RSP);
}

/* Linux crash_setup_regs snapshots sixteen live hardware GPRs, FLAGS and
 * RIP.  These are neither C values nor generic inline ASM: each is a
 * target-qualified source form and independent physical MachineIR effect.
 * Capturing R11 uses R10 for its output address; all other GPRs and FLAGS
 * use R11, preserving the source register until its snapshot instruction. */
/* Linux IBT gen_endbr constructs its four-byte ENDBR signature using one
 * early-clobber u32 GNU output. The SourcePolicy has already admitted the
 * exact two-instruction mov/not grammar. Carry its typed hardware value
 * through the existing source-ASM machine effect, then store the register
 * into the original C output object. */
static bool x86_asm_gp_lvalue_width(const X86CoreSelector *,
    MinicCoreValueId, size_t);
static bool x86_source_gen_endbr(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *role;
    MinicMachineOperand binding;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        sel->core->inline_asms == NULL) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    role = &ins->value.structured_inline_asm.operands[0];
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_GEN_ENDBR ||
        source->target_machine_form_variant != 0U || !source->is_volatile ||
        source->is_goto || source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,0U,
            source->template_text,source->template_length) ||
        role->operand_index != 0U ||
        role->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        role->value >= sel->core->value_count ||
        role->has_fixed_register_binding || role->early_clobber ||
        !role->physical_early_clobber ||
        role->physical_read_write || role->has_tied_output ||
        !x86_asm_gp_lvalue_width(sel,role->value,4U)) return false;
    binding = x86_reg(MINIC_X86_REG_RAX,32U);
    binding.value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    binding.asm_constraint = "=&r";
    binding.asm_name = "endbr";
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,0U,&binding,1U,NULL,0U,false,false) ||
        !x86_core_load(sel,role->value,MINIC_X86_REG_R11)) return false;
    return x86_2(sel,MINIC_X86_MI_MOV32_RA,
        x86_reg(MINIC_X86_REG_RAX,32U),
        x86_addr(MINIC_X86_REG_R11,0));
}

static bool x86_source_crash_register(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[16] = {
        MINIC_X86_REG_RBX, MINIC_X86_REG_RCX,
        MINIC_X86_REG_RDX, MINIC_X86_REG_RSI,
        MINIC_X86_REG_RDI, MINIC_X86_REG_RBP,
        MINIC_X86_REG_RAX, MINIC_X86_REG_RSP,
        MINIC_X86_REG_R8, MINIC_X86_REG_R9,
        MINIC_X86_REG_R10, MINIC_X86_REG_R11,
        MINIC_X86_REG_R12, MINIC_X86_REG_R13,
        MINIC_X86_REG_R14, MINIC_X86_REG_R15
    };
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *role;
    MinicMachineOperand binding;
    MinicMachineRegisterEffect effect = {0};
    unsigned int variant, scratch;
    bool memory_output;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 1U)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    role = &ins->value.structured_inline_asm.operands[0];
    variant = source->target_machine_form_variant;
    memory_output = variant <= 17U || variant == 19U;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_CRASH_REGISTER ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        variant == 0U || variant > 19U ||
        (memory_output && !source->is_volatile) || source->is_goto ||
        source->has_memory_clobber != (variant == 19U) ||
        source->register_clobber_count != 0U ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length) ||
        role->operand_index != 0U ||
        role->value >= sel->core->value_count ||
        role->kind != (memory_output ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        role->has_fixed_register_binding || role->early_clobber ||
        role->physical_early_clobber || role->physical_read_write ||
        role->has_tied_output ||
        !x86_asm_u64_lvalue(sel, role->value)) return false;
    if (!memory_output) {
        binding = x86_reg(MINIC_X86_REG_RAX, 64U);
        binding.value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        binding.asm_constraint = "=r";
        if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
                source, 0U, &binding, 1U, NULL, 0U,
                false, false) ||
            !x86_core_load(sel, role->value, MINIC_X86_REG_R11))
            return false;
        return x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RAX, 64U),
            x86_addr(MINIC_X86_REG_R11, 0));
    }
    scratch = variant == 12U ? MINIC_X86_REG_R10 : MINIC_X86_REG_R11;
    if (!x86_core_load(sel, role->value, scratch)) return false;
    binding = x86_addr(scratch, 0);
    binding.asm_constraint = variant == 19U ? "=rm" : "=m";
    if (variant <= 16U) {
        effect.reg = (MinicMachineRegister){
            regs[variant-1U], 64U, MINIC_MACHINE_REGISTER_GENERAL};
    } else {
        effect.reg = (MinicMachineRegister){
            0U, 64U, MINIC_MACHINE_REGISTER_FLAGS};
    }
    effect.access = MINIC_MACHINE_REGISTER_READ;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source, MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        &binding, 1U, &effect, 1U, false, false);
}

/* Linux futex atomic fault recovery has six exact GNU source forms sharing
 * one typed x86 physical execution ABI.  R10 remains the faulting user
 * address, R8D is the error/result carrier encoded in __ex_table's typed
 * register-fixup word, and RCX is reserved for post-operation lvalue stores.
 * The loop forms use R11D for the operation operand and R9D for proposed
 * values; all source-level `=&` clobber overlaps are independently checked. */
static bool x86_source_futex_extable(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[6];
    MinicMachineRegisterEffect flags = {0};
    unsigned int form;
    bool loop;
    size_t count, outputs, i;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    form = source->target_machine_form_variant;
    loop = form >= 4U && form <= 6U;
    outputs = loop ? 4U : 3U;
    count = outputs + 2U;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_FUTEX_EXTABLE ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        form == 0U || form > 6U ||
        ins->value.structured_inline_asm.operand_count != count ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->explicit_memory_clobber != (form == 1U) ||
        source->register_clobber_count != 0U ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length)) return false;
    for (i = 0U; i < count; ++i) {
        const bool memory = i == 2U;
        const bool readwrite = memory || (form == 1U && i == 0U);
        const bool early = loop && (i == 0U || i == 1U || i == 3U);
        const bool tie = i == outputs + 1U ||
            ((form == 2U || form == 3U) && i == outputs);
        const size_t tied_to = i == outputs + 1U ? 1U : 0U;
        const MinicCoreStructuredInlineAsmOperandKind kind = memory ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i < outputs ? (form == 1U && i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != kind ||
            roles[i].has_fixed_register_binding ||
            roles[i].early_clobber ||
            roles[i].physical_early_clobber != early ||
            roles[i].physical_read_write != readwrite ||
            roles[i].has_tied_output != tie ||
            (tie && roles[i].tied_output_index != tied_to) ||
            !(i < outputs ? x86_asm_u32_lvalue(sel, roles[i].value) :
                x86_asm_u32_input(sel, roles[i].value))) return false;
    }
    /* Core carries MEMORY_READWRITE operand two as an address SSA, not a
     * scalar value.  Only the native 32-bit futex word may be dereferenced. */
    if (!x86_core_load(sel, roles[2].value, MINIC_X86_REG_R10)) return false;
    if (form == 1U) {
        if (!x86_core_load(sel, roles[0].value, MINIC_X86_REG_RCX) ||
            !x86_2(sel, MINIC_X86_MI_MOV32_AR,
                x86_addr(MINIC_X86_REG_RCX, 0),
                x86_reg(MINIC_X86_REG_R8, 32U)) ||
            !x86_core_load(sel, roles[3].value, MINIC_X86_REG_R9) ||
            !x86_core_load(sel, roles[4].value, MINIC_X86_REG_RAX))
            return false;
    } else if (loop) {
        if (!x86_core_load(sel, roles[4].value, MINIC_X86_REG_R11) ||
            !x86_core_load(sel, roles[5].value, MINIC_X86_REG_R8))
            return false;
    } else {
        if (!x86_core_load(sel, roles[3].value, MINIC_X86_REG_RAX) ||
            !x86_core_load(sel, roles[4].value, MINIC_X86_REG_R8))
            return false;
    }
    if (form == 1U) {
        bindings[0] = x86_reg(MINIC_X86_REG_R8, 32U);
        bindings[0].asm_constraint = "+r";
        bindings[0].value.physical.access =
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
        bindings[1] = x86_reg(MINIC_X86_REG_RAX, 32U);
        bindings[1].asm_constraint = "=a";
    } else {
        bindings[0] = x86_reg(MINIC_X86_REG_RAX, 32U);
        bindings[0].asm_constraint = loop ? "=&a" : "=r";
        bindings[1] = x86_reg(MINIC_X86_REG_R8, 32U);
        bindings[1].asm_constraint = loop ? "=&r" : "=r";
    }
    bindings[0].value.physical.access |= MINIC_MACHINE_REGISTER_WRITE;
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[2] = x86_addr(MINIC_X86_REG_R10, 0);
    bindings[2].asm_constraint = "+m";
    if (loop) {
        bindings[3] = x86_reg(MINIC_X86_REG_R9, 32U);
        bindings[3].asm_constraint = "=&r";
        bindings[3].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        bindings[4] = x86_reg(MINIC_X86_REG_R11, 32U);
        bindings[4].asm_constraint = "r";
        bindings[4].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bindings[5] = x86_reg(MINIC_X86_REG_R8, 32U);
        bindings[5].asm_constraint = "1";
        bindings[5].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    } else {
        bindings[3] = x86_reg(form == 1U ?
            MINIC_X86_REG_R9 : MINIC_X86_REG_RAX, 32U);
        bindings[3].asm_constraint = form == 1U ? "r" : "0";
        bindings[3].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bindings[4] = x86_reg(form == 1U ?
            MINIC_X86_REG_RAX : MINIC_X86_REG_R8, 32U);
        bindings[4].asm_constraint = "1";
        bindings[4].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    flags.reg = (MinicMachineRegister){
        0U, 64U, MINIC_MACHINE_REGISTER_FLAGS};
    flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    /* Core normalizes +m and explicit "memory" into one conservative
     * compiler barrier demand; all six physical forms must preserve it. */
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings, count, form == 2U ? NULL : &flags,
            form == 2U ? 0U : 1U, false, false)) return false;
    for (i = 0U; i < outputs; ++i) {
        unsigned int reg;
        if (i == 2U) continue;
        reg = i == 0U ? (form == 1U ? MINIC_X86_REG_R8 :
            MINIC_X86_REG_RAX) : i == 1U ? (form == 1U ?
                MINIC_X86_REG_RAX : MINIC_X86_REG_R8) : MINIC_X86_REG_R9;
        if (!x86_core_load(sel, roles[i].value, MINIC_X86_REG_RCX) ||
            !x86_2(sel, MINIC_X86_MI_MOV32_RA,
                x86_reg(reg, 32U),
                x86_addr(MINIC_X86_REG_RCX, 0))) return false;
    }
    return true;
}

/* The source qualifier already proves the C expression types.  The Core
 * selector independently proves their width/layout before assigning actual
 * GP registers, because the typed MachineIR cannot carry C type identity. */
static bool x86_asm_gp_lvalue_width(const X86CoreSelector *sel,
    MinicCoreValueId value, size_t width) {
    MinicCoreTypeId pointee_id;
    const MinicCoreType *type;
    return value < sel->core->value_count &&
        minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[value].core_type_id, &pointee_id) &&
        (type = x86_type(sel,pointee_id)) != NULL &&
        type->has_layout && type->size == width &&
        ((type->kind == MINIC_CORE_TYPE_INTEGER &&
          !type->value.integer.is_bool) ||
         (width == 8U && type->kind == MINIC_CORE_TYPE_POINTER));
}

static bool x86_asm_gp_input_width(const X86CoreSelector *sel,
    MinicCoreValueId value, size_t width) {
    const MinicCoreType *type;
    if (value >= sel->core->value_count ||
        (type = x86_type(sel,
            sel->core->values[value].core_type_id)) == NULL ||
        !type->has_layout || type->size != width) return false;
    return (type->kind == MINIC_CORE_TYPE_INTEGER &&
        !type->value.integer.is_bool) ||
        (width == 8U && type->kind == MINIC_CORE_TYPE_POINTER);
}

/* The six original Linux MSR forms are one exact source-qualified family:
 * ordinary RDMSR/WRMSR, their #MC fixups, and safe register-qualified
 * exception records. Keep ECX/EAX/EDX architectural bindings as physical
 * MachineIR facts; only the safe forms have a separate GNU =r error output.
 * None of these privileged instructions is ever executed by our tests. */
/* The entire Linux counter ISA family uses architectural EDX:EAX outputs.
 * RDPMC/XGETBV also consume ECX; RDTSCP and the ordered Linux alternatives
 * clobber ECX. The latter keeps its exact .altinstructions payload intact. */
/* Linux 6.6 alternative.h HWEIGHT chooses the original helper call at
 * runtime until the kernel patches it with POPCNT. Preserve both fragments
 * as a single source-qualified physical MachineIR unit. */
static bool x86_source_hweight(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[2] = {{0}};
    unsigned int bytes, bits;
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count || sel->core->inline_asms == NULL)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    bytes = minic_x86_64_hweight_alternative_form(
        source->template_text,source->template_length);
    if ((bytes != 4U && bytes != 8U) ||
        source->target_machine_form_id != MINIC_X86_MACHINE_ASM_HWEIGHT ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        /* The Source HWEIGHT qualifier requires original GNU nonvolatile
         * syntax; Core deliberately marks its produced output effectful. */
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        roles == NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length))
        return false;
    if (!x86_asm_gp_lvalue_width(sel,roles[0].value,bytes) ||
        !x86_asm_gp_input_width(sel,roles[1].value,bytes))
        return false;
    for (i = 0U; i < 2U; ++i)
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding ||
            roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write ||
            roles[i].has_tied_output)
            return false;
    bits = bytes * 8U;
    if (!x86_core_load(sel,roles[1].value,MINIC_X86_REG_RDI))
        return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX,bits);
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[0].asm_constraint = "=a";
    bindings[1] = x86_reg(MINIC_X86_REG_RDI,bits);
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    bindings[1].asm_constraint = "D";
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,0U,bindings,2U,NULL,0U,false,false) &&
        x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11) &&
        x86_2(sel,bits == 64U ?
            MINIC_X86_MI_MOV64_RA : MINIC_X86_MI_MOV32_RA,
            x86_reg(MINIC_X86_REG_RAX,bits),
            x86_addr(MINIC_X86_REG_R11,0));
}

/* Linux raw MMIO and scalar XCHG share one bounded physical memory
 * transfer. The input/output C lvalue has its own address; the memory
 * operand remains an actual dereference of R10, never a C scalar surrogate. */
static bool x86_source_mmio_or_xchg(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[2];
    MinicMachineRegisterEffect flags = {0};
    MinicX86MachineOpcode load, store;
    uint32_t form_id;
    unsigned int width, bits;
    const char *reg_constraint;
    bool reading, writing, exchange, clobbers_memory;
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 2U)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    form_id = source->target_machine_form_id;
    reading = form_id == MINIC_X86_MACHINE_ASM_MMIO_READ;
    writing = form_id == MINIC_X86_MACHINE_ASM_MMIO_WRITE;
    exchange = form_id == MINIC_X86_MACHINE_ASM_ATOMIC_XCHG;
    if (!reading && !writing && !exchange) return false;
    width = reading ? minic_x86_64_linux_mmio_read_form(
        source->template_text,source->template_length) :
        writing ? minic_x86_64_linux_mmio_write_form(
            source->template_text,source->template_length) :
            minic_x86_64_linux_atomic_xchg_form(
                source->template_text,source->template_length);
    clobbers_memory = source->has_memory_clobber;
    if ((width != 1U && width != 2U && width != 4U && width != 8U) ||
        source->target_machine_form_variant != width ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto ||
        source->register_clobber_count != (exchange ? 1U : 0U) ||
        (exchange && (source->register_clobbers == NULL ||
            source->register_clobbers[0].name == NULL ||
            source->register_clobbers[0].name_length != 2U ||
            memcmp(source->register_clobbers[0].name,"cc",2U) != 0)) ||
        (exchange && (!clobbers_memory || !source->explicit_memory_clobber)) ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            form_id,source->target_machine_form_variant,
            source->template_text,source->template_length) ||
        roles == NULL)
        return false;
    for (i = 0U; i < 2U; ++i) {
        const bool address = reading || exchange || (writing && i == 1U);
        const MinicCoreType *type;
        MinicCoreTypeId type_id;
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != (exchange ?
                (i == 0U ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) :
                (reading ?
                    (i == 0U ?
                        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) :
                    (i == 0U ?
                        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
                        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT))) ||
            roles[i].has_fixed_register_binding ||
            roles[i].early_clobber || roles[i].physical_early_clobber ||
            roles[i].has_tied_output)
            return false;
        if (address) {
            if (!minic_lp64_core_pointer_pointee(sel->core->type_context,
                    sel->core->values[roles[i].value].core_type_id,
                    &type_id))
                return false;
        } else type_id = sel->core->values[roles[i].value].core_type_id;
        type = x86_type(sel,type_id);
        if (type == NULL || !type->has_layout ||
            type->size != (size_t)width ||
            !((type->kind == MINIC_CORE_TYPE_INTEGER &&
                !type->value.integer.is_bool) ||
              (exchange && width == 8U &&
               type->kind == MINIC_CORE_TYPE_POINTER)))
            return false;
    }
    bits = width * 8U;
    load = width == 1U ? MINIC_X86_MI_MOV8_AR :
        width == 2U ? MINIC_X86_MI_MOV16_AR :
        width == 4U ? MINIC_X86_MI_MOV32_AR : MINIC_X86_MI_MOV64_AR;
    store = width == 1U ? MINIC_X86_MI_MOV8_RA :
        width == 2U ? MINIC_X86_MI_MOV16_RA :
        width == 4U ? MINIC_X86_MI_MOV32_RA : MINIC_X86_MI_MOV64_RA;
    if (reading) {
        reg_constraint = width == 1U ?
            (minic_x86_64_compact_asm_equals(
                source->template_text,source->template_length,
                "movb%1,%%al") ? "=a" : "=q") : "=r";
    } else reg_constraint = exchange ?
        (width == 1U ? "+q" : "+r") :
        (width == 1U ? "q" : "r");
    if (exchange) {
        if (!x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11) ||
            !x86_2(sel,load,x86_addr(MINIC_X86_REG_R11,0),
                x86_reg(MINIC_X86_REG_RAX,bits)))
            return false;
    } else if (writing &&
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_RAX))
        return false;
    if (!x86_core_load(sel,roles[1].value,
            MINIC_X86_REG_R10)) return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX,bits);
    bindings[0].asm_constraint = (char *)reg_constraint;
    bindings[0].value.physical.access = exchange ?
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
        reading ? MINIC_MACHINE_REGISTER_WRITE :
                  MINIC_MACHINE_REGISTER_READ;
    bindings[1] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[1].asm_constraint = exchange ? "+m" : "m";
    if (exchange) {
        flags.reg = (MinicMachineRegister){
            0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
        flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,
            (reading || exchange ? MINIC_MACHINE_EFFECT_MEMORY_READ : 0U) |
            (writing || exchange ? MINIC_MACHINE_EFFECT_MEMORY_WRITE : 0U),
            bindings,2U,exchange ? &flags : NULL,exchange ? 1U : 0U,
            false,false))
        return false;
    if (reading || exchange) {
        if (!x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11) ||
            !x86_2(sel,store,x86_reg(MINIC_X86_REG_RAX,bits),
                x86_addr(MINIC_X86_REG_R11,0)))
            return false;
    }
    return true;
}

/* Linux PCI MMCONFIG's Fam10h erratum requires the *hardware* load/store
 * through AL/AX/EAX, with a general-register address in (%1).  This is not
 * the ordinary GNU `m` MMIO form: preserve the exact qualified source asm,
 * including its fixed accumulator and the actual dereference of R10. */
static bool x86_source_pci_mmconfig(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreType *value_type, *address_type;
    MinicCoreTypeId value_type_id;
    MinicMachineOperand bindings[2];
    MinicX86MachineOpcode store;
    bool reading;
    unsigned int width, bits;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 2U)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    reading = minic_x86_64_linux_pci_mmconfig_form(
        source->template_text, source->template_length, false) != 0U;
    width = minic_x86_64_linux_pci_mmconfig_form(
        source->template_text, source->template_length, !reading);
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_PCI_MMCONFIG ||
        (width != 1U && width != 2U && width != 4U) ||
        source->target_machine_form_variant != width ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto || source->register_clobber_count != 0U ||
        source->explicit_memory_clobber != !reading ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length) ||
        roles == NULL ||
        roles[0].operand_index != 0U ||
        roles[1].operand_index != 1U ||
        roles[0].value >= sel->core->value_count ||
        roles[1].value >= sel->core->value_count ||
        roles[0].kind != (reading ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        roles[0].has_fixed_register_binding ||
        roles[1].has_fixed_register_binding ||
        roles[0].early_clobber || roles[1].early_clobber ||
        roles[0].physical_early_clobber ||
        roles[1].physical_early_clobber ||
        roles[0].has_tied_output || roles[1].has_tied_output)
        return false;
    if (reading) {
        if (!minic_lp64_core_pointer_pointee(sel->core->type_context,
                sel->core->values[roles[0].value].core_type_id,
                &value_type_id)) return false;
    } else value_type_id =
        sel->core->values[roles[0].value].core_type_id;
    value_type = x86_type(sel, value_type_id);
    address_type = x86_type(sel,
        sel->core->values[roles[1].value].core_type_id);
    if (value_type == NULL || !value_type->has_layout ||
        value_type->size != width ||
        value_type->kind != MINIC_CORE_TYPE_INTEGER ||
        value_type->value.integer.is_bool ||
        address_type == NULL ||
        address_type->kind != MINIC_CORE_TYPE_POINTER)
        return false;
    bits = width * 8U;
    if ((!reading && !x86_core_load(sel, roles[0].value,
            MINIC_X86_REG_RAX)) ||
        !x86_core_load(sel, roles[1].value, MINIC_X86_REG_R10))
        return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX, bits);
    bindings[0].asm_constraint = reading ? "=a" : "a";
    bindings[0].value.physical.access = reading ?
        MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    bindings[1] = x86_reg(MINIC_X86_REG_R10, 64U);
    bindings[1].asm_constraint = "r";
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, reading ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings, 2U, NULL, 0U, false, false))
        return false;
    if (!reading) return true;
    store = width == 1U ? MINIC_X86_MI_MOV8_RA :
        width == 2U ? MINIC_X86_MI_MOV16_RA : MINIC_X86_MI_MOV32_RA;
    return x86_core_load(sel, roles[0].value, MINIC_X86_REG_R11) &&
        x86_2(sel, store, x86_reg(MINIC_X86_REG_RAX, bits),
            x86_addr(MINIC_X86_REG_R11, 0));
}

/* Original Linux memset16/32/64 has two early-clobber outputs tied to the
 * original count/destination inputs: =&c, =&D, a, 1, 0.  Preserve RCX and
 * RDI as actual rep-stos hardware operands, including their post-asm C
 * lvalue outputs, instead of treating the source as a generic memory copy. */
static bool x86_source_rep_stos(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const MinicCoreStructuredInlineAsmOperandKind kinds[5] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    static const unsigned int registers[5] = {
        MINIC_X86_REG_RCX, MINIC_X86_REG_RDI, MINIC_X86_REG_RAX,
        MINIC_X86_REG_RDI, MINIC_X86_REG_RCX
    };
    static const char *const constraints[5] = {
        "=&c", "=&D", "a", "1", "0"
    };
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[5];
    const MinicCoreType *type;
    MinicCoreTypeId value_type;
    size_t width, i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 5U)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    width = minic_x86_64_linux_rep_stos_width(
        source->template_text, source->template_length);
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_REP_STOS ||
        (width != 2U && width != 4U && width != 8U) ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto || !source->explicit_memory_clobber ||
        !source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length) ||
        roles == NULL) return false;
    for (i = 0U; i < 5U; ++i) {
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != kinds[i] ||
            roles[i].has_fixed_register_binding)
            return false;
        if (i < 2U) {
            if (!minic_lp64_core_pointer_pointee(sel->core->type_context,
                    sel->core->values[roles[i].value].core_type_id,
                    &value_type)) return false;
        } else value_type =
            sel->core->values[roles[i].value].core_type_id;
        type = x86_type(sel, value_type);
        if (type == NULL || !type->has_layout ||
            (i == 3U ? type->kind != MINIC_CORE_TYPE_POINTER ||
                        type->size != 8U :
                type->kind != MINIC_CORE_TYPE_INTEGER ||
                type->value.integer.is_bool ||
                type->size != (i == 2U ? width : 8U)))
            return false;
    }
    if (!x86_core_load(sel, roles[2].value, MINIC_X86_REG_RAX) ||
        !x86_core_load(sel, roles[3].value, MINIC_X86_REG_RDI) ||
        !x86_core_load(sel, roles[4].value, MINIC_X86_REG_RCX))
        return false;
    for (i = 0U; i < 5U; ++i) {
        bindings[i] = x86_reg(registers[i],
            i == 2U ? (unsigned int)(width * 8U) : 64U);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = i < 2U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder, source,
            MINIC_MACHINE_EFFECT_MEMORY_WRITE, bindings, 5U,
            NULL, 0U, false, false))
        return false;
    for (i = 0U; i < 2U; ++i) {
        if (!x86_core_load(sel, roles[i].value, MINIC_X86_REG_R11) ||
            !x86_2(sel, MINIC_X86_MI_MOV64_RA,
                x86_reg(registers[i], 64U),
                x86_addr(MINIC_X86_REG_R11, 0)))
            return false;
    }
    return true;
}

static bool x86_source_counter(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int registers[] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX, MINIC_X86_REG_RCX
    };
    static const char *const constraints[] = {"=a", "=d", "c"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect rcx = {0};
    unsigned int form;
    unsigned int width;
    size_t count, i;
    bool has_input, clobbers_rcx;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    form = minic_x86_64_linux_counter_form(
        source->template_text, source->template_length);
    has_input = form == 2U || form == 5U;
    clobbers_rcx = form == 3U || form == 4U;
    count = has_input ? 3U : 2U;
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_COUNTER ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        form < 1U || form > 5U ||
        source->source_asm_unique_id == 0U ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->register_clobber_count != (clobbers_rcx ? 1U : 0U) ||
        (clobbers_rcx && (source->register_clobbers == NULL ||
            source->register_clobbers[0].name == NULL ||
            source->register_clobbers[0].name_length != 3U ||
            memcmp(source->register_clobbers[0].name,"ecx",3U) != 0)) ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length) ||
        ins->value.structured_inline_asm.operand_count != count ||
        roles == NULL ||
        (!x86_asm_gp_lvalue_width(sel,roles[0].value,4U) &&
         !x86_asm_gp_lvalue_width(sel,roles[0].value,8U)))
        return false;
    width = x86_asm_gp_lvalue_width(sel,roles[0].value,8U) ? 64U : 32U;
    for (i = 0U; i < count; ++i) {
        const bool output = i < 2U;
        if (roles[i].operand_index != i ||
            roles[i].value >= sel->core->value_count ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write || roles[i].has_tied_output ||
            !(output ?
                x86_asm_gp_lvalue_width(sel,roles[i].value,
                    (size_t)(width / 8U)) :
                x86_asm_u32_input(sel,roles[i].value)))
            return false;
    }
    if (has_input &&
        !x86_core_load(sel,roles[2].value,MINIC_X86_REG_RCX))
        return false;
    for (i = 0U; i < count; ++i) {
        bindings[i] = x86_reg(registers[i],i < 2U ? width : 32U);
        bindings[i].value.physical.access = i < 2U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
        bindings[i].asm_constraint = (char *)constraints[i];
    }
    if (clobbers_rcx) {
        rcx.reg = (MinicMachineRegister){
            MINIC_X86_REG_RCX,32U,MINIC_MACHINE_REGISTER_GENERAL};
        rcx.access = MINIC_MACHINE_REGISTER_CLOBBER;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,0U,bindings,count,
            clobbers_rcx ? &rcx : NULL,clobbers_rcx ? 1U : 0U,
            false,false))
        return false;
    for (i = 0U; i < 2U; ++i)
        if (!x86_core_load(sel,roles[i].value,MINIC_X86_REG_R11) ||
            !x86_2(sel,width == 64U ? MINIC_X86_MI_MOV64_RA :
                MINIC_X86_MI_MOV32_RA,
                x86_reg(registers[i],width),
                x86_addr(MINIC_X86_REG_R11,0)))
            return false;
    return true;
}

/* Linux special_insns.h uses byte-encoded RDPKRU/WRPKRU with fixed
 * architectural EAX/ECX/EDX operands.  Keep both exact Source-qualified
 * forms together: RDPKRU writes EAX and EDX after reading ECX; WRPKRU
 * reads EAX, ECX and EDX. These are CPU protection-key state operations,
 * not memory aliases or normal arithmetic with FLAGS side effects. */
/* Linux rdrand_long/rdseed_long have two independent outputs: the CPU
 * Carry Flag reports success while the 64-bit sampled value is in a GPR.
 * Keep the output status as a real FLAGS MachineIR binding; selecting a
 * nonzero test of RAX would silently change retry-loop semantics. */
static bool x86_asm_bool_lvalue(const X86CoreSelector *sel,
    MinicCoreValueId value) {
    MinicCoreTypeId pointee;
    const MinicCoreType *type;
    return value < sel->core->value_count &&
        minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[value].core_type_id,&pointee) &&
        (type = x86_type(sel,pointee)) != NULL &&
        type->kind == MINIC_CORE_TYPE_INTEGER && type->has_layout &&
        type->size == 1U && type->value.integer.is_bool;
}

/* arch/x86/kernel/fpu/bugs.c performs the historical FDIV x87 probe
 * with exactly one int memory result and two binary64 memory inputs.
 * The two FNINIT operations reset all x87 slots, so describe the complete
 * floating-stack clobber instead of leaking an untyped textual fallback. */
static bool x86_fdiv_memory_shape(const X86CoreSelector *sel,
    MinicCoreValueId value, bool output) {
    MinicCoreTypeId pointee;
    const MinicCoreType *type;
    if (value >= sel->core->value_count ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[value].core_type_id,&pointee) ||
        (type = x86_type(sel,pointee)) == NULL ||
        !type->has_layout)
        return false;
    return output ? type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->size == 4U && type->value.integer.is_signed &&
        !type->value.integer.is_bool :
        type->kind == MINIC_CORE_TYPE_FLOAT && type->size == 8U &&
        type->value.floating.format == MINIC_CORE_FLOAT_BINARY64;
}

static bool x86_source_fdiv_bug(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int registers[3] = {
        MINIC_X86_REG_R10, MINIC_X86_REG_R11, MINIC_X86_REG_RAX
    };
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect fpu[8];
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (source->target_machine_form_id == MINIC_X86_MACHINE_ASM_FDIV_BUG &&
        getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL) {
        fprintf(stderr,
            "x86 fdiv source: plan=%u variant=%u unique=%u"
            " volatile=%u goto=%u mem=%u explicit=%u"
            " regs=%zu imm=%zu target=%u operand_count=%zu"
            " template_ok=%u\n",
            (unsigned int)source->operand_plan_owner,
            source->target_machine_form_variant,
            (unsigned int)(source->source_asm_unique_id != 0U),
            (unsigned int)source->is_volatile,
            (unsigned int)source->is_goto,
            (unsigned int)source->has_memory_clobber,
            (unsigned int)source->explicit_memory_clobber,
            source->register_clobber_count,
            source->resolved_immediate_count,
            (unsigned int)(source->goto_second_target != MINIC_CORE_BLOCK_INVALID),
            ins->value.structured_inline_asm.operand_count,
            (unsigned int)minic_x86_64_typed_machine_form_matches_template(
                source->target_machine_form_id,
                source->target_machine_form_variant,
                source->template_text,source->template_length));
        if (roles != NULL) {
            size_t diag;
            for (diag = 0U; diag < 3U &&
                diag < ins->value.structured_inline_asm.operand_count; ++diag) {
                const MinicCoreStructuredInlineAsmOperand *r = &roles[diag];
                fprintf(stderr,
                    "x86 fdiv role%zu: pos=%zu kind=%u fixed=%u"
                    " early=%u phys_early=%u rw=%u tie=%u to=%zu"
                    " value=%u shape=%u\n",
                    diag,r->operand_index,(unsigned int)r->kind,
                    (unsigned int)r->has_fixed_register_binding,
                    (unsigned int)r->early_clobber,
                    (unsigned int)r->physical_early_clobber,
                    (unsigned int)r->physical_read_write,
                    (unsigned int)r->has_tied_output,r->tied_output_index,
                    (unsigned int)r->value,
                    (unsigned int)x86_fdiv_memory_shape(sel,
                        r->value,diag == 0U));
            }
        }
    }
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_FDIV_BUG ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        source->is_goto || !source->has_memory_clobber ||
        source->explicit_memory_clobber ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        roles == NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length))
        return false;
    for (i = 0U; i < 3U; ++i) {
        if (roles[i].operand_index != i ||
            roles[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write || roles[i].has_tied_output ||
            !x86_fdiv_memory_shape(sel,roles[i].value,i == 0U) ||
            !x86_core_load(sel,roles[i].value,registers[i]))
            return false;
        bindings[i] = x86_addr(registers[i],0);
        bindings[i].asm_constraint = i == 0U ? "=m" : "m";
    }
    for (i = 0U; i < 8U; ++i) {
        fpu[i].reg = (MinicMachineRegister){
            (unsigned int)i,80U,MINIC_MACHINE_REGISTER_FLOAT};
        fpu[i].access = MINIC_MACHINE_REGISTER_CLOBBER;
    }
    {
        const bool selected = minic_machine_builder_emit_core_source_asm(
            &sel->builder,source,MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,3U,fpu,8U,false,false);
        if (!selected && getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL)
            fprintf(stderr,"x86 fdiv builder: source asm emission rejected\n");
        return selected;
    }
}

/* Early Linux x87 presence probe destroys the complete x87 stack via
 * FNINIT and writes the live 16-bit status/control words into two distinct
 * GNU +m operands. Carry exact destination pointers as typed MachineIR. */
static bool x86_source_x87_probe(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[2];
    MinicMachineRegisterEffect x87[8];
    const unsigned int regs[2] = {MINIC_X86_REG_R10,MINIC_X86_REG_R11};
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >= sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 2U)
        return false;
    source = &sel->core->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (roles == NULL ||
        source->target_machine_form_id != MINIC_X86_MACHINE_ASM_X87_PROBE ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber || source->explicit_memory_clobber ||
        !source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,0U,
            source->template_text,source->template_length)) return false;
    for (i = 0U; i < 2U; ++i) {
        if (roles[i].operand_index != i ||
            roles[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            !roles[i].physical_read_write || roles[i].has_tied_output ||
            !x86_asm_gp_lvalue_width(sel,roles[i].value,2U) ||
            !x86_core_load(sel,roles[i].value,regs[i])) return false;
        bindings[i] = x86_addr(regs[i],0);
        bindings[i].asm_constraint = "+m";
    }
    for (i = 0U; i < 8U; ++i) {
        x87[i].reg = (MinicMachineRegister){
            (unsigned int)i,80U,MINIC_MACHINE_REGISTER_FLOAT};
        x87[i].access = MINIC_MACHINE_REGISTER_CLOBBER;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        bindings,2U,x87,8U,false,false);
}

static bool x86_source_random_seed(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[2];
    MinicMachineRegisterEffect flags = {0};
    unsigned int form;
    bool named;
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    form = minic_x86_64_linux_random_seed_form(
        source->template_text,source->template_length);
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_RANDOM_SEED ||
        (form != 1U && form != 2U) ||
        source->target_machine_form_variant != form ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        roles == NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length))
        return false;
    for (i = 0U; i < 2U; ++i) {
        if (roles[i].operand_index != i ||
            roles[i].kind !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write || roles[i].has_tied_output)
            return false;
    }
    if (!x86_asm_bool_lvalue(sel,roles[0].value) ||
        !x86_asm_u64_lvalue(sel,roles[1].value))
        return false;
    /* Both exact Linux templates have the same six-character opcode and
     * either a named %[out] or numbered %1 output reference. */
    named = source->template_length > 8U &&
        source->template_text[7] == '%' &&
        source->template_text[8] == '[';
    bindings[0] = x86_reg(0U,64U);
    bindings[0].value.physical.reg.bank = MINIC_MACHINE_REGISTER_FLAGS;
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[0].asm_constraint = "=@ccc";
    bindings[1] = x86_reg(MINIC_X86_REG_RAX,64U);
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[1].asm_constraint = "=r";
    bindings[1].asm_name = named ? "out" : NULL;
    flags.reg = (MinicMachineRegister){
        0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
    flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            0U,bindings,2U,&flags,1U,false,false) ||
        !x86_1(sel,MINIC_X86_MI_SETC8_R,
            x86_reg(MINIC_X86_REG_R11,8U)))
        return false;
    /* Neither address calculation nor the value store modifies CF before
     * SETC; after SETC, the one-byte boolean stays live in R11B. */
    if (!x86_core_load(sel,roles[1].value,MINIC_X86_REG_R10) ||
        !x86_2(sel,MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RAX,64U),
            x86_addr(MINIC_X86_REG_R10,0)) ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_R10) ||
        !x86_2(sel,MINIC_X86_MI_MOV8_RA,
            x86_reg(MINIC_X86_REG_R11,8U),
            x86_addr(MINIC_X86_REG_R10,0)))
        return false;
    return true;
}


/* SSE MXCSR is a 32-bit architectural control word loaded from or stored
 * through a true memory operand. Keep its GNU literal sealed to one memory
 * binding; the implicit CPU control state is an observable volatile effect. */

/* Exact, descriptor-owned privileged and Linux atomic scalar ASM. C ingress
 * owns GNU constraint parsing. These helpers recheck Core roles and commit
 * real physical operands; no source text ever becomes a generic assembler
 * escape hatch. */
static bool x86_scalar_source_base(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, uint32_t form_id,
    const MinicCoreInlineAsm **source_out,
    const MinicCoreStructuredInlineAsmOperand **roles_out) {
    const MinicCoreInlineAsm *source;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    if (source->target_machine_form_id != form_id ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || source->is_goto ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length))
        return false;
    *source_out = source;
    *roles_out = ins->value.structured_inline_asm.operands;
    return *roles_out != NULL;
}

static bool x86_scalar_role(const MinicCoreStructuredInlineAsmOperand *role,
    size_t index, MinicCoreStructuredInlineAsmOperandKind kind) {
    return role->operand_index == index && role->kind == kind &&
        !role->has_fixed_register_binding && !role->early_clobber &&
        !role->physical_early_clobber;
}

static bool x86_scalar_output_store(X86CoreSelector *sel,
    MinicCoreValueId pointer, unsigned int reg, size_t width) {
    MinicX86MachineOpcode opcode;
    switch (width) {
    case 1U: opcode = MINIC_X86_MI_MOV8_RA; break;
    case 2U: opcode = MINIC_X86_MI_MOV16_RA; break;
    case 4U: opcode = MINIC_X86_MI_MOV32_RA; break;
    case 8U: opcode = MINIC_X86_MI_MOV64_RA; break;
    default: return false;
    }
    return x86_core_load(sel,pointer,MINIC_X86_REG_RDI) &&
        x86_2(sel,opcode,x86_reg(reg,(unsigned int)(width*8U)),
            x86_addr(MINIC_X86_REG_RDI,0));
}

static MinicMachineRegisterEffect x86_scalar_flags_clobber(void) {
    MinicMachineRegisterEffect effect = {0};
    effect.reg = (MinicMachineRegister){
        0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
    effect.access = MINIC_MACHINE_REGISTER_CLOBBER;
    return effect;
}

/* Linux array_index_mask_nospec: size and index compare in their exact
 * Core byte width, while the subtraction-borrow materializes a full 64-bit
 * mask. Two selected physical opcodes own the hardware flags; the single
 * source-owned marker contributes provenance/cc clobber but prints no bytes. */
static bool x86_source_nospec(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreInlineAsmRegisterClobber *clobber;
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    size_t width;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    width = source->target_machine_form_variant;
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_NOSPEC ||
        (width != 4U && width != 8U) ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->register_clobber_count != 1U ||
        source->register_clobbers == NULL ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        roles == NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length))
        return false;
    clobber = &source->register_clobbers[0];
    if (clobber->name == NULL || clobber->name_length != 2U ||
        memcmp(clobber->name, "cc", 2U) != 0 ||
        !x86_scalar_role(&roles[0], 0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        !x86_scalar_role(&roles[1], 1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        !x86_scalar_role(&roles[2], 2U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[0].physical_read_write || roles[1].physical_read_write ||
        roles[2].physical_read_write ||
        roles[0].has_tied_output || roles[1].has_tied_output ||
        roles[2].has_tied_output ||
        !x86_asm_u64_lvalue(sel, roles[0].value) ||
        !x86_asm_gp_input_width(sel, roles[1].value, width) ||
        !x86_asm_gp_input_width(sel, roles[2].value, width))
        return false;
    /* GNU %1=size, %2=index, %0=mask. No flag-affecting instruction
     * between CMP and SBB: CF alone determines zero versus all-ones. */
    return x86_core_load(sel, roles[1].value, MINIC_X86_REG_R10) &&
        x86_core_load(sel, roles[2].value, MINIC_X86_REG_R11) &&
        x86_rr(sel, width == 4U ? MINIC_X86_MI_CMP32_RR :
            MINIC_X86_MI_CMP64_RR, (unsigned int)(width * 8U),
            MINIC_X86_REG_R10, MINIC_X86_REG_R11) &&
        x86_rr(sel, MINIC_X86_MI_SBB64_RR, 64U,
            MINIC_X86_REG_R8, MINIC_X86_REG_R8) &&
        minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, 0U, NULL, 0U, &flags, 1U, false, false) &&
        x86_scalar_output_store(sel, roles[0].value, MINIC_X86_REG_R8, 8U);
}

static bool x86_source_scalar_port_io(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicX86PortIoDesc *desc;
    MinicMachineOperand bindings[2];
    MinicX86PortIo form;
    unsigned int bits;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_PORT_IO,
            &source,&roles)) return false;
    form = minic_x86_64_port_io_form(
        source->template_text,source->template_length);
    desc = minic_x86_64_port_io_description(form);
    if (desc == NULL || source->target_machine_form_variant != (uint32_t)form ||
        !source->is_volatile || source->has_memory_clobber ||
        source->explicit_memory_clobber ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        !x86_scalar_role(&roles[0],0U,desc->is_input ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        !x86_scalar_role(&roles[1],1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[0].has_tied_output || roles[1].has_tied_output ||
        roles[0].physical_read_write || roles[1].physical_read_write ||
        !(desc->is_input ?
            x86_asm_gp_lvalue_width(sel,roles[0].value,desc->width) :
            x86_asm_gp_input_width(sel,roles[0].value,desc->width)) ||
        !x86_asm_gp_input_width(sel,roles[1].value,2U) ||
        !x86_core_load(sel,roles[1].value,MINIC_X86_REG_RDX) ||
        (!desc->is_input &&
            !x86_core_load(sel,roles[0].value,MINIC_X86_REG_RAX)))
        return false;
    bits = (unsigned int)(desc->width*8U);
    bindings[0] = x86_reg(MINIC_X86_REG_RAX,bits);
    bindings[0].asm_constraint = desc->is_input ? "=a" : "a";
    bindings[0].value.physical.access = desc->is_input ?
        MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    bindings[1] = x86_reg(MINIC_X86_REG_RDX,16U);
    bindings[1].asm_constraint = "Nd";
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            0U,bindings,2U,NULL,0U,false,false) &&
        (!desc->is_input || x86_scalar_output_store(sel,roles[0].value,
            MINIC_X86_REG_RAX,desc->width));
}

static bool x86_source_scalar_atomic_rmw(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicX86AtomicRmwDesc *desc;
    MinicX86AtomicRmw form;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    size_t count, i;
    bool legacy, named;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_ATOMIC_RMW,
            &source,&roles)) return false;
    form = minic_x86_64_atomic_rmw_form(
        source->template_text,source->template_length);
    desc = minic_x86_64_atomic_rmw_description(form);
    if (desc == NULL || source->target_machine_form_variant != (uint32_t)form ||
        !source->is_volatile ||
        (desc->locked && !source->has_memory_clobber) ||
        (source->explicit_memory_clobber &&
            !source->has_memory_clobber))
        return false;
    legacy = ins->value.structured_inline_asm.operand_count ==
        (desc->unary ? 2U : 3U);
    count = 1U + (desc->unary ? 0U : 1U) + (legacy ? 1U : 0U);
    if (ins->value.structured_inline_asm.operand_count != count ||
        (legacy && (!desc->locked || desc->width != 8U ||
            (!desc->unary && strcmp(desc->opcode,"addq") != 0 &&
             strcmp(desc->opcode,"subq") != 0))) ||
        !x86_scalar_role(&roles[0],0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) ||
        roles[0].has_tied_output ||
        !x86_asm_gp_lvalue_width(sel,roles[0].value,desc->width))
        return false;
    if (!desc->unary &&
        (!x86_scalar_role(&roles[1],1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
         roles[1].has_tied_output ||
         !x86_asm_gp_input_width(sel,roles[1].value,desc->width)))
        return false;
    if (legacy &&
        (!x86_scalar_role(&roles[count-1U],count-1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) ||
         roles[count-1U].has_tied_output ||
         !x86_asm_gp_lvalue_width(sel,
            roles[count-1U].value,desc->width)))
        return false;
    if (!x86_core_load(sel,roles[0].value,MINIC_X86_REG_R10) ||
        (!desc->unary && !x86_core_load(sel,roles[1].value,
            MINIC_X86_REG_R11)) ||
        (legacy && !x86_core_load(sel,roles[count-1U].value,
            MINIC_X86_REG_R8)))
        return false;
    named = memchr(source->template_text,'[',
        source->template_length) != NULL;
    bindings[0] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[0].asm_constraint = "+m";
    bindings[0].asm_name = named ? "var" : NULL;
    if (!desc->unary) {
        bindings[1] = x86_reg(MINIC_X86_REG_R11,
            (unsigned int)(desc->width*8U));
        bindings[1].asm_constraint = "r";
        bindings[1].asm_name = named ? "val" : NULL;
        bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    if (legacy) {
        bindings[count-1U] = x86_addr(MINIC_X86_REG_R8,0);
        bindings[count-1U].asm_constraint = "m";
    }
    for (i = 0U; i < count; ++i)
        if (bindings[i].asm_constraint == NULL) return false;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        bindings,count,&flags,1U,false,false);
}


static bool x86_scalar_load_lvalue(X86CoreSelector *sel,
    MinicCoreValueId pointer, unsigned int reg, size_t width) {
    MinicX86MachineOpcode opcode;
    switch (width) {
    case 1U: opcode = MINIC_X86_MI_MOV8_AR; break;
    case 2U: opcode = MINIC_X86_MI_MOV16_AR; break;
    case 4U: opcode = MINIC_X86_MI_MOV32_AR; break;
    case 8U: opcode = MINIC_X86_MI_MOV64_AR; break;
    default: return false;
    }
    return x86_core_load(sel,pointer,MINIC_X86_REG_RDI) &&
        x86_2(sel,opcode,x86_addr(MINIC_X86_REG_RDI,0),
            x86_reg(reg,(unsigned int)(width*8U)));
}

/* Both raw CMPXCHG and try-CAS carry the same real EAX accumulator.
 * R11 owns the memory object, R10 the replacement, and hardware ZF is
 * selected only for try-CAS; original old-value and bool outputs stay live
 * until stored through independent C lvalue addresses. */
static bool x86_source_scalar_cmpxchg(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, bool trycas) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const uint32_t form_id = trycas ? MINIC_X86_MACHINE_ASM_TRYCAS :
        MINIC_X86_MACHINE_ASM_RAW_CMPXCHG;
    MinicX86TryCas form;
    const MinicX86TryCasDesc *desc;
    MinicMachineOperand bindings[4];
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    unsigned int bits;
    size_t i;
    if (!x86_scalar_source_base(sel,ins,form_id,&source,&roles))
        return false;
    form = trycas ? minic_x86_64_trycas_form(
        source->template_text,source->template_length) :
        minic_x86_64_raw_cmpxchg_form(
            source->template_text,source->template_length);
    desc = minic_x86_64_trycas_desc(form);
    if (desc == NULL || source->target_machine_form_variant != (uint32_t)form ||
        !source->is_volatile || !source->has_memory_clobber ||
        !source->explicit_memory_clobber ||
        ins->value.structured_inline_asm.operand_count != 4U)
        return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind expected = trycas ?
            i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i == 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                      MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
            i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                      MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (!x86_scalar_role(&roles[i],i,expected) ||
            roles[i].has_tied_output != (!trycas && i == 3U) ||
            (!trycas && i == 3U && roles[i].tied_output_index != 0U))
            return false;
    }
    if (!x86_asm_gp_lvalue_width(sel,roles[1].value,desc->width) ||
        !x86_asm_gp_input_width(sel,roles[3].value,desc->width) ||
        !(trycas ? x86_asm_bool_lvalue(sel,roles[0].value) &&
            x86_asm_gp_lvalue_width(sel,roles[2].value,desc->width) :
            x86_asm_gp_lvalue_width(sel,roles[0].value,desc->width) &&
            x86_asm_gp_input_width(sel,roles[2].value,desc->width)))
        return false;
    bits = (unsigned int)(desc->width*8U);
    if (!(trycas ? x86_scalar_load_lvalue(sel,roles[2].value,
            MINIC_X86_REG_RAX,desc->width) :
            x86_core_load(sel,roles[3].value,MINIC_X86_REG_RAX)) ||
        !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R11) ||
        !x86_core_load(sel,roles[trycas ? 3U : 2U].value,
            MINIC_X86_REG_R10)) return false;
    bindings[0] = trycas ? x86_reg(0U,64U) :
        x86_reg(MINIC_X86_REG_RAX,bits);
    if (trycas) {
        bindings[0].value.physical.reg.bank = MINIC_MACHINE_REGISTER_FLAGS;
        bindings[0].asm_constraint = "=@ccz";
    } else bindings[0].asm_constraint = "=a";
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[1] = x86_addr(MINIC_X86_REG_R11,0);
    bindings[1].asm_constraint = "+m";
    bindings[1].asm_name = trycas && memchr(source->template_text,'[',
        source->template_length) != NULL ? "ptr" : NULL;
    bindings[2] = x86_reg(trycas ? MINIC_X86_REG_RAX :
        MINIC_X86_REG_R10,bits);
    bindings[2].asm_constraint = trycas ? "+a" :
        desc->width == 1U ? "q" : "r";
    bindings[2].value.physical.access = trycas ?
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
        MINIC_MACHINE_REGISTER_READ;
    bindings[3] = x86_reg(trycas ? MINIC_X86_REG_R10 :
        MINIC_X86_REG_RAX,bits);
    bindings[3].asm_constraint = trycas ?
        desc->width == 1U ? "q" : "r" : "0";
    bindings[3].asm_name = trycas && memchr(source->template_text,'[',
        source->template_length) != NULL ? "new" : NULL;
    bindings[3].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,4U,&flags,1U,false,false))
        return false;
    if (trycas && !x86_1(sel,MINIC_X86_MI_SETZ8_R,
        x86_reg(MINIC_X86_REG_R9,8U)))
        return false;
    if (!x86_scalar_output_store(sel,
        roles[trycas ? 2U : 0U].value,MINIC_X86_REG_RAX,desc->width))
        return false;
    return !trycas || x86_scalar_output_store(sel,
        roles[0].value,MINIC_X86_REG_R9,1U);
}


static bool x86_source_scalar_bitops(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicX86BitopsForm form;
    const MinicX86BitopsDesc *desc;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    size_t count, mem_index, flag_index, bit_index, i;
    bool byte_rmw, required_barrier, named;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_BITOPS,
            &source,&roles)) return false;
    form = minic_x86_64_linux_bitops_form(
        source->template_text,source->template_length);
    desc = minic_x86_64_bitops_description(form);
    if (desc == NULL || source->target_machine_form_variant != (uint32_t)form)
        return false;
    byte_rmw = form == MINIC_X86_BITOPS_LOCK_OR_BYTE ||
        form == MINIC_X86_BITOPS_LOCK_AND_BYTE ||
        form == MINIC_X86_BITOPS_LOCK_XOR_BYTE;
    required_barrier = !byte_rmw ||
        form == MINIC_X86_BITOPS_LOCK_OR_BYTE;
    count = desc->has_flag ? 3U : 2U;
    mem_index = desc->has_flag && !desc->memory_output ? 1U : 0U;
    flag_index = desc->has_flag ?
        (desc->memory_output ? 1U : 0U) : SIZE_MAX;
    bit_index = count-1U;
    /* Core records the effective memory effect: a GNU +m output is an
     * implicit memory write even without a separate memory clobber.
     * Only the explicit clobber controls the source grammar policy. */
    if (source->has_memory_clobber !=
            (required_barrier || desc->memory_output) ||
        source->explicit_memory_clobber != required_barrier ||
        (desc->is_locked && !source->is_volatile) ||
        ins->value.structured_inline_asm.operand_count != count)
        return false;
    for (i = 0U; i < count; ++i) {
        MinicCoreStructuredInlineAsmOperandKind kind =
            i == mem_index ?
                (desc->memory_output ?
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) :
            i == flag_index ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (!x86_scalar_role(&roles[i],i,kind) ||
            roles[i].has_tied_output)
            return false;
    }
    if (!x86_asm_gp_lvalue_width(sel,roles[mem_index].value,
            byte_rmw ? 1U : 8U) ||
        !x86_asm_gp_input_width(sel,roles[bit_index].value,
            byte_rmw ? 4U : 8U) ||
        (desc->has_flag &&
            !x86_asm_bool_lvalue(sel,roles[flag_index].value)) ||
        !x86_core_load(sel,roles[mem_index].value,MINIC_X86_REG_R10) ||
        !x86_core_load(sel,roles[bit_index].value,MINIC_X86_REG_R11))
        return false;
    named = memchr(source->template_text,'[',
        source->template_length) != NULL;
    for (i = 0U; i < count; ++i) {
        if (i == mem_index) {
            bindings[i] = x86_addr(MINIC_X86_REG_R10,0);
            bindings[i].asm_constraint = desc->memory_output ? "+m" : "m";
            bindings[i].asm_name = named ? "var" : NULL;
        } else if (i == flag_index) {
            bindings[i] = x86_reg(0U,64U);
            bindings[i].value.physical.reg.bank =
                MINIC_MACHINE_REGISTER_FLAGS;
            bindings[i].value.physical.access =
                MINIC_MACHINE_REGISTER_WRITE;
            bindings[i].asm_constraint = "=@ccc";
        } else {
            bindings[i] = x86_reg(MINIC_X86_REG_R11,
                byte_rmw ? 8U : 64U);
            bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_READ;
            bindings[i].asm_constraint = byte_rmw ? "iq" : "Ir";
            bindings[i].asm_name = named ? "val" : NULL;
        }
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                (desc->writes_memory ? MINIC_MACHINE_EFFECT_MEMORY_WRITE : 0U),
            bindings,count,&flags,1U,false,false))
        return false;
    return !desc->has_flag ||
        (x86_1(sel,MINIC_X86_MI_SETC8_R,
            x86_reg(MINIC_X86_REG_R9,8U)) &&
         x86_scalar_output_store(sel,roles[flag_index].value,
            MINIC_X86_REG_R9,1U));
}

static bool x86_source_scalar_atomic_cc(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicX86AtomicCCDesc *desc;
    MinicX86AtomicCC form;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    size_t count;
    bool named, locked;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_ATOMIC_CC,
            &source,&roles)) return false;
    form = minic_x86_64_atomic_cc_form(
        source->template_text,source->template_length);
    desc = minic_x86_64_atomic_cc_description(form);
    locked = minic_x86_64_atomic_cc_locked_form(
        source->template_text,source->template_length);
    count = desc != NULL && desc->has_value ? 3U : 2U;
    if (desc == NULL ||
        source->target_machine_form_variant !=
            ((uint32_t)form |
                (locked ? MINIC_X86_ATOMIC_CC_LOCK_VARIANT : 0U)) ||
        !source->is_volatile || !source->has_memory_clobber ||
        !source->explicit_memory_clobber ||
        ins->value.structured_inline_asm.operand_count != count ||
        !x86_scalar_role(&roles[0],0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) ||
        !x86_scalar_role(&roles[1],1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        roles[0].has_tied_output || roles[1].has_tied_output ||
        !x86_asm_gp_lvalue_width(sel,roles[0].value,desc->width) ||
        !x86_asm_bool_lvalue(sel,roles[1].value))
        return false;
    if (desc->has_value &&
        (!x86_scalar_role(&roles[2],2U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
         roles[2].has_tied_output ||
         !x86_asm_gp_input_width(sel,roles[2].value,desc->width)))
        return false;
    if (!x86_core_load(sel,roles[0].value,MINIC_X86_REG_R10) ||
        (desc->has_value && !x86_core_load(sel,roles[2].value,
            MINIC_X86_REG_R11)))
        return false;
    named = memchr(source->template_text,'[',
        source->template_length) != NULL;
    bindings[0] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[0].asm_constraint = "+m";
    bindings[0].asm_name = named ? "var" : NULL;
    bindings[1] = x86_reg(0U,64U);
    bindings[1].value.physical.reg.bank = MINIC_MACHINE_REGISTER_FLAGS;
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[1].asm_constraint = desc->flag_sign ? "=@ccs" : "=@cce";
    if (desc->has_value) {
        bindings[2] = x86_reg(MINIC_X86_REG_R11,
            (unsigned int)(desc->width*8U));
        bindings[2].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bindings[2].asm_constraint = "er";
        bindings[2].asm_name = named ? "val" : NULL;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,count,&flags,1U,false,false) &&
        x86_1(sel,desc->flag_sign ? MINIC_X86_MI_SETS8_R :
            MINIC_X86_MI_SETZ8_R,x86_reg(MINIC_X86_REG_R9,8U)) &&
        x86_scalar_output_store(sel,roles[1].value,MINIC_X86_REG_R9,1U);
}

static bool x86_source_mxcsr(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand binding;
    unsigned int form;
    bool reading_memory, named;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    form = minic_x86_64_linux_mxcsr_form(
        source->template_text,source->template_length);
    reading_memory = form == 1U;
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_MXCSR ||
        (form != 1U && form != 2U) ||
        source->target_machine_form_variant != form ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        roles == NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length))
        return false;
    if (roles[0].operand_index != 0U ||
        roles[0].kind != (reading_memory ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT) ||
        roles[0].has_fixed_register_binding || roles[0].early_clobber ||
        roles[0].physical_early_clobber ||
        roles[0].physical_read_write || roles[0].has_tied_output ||
        !x86_asm_u32_lvalue(sel,roles[0].value) ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_R10))
        return false;
    named = memchr(source->template_text,'[',
        source->template_length) != NULL;
    binding = x86_addr(MINIC_X86_REG_R10,0);
    binding.asm_constraint = reading_memory ? "m" : "=m";
    binding.asm_name = named ? "val" : NULL;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        reading_memory ? MINIC_MACHINE_EFFECT_MEMORY_READ :
            MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        &binding,1U,NULL,0U,false,false);
}

/* ORC snapshots the *live* RIP/RSP/RBP, each into a distinct physical
 * 64-bit GPR. Materialize all three first, then store their values using a
 * separate pointer scratch; no output store may overwrite another snapshot. */
static bool x86_source_unwind_context(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int output_registers[3] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_R10, MINIC_X86_REG_R11
    };
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3];
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_UNWIND_CONTEXT ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        roles == NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length))
        return false;
    for (i = 0U; i < 3U; ++i) {
        if (roles[i].operand_index != i ||
            roles[i].kind !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write || roles[i].has_tied_output ||
            !x86_asm_u64_lvalue(sel,roles[i].value))
            return false;
        bindings[i] = x86_reg(output_registers[i],64U);
        bindings[i].asm_constraint = "=r";
        bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            0U,bindings,3U,NULL,0U,false,false))
        return false;
    for (i = 0U; i < 3U; ++i) {
        if (!x86_core_load(sel,roles[i].value,MINIC_X86_REG_RDI) ||
            !x86_2(sel,MINIC_X86_MI_MOV64_RA,
                x86_reg(output_registers[i],64U),
                x86_addr(MINIC_X86_REG_RDI,0)))
            return false;
    }
    return true;
}

static bool x86_source_pkru(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int read_registers[3] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX, MINIC_X86_REG_RCX
    };
    static const unsigned int write_registers[3] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RCX, MINIC_X86_REG_RDX
    };
    static const char *const read_constraints[3] = {"=a", "=d", "c"};
    static const char *const write_constraints[3] = {"a", "c", "d"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3];
    const unsigned int *registers;
    const char *const *constraints;
    unsigned int form;
    size_t outputs, i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    form = minic_x86_64_linux_pkru_form(
        source->template_text,source->template_length);
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_PKRU ||
        form < 1U || form > 2U ||
        source->target_machine_form_variant != form ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        roles == NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length))
        return false;
    outputs = form == 1U ? 2U : 0U;
    registers = form == 1U ? read_registers : write_registers;
    constraints = form == 1U ? read_constraints : write_constraints;
    for (i = 0U; i < 3U; ++i) {
        const bool output = i < outputs;
        if (roles[i].operand_index != i ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write || roles[i].has_tied_output ||
            !(output ? x86_asm_gp_lvalue_width(sel,roles[i].value,4U) :
                x86_asm_gp_input_width(sel,roles[i].value,4U)))
            return false;
    }
    /* The input ECX for reads, and EAX/ECX/EDX for writes, must all be
     * loaded before the byte sequence. Output addresses come afterward so
     * pointer calculations cannot overwrite fixed architectural operands. */
    for (i = outputs; i < 3U; ++i)
        if (!x86_core_load(sel,roles[i].value,registers[i]))
            return false;
    for (i = 0U; i < 3U; ++i) {
        bindings[i] = x86_reg(registers[i],32U);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = i < outputs ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            0U,bindings,3U,NULL,0U,false,false))
        return false;
    for (i = 0U; i < outputs; ++i) {
        if (!x86_core_load(sel,roles[i].value,MINIC_X86_REG_R10) ||
            !x86_2(sel,MINIC_X86_MI_MOV32_RA,
                x86_reg(registers[i],32U),
                x86_addr(MINIC_X86_REG_R10,0)))
            return false;
    }
    return true;
}

/* Linux ALTERNATIVE_WRMSR is an entire patchable instruction site, not an
 * ordinary WRMSR. All three hardware inputs must be loaded into ECX:EAX:EDX
 * before the source-owned .altinstructions record is serialized. The feature
 * bit remains a verified immediate rather than a late textual substitution. */
static bool x86_source_alternative_wrmsr(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[3] = {
        MINIC_X86_REG_RCX, MINIC_X86_REG_RAX, MINIC_X86_REG_RDX
    };
    static const char *const constraints[4] = {"c", "a", "d", "i"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreInstruction *feature;
    MinicMachineOperand bindings[4];
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 4U)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_ALTERNATIVE_WRMSR ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,0U,
            source->template_text,source->template_length)) return false;
    for (i = 0U; i < 4U; ++i) {
        if (roles[i].operand_index != i ||
            roles[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write || roles[i].has_tied_output ||
            roles[i].value >= sel->core->value_count ||
            (i < 3U && !x86_asm_gp_input_width(sel,roles[i].value,4U)))
            return false;
    }
    if (!minic_core_function_value_is_kind(sel->core,
            roles[3].value,MINIC_CORE_TYPE_INTEGER) ||
        sel->core->values[roles[3].value].definition >=
            sel->core->instruction_count) return false;
    feature = &sel->core->instructions[
        sel->core->values[roles[3].value].definition];
    if (feature->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        feature->result != roles[3].value ||
        feature->value.integer_value < 0 ||
        (uint64_t)feature->value.integer_value > UINT32_MAX) return false;
    for (i = 0U; i < 3U; ++i) {
        if (!x86_core_load(sel,roles[i].value,regs[i])) return false;
        bindings[i] = x86_reg(regs[i],32U);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    bindings[3] = x86_imm(feature->value.integer_value);
    bindings[3].asm_constraint = (char *)constraints[3];
    bindings[3].asm_name = "feature";
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        0U,bindings,4U,NULL,0U,false,false);
}

/* Both AMD APIC Linux alternatives are complete GNU patch sites. The MMIO
 * variant has a real output, tied input and two independently qualified
 * memory operands; the divider variant fixes EAX/EDX and its divisor to
 * source-proven integer constants. Preserve both forms as physical MIR. */
static bool x86_apic_core_constant(const X86CoreSelector *sel,
    MinicCoreValueId value, int64_t expected) {
    const MinicCoreInstruction *constant;
    if (value >= sel->core->value_count ||
        !minic_core_function_value_is_kind(sel->core,value,
            MINIC_CORE_TYPE_INTEGER) ||
        sel->core->values[value].definition >=
            sel->core->instruction_count) return false;
    constant = &sel->core->instructions[
        sel->core->values[value].definition];
    return constant->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT &&
        constant->result == value &&
        constant->value.integer_value == expected;
}

static bool x86_source_apic_amd_alt(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[5];
    const unsigned int regs[3] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX, MINIC_X86_REG_R10
    };
    const char *const constraints[3] = {"a","d","r"};
    unsigned int form;
    size_t i;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    form = source->target_machine_form_variant;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_APIC_AMD_ALT ||
        (form != 1U && form != 2U) ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto ||
        source->has_memory_clobber != (form == 1U) ||
        source->explicit_memory_clobber ||
        source->implicit_memory_effect != (form == 1U) ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        roles == NULL || ins->value.structured_inline_asm.operand_count !=
            (form == 1U ? 5U : 3U) ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,form,
            source->template_text,source->template_length)) return false;
    for (i = 0U; i < (form == 1U ? 5U : 3U); ++i)
        if (roles[i].operand_index != i ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber || roles[i].physical_read_write ||
            roles[i].has_tied_output != (form == 1U && i == 3U) ||
            (form == 1U && i == 3U && roles[i].tied_output_index != 0U))
            return false;
    if (form == 1U) {
        if (roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
            roles[2].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            roles[3].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            roles[4].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
            !x86_asm_gp_lvalue_width(sel,roles[0].value,4U) ||
            !x86_asm_gp_lvalue_width(sel,roles[1].value,4U) ||
            !x86_apic_core_constant(sel,roles[2].value,0) ||
            !x86_asm_gp_input_width(sel,roles[3].value,4U) ||
            !x86_asm_gp_lvalue_width(sel,roles[4].value,4U) ||
            !x86_core_load(sel,roles[3].value,MINIC_X86_REG_RAX) ||
            !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R10) ||
            !x86_core_load(sel,roles[4].value,MINIC_X86_REG_R11))
            return false;
        bindings[0] = x86_reg(MINIC_X86_REG_RAX,32U);
        bindings[0].asm_constraint = "=r";
        bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        bindings[1] = x86_addr(MINIC_X86_REG_R10,0);
        bindings[1].asm_constraint = "=m";
        bindings[2] = x86_imm(0);
        bindings[2].asm_constraint = "i";
        bindings[3] = x86_reg(MINIC_X86_REG_RAX,32U);
        bindings[3].asm_constraint = "0";
        bindings[3].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bindings[4] = x86_addr(MINIC_X86_REG_R11,0);
        bindings[4].asm_constraint = "m";
        return minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,5U,NULL,0U,false,false) &&
            x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RAX,4U);
    }
    for (i = 0U; i < 3U; ++i) {
        if (roles[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            !x86_apic_core_constant(sel,roles[i].value,i == 2U ? 1 : 0) ||
            !x86_asm_gp_input_width(sel,roles[i].value,4U) ||
            !x86_core_load(sel,roles[i].value,regs[i])) return false;
        bindings[i] = x86_reg(regs[i],32U);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source,0U,bindings,3U,NULL,0U,false,false);
}

/* The XCR0/XSS hardware write consumes all three 32-bit fixed registers.
 * XGETBV is handled by the counter family, but XSETBV has no output pair. */
static bool x86_source_xsetbv(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[3] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX, MINIC_X86_REG_RCX
    };
    static const char *const constraints[3] = {"a", "d", "c"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3];
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count || sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 3U)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_XSETBV ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto || source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,0U,
            source->template_text,source->template_length)) return false;
    for (i = 0U; i < 3U; ++i) {
        if (roles[i].operand_index != i ||
            roles[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber || roles[i].physical_read_write ||
            roles[i].has_tied_output ||
            !x86_asm_gp_input_width(sel,roles[i].value,4U) ||
            !x86_core_load(sel,roles[i].value,regs[i])) return false;
        bindings[i] = x86_reg(regs[i],32U);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        0U,bindings,3U,NULL,0U,false,false);
}

/* Linux 6.6 idle and PAT cacheline alternatives share a validated
 * fixed-register cache-address contract, but NOT the same patch records.
 * The idle form patches a NOP site into CLFLUSH on RAX; PAT replaces the
 * 3E-prefixed CLFLUSH on R10 by its 66-prefixed CLFLUSHOPT spelling.
 * The Source qualifier freezes each complete GNU section grammar, while
 * the physical machine verifier separately checks these exact bindings. */
/* The five Linux idle instructions are fixed-register hardware interfaces,
 * despite being spelled as raw opcode bytes in kernel C for old assemblers.
 * MWAITX consumes RBX, which the SysV callee must preserve explicitly. */
static bool x86_source_monitor_wait(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int acd[] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RCX, MINIC_X86_REG_RDX
    };
    static const unsigned int abc[] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_RBX, MINIC_X86_REG_RCX
    };
    static const char *const acd_constraints[] = {"a","c","d"};
    static const char *const abc_constraints[] = {"a","b","c"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const unsigned int *regs;
    const char *const *constraints;
    MinicMachineOperand bindings[3];
    unsigned int variant;
    size_t count, i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_MONITOR_WAIT)
        return false;
    variant = minic_x86_64_linux_monitor_wait_form(
        source->template_text,source->template_length);
    count = variant == 1U || variant == 2U || variant == 4U ? 3U : 2U;
    regs = variant == 4U ? abc : acd;
    constraints = variant == 4U ? abc_constraints : acd_constraints;
    if (variant == 0U || source->target_machine_form_variant != variant ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->source_asm_unique_id == 0U ||
        ins->value.structured_inline_asm.operand_count != count ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,variant,
            source->template_text,source->template_length)) return false;
    roles = ins->value.structured_inline_asm.operands;
    if (variant == 4U &&
        !x86_1(sel,MINIC_X86_MI_PUSH64_R,
            x86_reg(MINIC_X86_REG_RBX,64U))) return false;
    for (i = 0U; i < count; ++i) {
        const bool full = x86_asm_gp_input_width(sel,roles[i].value,8U);
        if (!x86_scalar_role(&roles[i],i,
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].physical_read_write || roles[i].has_tied_output ||
            (!full && ((i == 0U && variant <= 2U) ||
             !x86_asm_gp_input_width(sel,roles[i].value,4U))) ||
            !x86_core_load(sel,roles[i].value,regs[i])) return false;
        bindings[i] = x86_reg(regs[i],full ? 64U : 32U);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,0U,bindings,count,NULL,0U,false,false) &&
        (variant != 4U || x86_1(sel,MINIC_X86_MI_POP64_R,
            x86_reg(MINIC_X86_REG_RBX,64U)));
}

/* Segment-selector reads produce 16-bit or zero-extended 32-bit values.
 * Explicit Source `memory` clobbers remain compiler barriers, and the
 * chosen physical register is written back to the C lvalue after the ASM. */
static bool x86_source_segment_read(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *role;
    MinicMachineOperand output;
    unsigned int variant, bits;
    size_t width;
    bool short_selector;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_SEGMENT_READ)
        return false;
    variant = minic_x86_64_linux_segment_read_form(
        source->template_text,source->template_length);
    short_selector = variant <= 2U || variant >= 7U;
    role = ins->value.structured_inline_asm.operands;
    if (variant == 0U || source->target_machine_form_variant != variant ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber || !source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->source_asm_unique_id == 0U ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,variant,
            source->template_text,source->template_length) ||
        !x86_scalar_role(role,0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        role->physical_read_write || role->has_tied_output ||
        (!x86_asm_gp_lvalue_width(sel,role->value,2U) &&
         !x86_asm_gp_lvalue_width(sel,role->value,4U))) return false;
    width = x86_asm_gp_lvalue_width(sel,role->value,2U) ? 2U : 4U;
    if (!short_selector && width != 4U) return false;
    bits = (unsigned int)(width * 8U);
    output = x86_reg(MINIC_X86_REG_R10,bits);
    output.asm_constraint = "=r";
    output.value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            0U,&output,1U,NULL,0U,false,false) &&
        x86_scalar_output_store(sel,role->value,MINIC_X86_REG_R10,width);
}

static bool x86_source_cache_flush_alt(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreInstruction *constant;
    MinicMachineOperand bindings[2];
    bool idle;
    if (!ins || ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        !sel || !sel->core || !sel->core->inline_asms ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    idle = source->target_machine_form_id ==
        MINIC_X86_MACHINE_ASM_IDLE_CLFLUSH_ALT;
    if (!idle && source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_PAT_CLFLUSHOPT) return false;
    roles = ins->value.structured_inline_asm.operands;
    if (!roles || source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto || !source->has_memory_clobber ||
        source->explicit_memory_clobber || !source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id, 0U,
            source->template_text,source->template_length)) return false;
    if (roles[0].operand_index != 0U ||
        roles[0].kind != (idle ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) ||
        roles[1].operand_index != 1U ||
        roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        roles[0].has_fixed_register_binding ||
        roles[1].has_fixed_register_binding ||
        roles[0].early_clobber || roles[1].early_clobber ||
        roles[0].physical_early_clobber ||
        roles[1].physical_early_clobber ||
        roles[0].has_tied_output || roles[1].has_tied_output ||
        roles[0].physical_read_write != !idle ||
        roles[1].physical_read_write) return false;
    if ((idle ? !x86_asm_gp_input_width(sel,roles[1].value,8U) :
            !x86_asm_gp_lvalue_width(sel,roles[0].value,1U)) ||
        (idle ? !minic_core_function_value_is_kind(sel->core,
            roles[0].value,MINIC_CORE_TYPE_INTEGER) :
            !minic_core_function_value_is_kind(sel->core,
                roles[1].value,MINIC_CORE_TYPE_INTEGER))) return false;
    {
        const MinicCoreValueId value = roles[idle ? 0U : 1U].value;
        if (value >= sel->core->value_count ||
            sel->core->values[value].definition >=
                sel->core->instruction_count) return false;
        constant = &sel->core->instructions[
            sel->core->values[value].definition];
        if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
            constant->result != value ||
            (idle && constant->value.integer_value != 0))
            return false;
    }
    if (!x86_core_load(sel,roles[idle ? 1U : 0U].value,
            idle ? MINIC_X86_REG_RAX : MINIC_X86_REG_R10)) return false;
    if (idle) {
        bindings[0] = x86_imm(0);
        bindings[0].asm_constraint = "i";
        bindings[1] = x86_reg(MINIC_X86_REG_RAX,64U);
        bindings[1].asm_constraint = "a";
        bindings[1].asm_name = "addr";
        bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    } else {
        bindings[0] = x86_addr(MINIC_X86_REG_R10,0);
        bindings[0].asm_constraint = "+m";
        bindings[1] = x86_imm(constant->value.integer_value);
        bindings[1].asm_constraint = "i";
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        idle ? MINIC_MACHINE_EFFECT_MEMORY_READ :
            (MINIC_MACHINE_EFFECT_MEMORY_READ |
             MINIC_MACHINE_EFFECT_MEMORY_WRITE),
        bindings,2U,NULL,0U,false,false);
}

/* Linux's preemption count is a GS-relative 32-bit memory decrement whose
 * zero flag is a C bool result. The `=@cce` output is not an integer register
 * and must be materialized from the real hardware ZF after the sealed ASM. */
static bool x86_source_preempt_gs_dec(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[2];
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count || sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 2U)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_PREEMPT_GS_DEC ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto || !source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,0U,
            source->template_text,source->template_length) ||
        !x86_scalar_role(&roles[0],0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) ||
        !x86_scalar_role(&roles[1],1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        roles[0].has_tied_output || roles[1].has_tied_output ||
        !x86_asm_gp_lvalue_width(sel,roles[0].value,4U) ||
        !x86_asm_bool_lvalue(sel,roles[1].value) ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_R10))
        return false;
    bindings[0] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[0].value.address.has_segment = true;
    bindings[0].value.address.segment = (MinicMachineRegister){
        MINIC_X86_SEG_GS,16U,MINIC_MACHINE_REGISTER_SYSTEM};
    bindings[0].asm_constraint = "+m";
    bindings[0].asm_name = "var";
    bindings[1] = x86_reg(0U,64U);
    bindings[1].value.physical.reg.bank = MINIC_MACHINE_REGISTER_FLAGS;
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[1].asm_constraint = "=@cce";
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,2U,&flags,1U,false,false) &&
        x86_1(sel,MINIC_X86_MI_SETZ8_R,
            x86_reg(MINIC_X86_REG_R9,8U)) &&
        x86_scalar_output_store(sel,roles[1].value,MINIC_X86_REG_R9,1U);
}

/* Linux's boot-time memcmp takes its three C +register lvalues in the
 * architectural REP CMPSB registers. The flag result is the real hardware
 * NZ condition: a post-asm comparison would observe a different value.
 * Preserve all three updated GNU read/write outputs before RDI is reused
 * to address their C lvalues. */
static bool x86_source_boot_cmpsb(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[3] = {
        MINIC_X86_REG_RDI, MINIC_X86_REG_RSI, MINIC_X86_REG_RCX
    };
    static const char *const constraints[3] = {"+D", "+S", "+c"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[4];
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    size_t i;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_BOOT_STRING,
            &source,&roles) ||
        source->target_machine_form_variant != 1U || !source->is_volatile ||
        !source->has_memory_clobber || source->explicit_memory_clobber ||
        !source->implicit_memory_effect ||
        ins->value.structured_inline_asm.operand_count != 4U ||
        !x86_scalar_role(&roles[0],0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        roles[0].physical_read_write || roles[0].has_tied_output ||
        !x86_asm_bool_lvalue(sel,roles[0].value)) return false;
    for (i = 1U; i < 4U; ++i) {
        if (!x86_scalar_role(&roles[i],i,
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) ||
            !roles[i].physical_read_write || roles[i].has_tied_output ||
            !x86_asm_gp_lvalue_width(sel,roles[i].value,8U)) return false;
    }
    /* The common load helper addresses C locals through RDI. Load +D last. */
    if (!x86_scalar_load_lvalue(sel,roles[3].value,MINIC_X86_REG_RCX,8U) ||
        !x86_scalar_load_lvalue(sel,roles[2].value,MINIC_X86_REG_RSI,8U) ||
        !x86_scalar_load_lvalue(sel,roles[1].value,MINIC_X86_REG_RDI,8U))
        return false;
    bindings[0] = x86_reg(0U,64U);
    bindings[0].value.physical.reg.bank = MINIC_MACHINE_REGISTER_FLAGS;
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[0].asm_constraint = "=@ccnz";
    for (i = 1U; i < 4U; ++i) {
        bindings[i] = x86_reg(regs[i-1U],64U);
        bindings[i].asm_constraint = (char *)constraints[i-1U];
        bindings[i].value.physical.access =
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,4U,&flags,1U,false,false) &&
        x86_1(sel,MINIC_X86_MI_SETNZ8_R,x86_reg(MINIC_X86_REG_R9,8U)) &&
        x86_rr(sel,MINIC_X86_MI_MOV64_RR,64U,
            MINIC_X86_REG_RDI,MINIC_X86_REG_RAX) &&
        x86_rr(sel,MINIC_X86_MI_MOV64_RR,64U,
            MINIC_X86_REG_RSI,MINIC_X86_REG_RDX) &&
        x86_rr(sel,MINIC_X86_MI_MOV64_RR,64U,
            MINIC_X86_REG_RCX,MINIC_X86_REG_R10) &&
        x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_R9,1U) &&
        x86_scalar_output_store(sel,roles[1].value,MINIC_X86_REG_RAX,8U) &&
        x86_scalar_output_store(sel,roles[2].value,MINIC_X86_REG_RDX,8U) &&
        x86_scalar_output_store(sel,roles[3].value,MINIC_X86_REG_R10,8U);
}

/* Linux's unsigned 64/32 division helper is an architectural EDX:EAX
 * operation. GNU "0" and "1" are tied inputs, not extra independent GP
 * operands; the two resulting 32-bit outputs are stored separately. */
static bool x86_source_boot_divl(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[5] = {
        MINIC_X86_REG_RAX,MINIC_X86_REG_RDX,MINIC_X86_REG_R10,
        MINIC_X86_REG_RAX,MINIC_X86_REG_RDX
    };
    static const char *const constraints[5] = {"=a","=d","rm","0","1"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[5];
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    size_t i;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_BOOT_STRING,
            &source,&roles) ||
        source->target_machine_form_variant != 2U || !source->is_volatile ||
        source->has_memory_clobber || source->implicit_memory_effect ||
        ins->value.structured_inline_asm.operand_count != 5U) return false;
    for (i = 0U; i < 5U; ++i) {
        const bool output = i < 2U;
        if (!x86_scalar_role(&roles[i],i,output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].physical_read_write ||
            (i >= 3U ? (!roles[i].has_tied_output ||
                roles[i].tied_output_index != i-3U) :
                roles[i].has_tied_output) ||
            (output ? !x86_asm_gp_lvalue_width(sel,roles[i].value,4U) :
                !x86_asm_gp_input_width(sel,roles[i].value,4U))) return false;
    }
    for (i = 2U; i < 5U; ++i) {
        if (!x86_core_load(sel,roles[i].value,regs[i])) return false;
    }
    for (i = 0U; i < 5U; ++i) {
        bindings[i] = x86_reg(regs[i],32U);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = i < 2U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            0U,bindings,5U,&flags,1U,false,false) &&
        x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RAX,4U) &&
        x86_scalar_output_store(sel,roles[1].value,MINIC_X86_REG_RDX,4U);
}

/* Original x86 boot memcpy uses the complete pair of REP MOVS operations.
 * Its early-clobber outputs are RCX/RDI/RSI, tied to source count/dest/src;
 * the independent 'g' remainder is in R10 across the first REP. */
static bool x86_source_boot_movs(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[7] = {
        MINIC_X86_REG_RCX,MINIC_X86_REG_RDI,MINIC_X86_REG_RSI,
        MINIC_X86_REG_RCX,MINIC_X86_REG_R10,
        MINIC_X86_REG_RDI,MINIC_X86_REG_RSI
    };
    static const char *const constraints[7] = {
        "=&c","=&D","=&S","0","g","1","2"
    };
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[7];
    size_t i;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_BOOT_STRING,
            &source,&roles) ||
        source->target_machine_form_variant != 3U || !source->is_volatile ||
        !source->has_memory_clobber || !source->explicit_memory_clobber ||
        !source->implicit_memory_effect ||
        ins->value.structured_inline_asm.operand_count != 7U) return false;
    for (i = 0U; i < 7U; ++i) {
        const bool output = i < 3U;
        if (roles[i].operand_index != i ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding ||
            roles[i].physical_read_write ||
            (output ? !roles[i].physical_early_clobber :
                roles[i].physical_early_clobber) ||
            (i == 3U || i >= 5U ?
                (!roles[i].has_tied_output ||
                 roles[i].tied_output_index != (i == 3U ? 0U : i-4U)) :
                roles[i].has_tied_output) ||
            (output ? !x86_asm_gp_lvalue_width(sel,roles[i].value,8U) :
                !x86_asm_gp_input_width(sel,roles[i].value,8U))) return false;
    }
    for (i = 3U; i < 7U; ++i)
        if (!x86_core_load(sel,roles[i].value,regs[i])) return false;
    for (i = 0U; i < 7U; ++i) {
        bindings[i] = x86_reg(regs[i],64U);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = i < 3U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,7U,NULL,0U,false,false) &&
        x86_rr(sel,MINIC_X86_MI_MOV64_RR,64U,
            MINIC_X86_REG_RDI,MINIC_X86_REG_RAX) &&
        x86_rr(sel,MINIC_X86_MI_MOV64_RR,64U,
            MINIC_X86_REG_RSI,MINIC_X86_REG_RDX) &&
        x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RCX,8U) &&
        x86_scalar_output_store(sel,roles[1].value,MINIC_X86_REG_RAX,8U) &&
        x86_scalar_output_store(sel,roles[2].value,MINIC_X86_REG_RDX,8U);
}

/* CR0/CR2/CR3/CR4 have an architectural read/write ABI independent of
 * general-purpose register allocation. Linux's optional dummy "m" input
 * (address 0x1000) is a compile-time ordering dependency, not a physical
 * memory read; carry its distinct typed ADDRESS operand without dereference.
 * The actual privileged MOV only touches CRn and RAX. */
static bool x86_source_control_register(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[2];
    const MinicCoreType *fake_type;
    MinicCoreTypeId pointee;
    unsigned int form;
    bool read, tied_write, dummy;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_CONTROL_REGISTER,
            &source,&roles)) return false;
    form = source->target_machine_form_variant;
    read = form >= 1U && form <= 4U;
    tied_write = (form == 7U || form == 8U) &&
        ins->value.structured_inline_asm.operand_count == 1U &&
        roles[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE;
    dummy = read && ins->value.structured_inline_asm.operand_count == 2U;
    if (form < 1U || form > 8U || !source->is_volatile ||
        !source->has_memory_clobber || !source->implicit_memory_effect ||
        source->explicit_memory_clobber !=
            (form == 5U || form == 6U || tied_write) ||
        ins->value.structured_inline_asm.operand_count !=
            (dummy ? 2U : 1U) ||
        !x86_scalar_role(&roles[0],0U,read ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT : tied_write ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[0].physical_read_write != tied_write ||
        roles[0].has_tied_output ||
        (read || tied_write ?
            !x86_asm_gp_lvalue_width(sel,roles[0].value,8U) :
            !x86_asm_gp_input_width(sel,roles[0].value,8U))) return false;
    if (dummy) {
        if (!x86_scalar_role(&roles[1],1U,
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) ||
            roles[1].physical_read_write || roles[1].has_tied_output ||
            roles[1].value >= sel->core->value_count ||
            !minic_lp64_core_pointer_pointee(sel->core->type_context,
                sel->core->values[roles[1].value].core_type_id,&pointee) ||
            (fake_type = x86_type(sel,pointee)) == NULL ||
            fake_type->kind != MINIC_CORE_TYPE_INTEGER ||
            !fake_type->has_layout || fake_type->size != 4U ||
            fake_type->value.integer.is_bool ||
            !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R10)) return false;
    }
    if (tied_write &&
        !x86_scalar_load_lvalue(sel,roles[0].value,MINIC_X86_REG_RAX,8U))
        return false;
    if (!read && !tied_write &&
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_RAX))
        return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX,64U);
    bindings[0].asm_constraint = read ? "=r" : tied_write ? "+r" : "r";
    bindings[0].value.physical.access = read ?
        MINIC_MACHINE_REGISTER_WRITE : tied_write ?
        (MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE) :
        MINIC_MACHINE_REGISTER_READ;
    if (dummy) {
        bindings[1] = x86_addr(MINIC_X86_REG_R10,0);
        bindings[1].asm_constraint = "m";
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            read ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,dummy ? 2U : 1U,NULL,0U,false,false) &&
        (!(read || tied_write) ||
            x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RAX,8U));
}

/* Linux's early GDT setup writes DS, SS and ES from AX in one precisely
 * admitted source occurrence. The scalar arrives through the real RAX input;
 * there is no old Core->FILE* escape or untyped segment-register operation. */
static bool x86_source_boot_segments(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand binding;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_BOOT_SEGMENTS,
            &source,&roles) ||
        source->target_machine_form_variant != 1U ||
        !source->is_volatile || !source->has_memory_clobber ||
        !source->implicit_memory_effect ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        !x86_scalar_role(&roles[0],0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[0].physical_read_write || roles[0].has_tied_output ||
        (!x86_asm_gp_input_width(sel,roles[0].value,2U) &&
         !x86_asm_gp_input_width(sel,roles[0].value,4U) &&
         !x86_asm_gp_input_width(sel,roles[0].value,8U)) ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_RAX))
        return false;
    binding = x86_reg(MINIC_X86_REG_RAX,32U);
    binding.asm_constraint = "a";
    binding.value.physical.access = MINIC_MACHINE_REGISTER_READ;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        MINIC_MACHINE_EFFECT_MEMORY_READ |
        MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        &binding,1U,NULL,0U,false,false);
}

/* All 12 Linux debug-register forms: six reads and six writes. DR7's fake
 * 32-bit `m` dependency is an ADDRESS in MachineIR and is NEVER dereferenced.
 * The source's nonvolatile vs volatile distinction remains exact. */
/* The complete loadsegment() GNU source has one 16-bit selector input and
 * a target-owned three-word fault fixup. SourcePolicy seals each FS/GS/DS/
 * ES/SS variant; MachineIR records the exact physical segment operation
 * with a fixed AX selector and its memory/compiler-barrier effect. */
static bool x86_source_segment_load_extable(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand binding;
    unsigned int form;
    if (!x86_scalar_source_base(sel,ins,
            MINIC_X86_MACHINE_ASM_SEGMENT_LOAD_EXTABLE,&source,&roles))
        return false;
    form = source->target_machine_form_variant;
    if (form < 1U || form > 5U || !source->is_volatile || source->is_goto ||
        !source->explicit_memory_clobber ||
        !source->has_memory_clobber || !source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        !x86_scalar_role(&roles[0],0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[0].physical_read_write || roles[0].has_tied_output ||
        !x86_asm_gp_input_width(sel,roles[0].value,2U) ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_RAX)) return false;
    binding = x86_reg(MINIC_X86_REG_RAX,16U);
    binding.asm_constraint = "rm";
    binding.value.physical.access = MINIC_MACHINE_REGISTER_READ;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        MINIC_MACHINE_EFFECT_MEMORY_WRITE,&binding,1U,NULL,0U,false,false);
}

static bool x86_source_debug_register(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[2];
    const MinicCoreType *fake_type;
    MinicCoreTypeId pointee;
    unsigned int form;
    bool read, dr7;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_DEBUG_REGISTER,
            &source,&roles)) return false;
    form = source->target_machine_form_variant;
    read = form >= 1U && form <= 6U;
    dr7 = form == 6U || form == 12U;
    /* Core conservatively raises every structured source ASM to volatile
     * with the source-qualified effective memory dependency. The C ingress
     * proved the five plain DR reads lack GNU volatile, the six no-output
     * writes are implicitly GNU-volatile, and DR7's fake dependency is
     * explicitly volatile. None claims an explicit memory clobber. */
    if (form < 1U || form > 12U || !source->is_volatile ||
        !source->has_memory_clobber || source->explicit_memory_clobber ||
        !source->implicit_memory_effect ||
        ins->value.structured_inline_asm.operand_count != (dr7 ? 2U : 1U) ||
        !x86_scalar_role(&roles[0],0U,read ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[0].physical_read_write || roles[0].has_tied_output ||
        (read ? !x86_asm_gp_lvalue_width(sel,roles[0].value,8U) :
            !x86_asm_gp_input_width(sel,roles[0].value,8U)))
        return false;
    if (dr7) {
        if (!x86_scalar_role(&roles[1],1U,
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) ||
            roles[1].physical_read_write || roles[1].has_tied_output ||
            roles[1].value >= sel->core->value_count ||
            !minic_lp64_core_pointer_pointee(sel->core->type_context,
                sel->core->values[roles[1].value].core_type_id,&pointee) ||
            (fake_type = x86_type(sel,pointee)) == NULL ||
            fake_type->kind != MINIC_CORE_TYPE_INTEGER ||
            !fake_type->has_layout || fake_type->size != 4U ||
            fake_type->value.integer.is_bool ||
            !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R10))
            return false;
    }
    if (!read && !x86_core_load(sel,roles[0].value,MINIC_X86_REG_RAX))
        return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX,64U);
    bindings[0].asm_constraint = read ? "=r" : "r";
    bindings[0].value.physical.access = read ?
        MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    if (dr7) {
        bindings[1] = x86_addr(MINIC_X86_REG_R10,0);
        bindings[1].asm_constraint = "m";
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            read ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,dr7 ? 2U : 1U,NULL,0U,false,false) &&
        (!read || x86_scalar_output_store(sel,roles[0].value,
            MINIC_X86_REG_RAX,8U));
}

/* Linux's LTR/STR task-register pair selects true 16-bit privileged ISA
 * while preserving source's q-input and r-output GNU constraint widths.
 * STR's wider output widths are valid x86 register encodings; no C type
 * interpretation leaks into the physical MachineIR after this selection. */
static bool x86_source_task_control(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand binding;
    unsigned int form;
    size_t width = 0U;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_TASK_CONTROL,
            &source,&roles)) return false;
    form = source->target_machine_form_variant;
    if (form < 1U || form > 2U || !source->is_volatile ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->implicit_memory_effect ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        !x86_scalar_role(&roles[0],0U,form == 1U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        roles[0].physical_read_write || roles[0].has_tied_output)
        return false;
    if (form == 1U) {
        if (!x86_asm_gp_input_width(sel,roles[0].value,1U) &&
            !x86_asm_gp_input_width(sel,roles[0].value,2U) &&
            !x86_asm_gp_input_width(sel,roles[0].value,4U) &&
            !x86_asm_gp_input_width(sel,roles[0].value,8U)) return false;
        width = 2U;
        if (!x86_core_load(sel,roles[0].value,MINIC_X86_REG_RAX)) return false;
    } else if (x86_asm_gp_lvalue_width(sel,roles[0].value,2U)) {
        width = 2U;
    } else if (x86_asm_gp_lvalue_width(sel,roles[0].value,4U)) {
        width = 4U;
    } else if (x86_asm_gp_lvalue_width(sel,roles[0].value,8U)) {
        width = 8U;
    } else return false;
    binding = x86_reg(MINIC_X86_REG_RAX,(unsigned int)(8U*width));
    binding.asm_constraint = form == 1U ? "q" : "=r";
    binding.value.physical.access = form == 1U ?
        MINIC_MACHINE_REGISTER_READ : MINIC_MACHINE_REGISTER_WRITE;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        0U,&binding,1U,NULL,0U,false,false) &&
        (form == 1U || x86_scalar_output_store(sel,roles[0].value,
            MINIC_X86_REG_RAX,width));
}

/* vDSO getcpu's LSL-to-RDPID CPU alternative: one source occurrence owns
 * both the live LSL selector and its ELF patch table. The integer feature
 * input is the original constant zero; the replacement feature is selected
 * by the exact verified source form, not by a generic source passthrough. */
static bool x86_source_vdso_cpunode(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreInstruction *zero;
    MinicMachineOperand bindings[3];
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_VDSO_CPUNODE,
            &source,&roles) ||
        source->target_machine_form_variant != 1U ||
        !source->is_volatile || !source->has_memory_clobber ||
        source->explicit_memory_clobber || !source->implicit_memory_effect ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !x86_scalar_role(&roles[0],0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        !x86_scalar_role(&roles[1],1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        !x86_scalar_role(&roles[2],2U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[0].physical_read_write || roles[0].has_tied_output ||
        roles[1].physical_read_write || roles[1].has_tied_output ||
        roles[2].physical_read_write || roles[2].has_tied_output ||
        !x86_asm_gp_lvalue_width(sel,roles[0].value,4U) ||
        !x86_asm_gp_input_width(sel,roles[1].value,4U) ||
        (!x86_asm_gp_input_width(sel,roles[2].value,2U) &&
         !x86_asm_gp_input_width(sel,roles[2].value,4U) &&
         !x86_asm_gp_input_width(sel,roles[2].value,8U)) ||
        roles[1].value >= sel->core->value_count ||
        sel->core->values[roles[1].value].definition >=
            sel->core->instruction_count) return false;
    zero = &sel->core->instructions[
        sel->core->values[roles[1].value].definition];
    if (zero->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        zero->result != roles[1].value || zero->value.integer_value != 0 ||
        !x86_core_load(sel,roles[2].value,MINIC_X86_REG_R11)) return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX,32U);
    bindings[0].asm_constraint = "=a";
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[1] = x86_imm(0);
    bindings[1].asm_constraint = "i";
    bindings[2] = x86_reg(MINIC_X86_REG_R11,16U);
    bindings[2].asm_constraint = "r";
    bindings[2].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        0U,bindings,3U,NULL,0U,false,false) &&
        x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RAX,4U);
}

static bool x86_source_msr(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[4];
    MinicMachineRegisterEffect flags = {0};
    unsigned int registers[4] = {0};
    const char *constraints[4] = {NULL};
    size_t widths[4] = {0};
    size_t outputs, count, i;
    unsigned int form;
    bool read, safe;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_MSR ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->target_machine_form_variant != 0U ||
        source->source_asm_unique_id == 0U ||
        !source->is_volatile || source->is_goto ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->register_clobber_count != 0U ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length)) return false;
    form = minic_x86_64_linux_msr_form(source->template_text,
        source->template_length);
    read = form == 1U || form == 3U || form == 5U;
    safe = form == 3U || form == 4U;
    if (form == 0U || form > 6U ||
        source->has_memory_clobber == read ||
        source->explicit_memory_clobber == read) return false;
    outputs = safe ? (read ? 3U : 1U) : (read ? 2U : 0U);
    count = safe ? 4U : 3U;
    if (ins->value.structured_inline_asm.operand_count != count ||
        roles == NULL) return false;
    if (safe && read) {
        /* GNU's original safe-RDMSR allocator chooses R11D, encoded as
         * 0x0b0b in the exception table's type/register descriptor. */
        registers[0] = MINIC_X86_REG_R11;
        registers[1] = MINIC_X86_REG_RAX;
        registers[2] = MINIC_X86_REG_RDX;
        registers[3] = MINIC_X86_REG_RCX;
        constraints[0] = "=r";
        constraints[1] = "=a";
        constraints[2] = "=d";
        constraints[3] = "c";
        widths[0] = widths[3] = 4U;
        widths[1] = widths[2] = 8U;
    } else if (safe) {
        registers[0] = registers[2] = MINIC_X86_REG_RAX;
        registers[1] = MINIC_X86_REG_RCX;
        registers[3] = MINIC_X86_REG_RDX;
        constraints[0] = "=a";
        constraints[1] = "c";
        constraints[2] = "0";
        constraints[3] = "d";
        for (i = 0U; i < count; ++i) widths[i] = 4U;
    } else if (read) {
        const size_t width = x86_asm_gp_lvalue_width(sel,roles[0].value,8U) ?
            8U : 4U;
        registers[0] = MINIC_X86_REG_RAX;
        registers[1] = MINIC_X86_REG_RDX;
        registers[2] = MINIC_X86_REG_RCX;
        constraints[0] = "=a";
        constraints[1] = "=d";
        constraints[2] = "c";
        widths[0] = widths[1] = width;
        widths[2] = 4U;
    } else {
        registers[0] = MINIC_X86_REG_RCX;
        registers[1] = MINIC_X86_REG_RAX;
        registers[2] = MINIC_X86_REG_RDX;
        constraints[0] = "c";
        constraints[1] = "a";
        constraints[2] = "d";
        for (i = 0U; i < count; ++i) widths[i] = 4U;
    }
    for (i = 0U; i < count; ++i) {
        const bool output = i < outputs;
        const bool tied = safe && !read && i == 2U;
        if (roles[i].operand_index != i ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write ||
            roles[i].has_tied_output != tied ||
            (tied && roles[i].tied_output_index != 0U) ||
            !(output ? x86_asm_gp_lvalue_width(sel,roles[i].value,widths[i]) :
                x86_asm_gp_input_width(sel,roles[i].value,widths[i])))
            return false;
    }
    /* Load all *input* frame slots before the privileged instruction.  The
     * special RAX tie in safe WRMSR receives its seed via the ordinary input
     * operand; output pointers are materialized only after the instruction. */
    for (i = outputs; i < count; ++i)
        if (!x86_core_load(sel,roles[i].value,registers[i])) return false;
    for (i = 0U; i < count; ++i) {
        bindings[i] = x86_reg(registers[i],(unsigned int)(widths[i]*8U));
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = i < outputs ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    }
    if (safe) bindings[0].asm_name = "err";
    if (safe) {
        flags.reg = (MinicMachineRegister){0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
        flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            read ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,count,safe ? &flags : NULL,safe ? 1U : 0U,
            false,false)) return false;
    for (i = 0U; i < outputs; ++i) {
        if (!x86_core_load(sel,roles[i].value,MINIC_X86_REG_R10) ||
            !x86_2(sel,widths[i] == 8U ? MINIC_X86_MI_MOV64_RA :
                MINIC_X86_MI_MOV32_RA,
                x86_reg(registers[i],(unsigned int)(widths[i]*8U)),
                x86_addr(MINIC_X86_REG_R10,0))) return false;
    }
    return true;
}

static bool x86_percpu_scalar_shape(const X86CoreSelector *sel,
    MinicCoreValueId value, size_t width, bool address) {
    const MinicCoreType *type;
    MinicCoreTypeId pointee;
    if (value >= sel->core->value_count ||
        (width != 1U && width != 2U && width != 4U && width != 8U))
        return false;
    if (address) {
        if (!minic_lp64_core_pointer_pointee(sel->core->type_context,
                sel->core->values[value].core_type_id,&pointee))
            return false;
    } else pointee = sel->core->values[value].core_type_id;
    type = x86_type(sel,pointee);
    return type != NULL && type->has_layout && type->size == width &&
        (type->kind == MINIC_CORE_TYPE_INTEGER ||
         (width == 8U && type->kind == MINIC_CORE_TYPE_POINTER));
}

/* Original Linux percpu.h contains 36 GS-relative scalar forms: 4 widths
 * times MOV read/write and INC/DEC/ADD/SUB/AND/OR/XOR. All memory operands
 * remain true hardware `%gs:` references, not ordinary C pointer accesses;
 * Source proves the exact opcode and qualifier, MachineIR carries the base
 * register, width, operand accesses and ALU flag effects. */
static bool x86_source_percpu_gs(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[2];
    MinicMachineRegisterEffect flags = {0};
    unsigned int operation;
    size_t width, count, stable_width;
    bool stable;
    const bool is_source = ins->kind ==
        MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    if (!is_source || ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count || sel->core->inline_asms == NULL)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    stable_width = minic_x86_64_asm_percpu_stable_width(
        source->template_text,source->template_length);
    stable = stable_width != 0U;
    if (stable && getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL) {
        fprintf(stderr,
            "x86 gs stable source: form=%u owner=%u variant=%u unique=%u volatile=%u mem=%u explicit=%u clobbers=%zu operands=%zu template=%u\n",
            (unsigned int)source->target_machine_form_id,
            (unsigned int)source->operand_plan_owner,
            (unsigned int)source->target_machine_form_variant,
            (unsigned int)(source->source_asm_unique_id != 0U),
            (unsigned int)source->is_volatile,
            (unsigned int)source->has_memory_clobber,
            (unsigned int)source->explicit_memory_clobber,
            source->register_clobber_count,
            ins->value.structured_inline_asm.operand_count,
            (unsigned int)minic_x86_64_typed_machine_form_matches_template(
                source->target_machine_form_id,
                source->target_machine_form_variant,
                source->template_text,source->template_length));
        if (roles != NULL &&
            ins->value.structured_inline_asm.operand_count >= 2U)
            fprintf(stderr,
                "x86 gs stable roles: output_kind=%u output_shape=%u input_kind=%u input_shape=%u input_value=%u\n",
                (unsigned int)roles[0].kind,
                (unsigned int)x86_percpu_scalar_shape(
                    sel,roles[0].value,stable_width,true),
                (unsigned int)roles[1].kind,
                (unsigned int)x86_asm_gp_input_width(
                    sel,roles[1].value,8U),
                (unsigned int)roles[1].value);
    }
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_PERCPU_GS ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->target_machine_form_variant != 0U ||
        source->source_asm_unique_id == 0U || source->is_goto ||
        source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length) ||
        !minic_x86_64_linux_percpu_form(source->template_text,
            source->template_length,&operation,&width)) return false;
    count = operation == 2U || operation == 3U ? 1U : 2U;
    if (source->has_memory_clobber != (operation != 0U)) return false;
    if (ins->value.structured_inline_asm.operand_count != count ||
        roles == NULL || roles[0].operand_index != 0U ||
        roles[0].kind != (operation == 0U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) ||
        roles[0].has_fixed_register_binding || roles[0].early_clobber ||
        roles[0].physical_early_clobber ||
        (operation == 0U && roles[0].physical_read_write) ||
        roles[0].has_tied_output ||
        !x86_percpu_scalar_shape(sel,roles[0].value,width,true)) return false;
    if (count == 2U && (roles[1].operand_index != 1U ||
        roles[1].kind != (operation == 0U && !stable ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[1].has_fixed_register_binding || roles[1].early_clobber ||
        roles[1].physical_early_clobber ||
        roles[1].physical_read_write || roles[1].has_tied_output ||
        !(stable ? x86_asm_gp_input_width(sel,roles[1].value,8U) :
            x86_percpu_scalar_shape(sel,roles[1].value,width,
                operation == 0U)))) return false;
    if (!x86_core_load(sel,roles[operation == 0U ? 1U : 0U].value,
            MINIC_X86_REG_R10)) return false;
    if (operation > 0U && operation != 2U && operation != 3U &&
        !x86_core_load(sel,roles[1].value,MINIC_X86_REG_RAX))
        return false;
    bindings[0] = operation == 0U ?
        x86_reg(MINIC_X86_REG_RAX,(unsigned int)(width*8U)) :
        x86_addr(MINIC_X86_REG_R10,0);
    bindings[0].asm_name = operation == 0U ? "val" : "var";
    bindings[0].asm_constraint = operation == 0U ?
        (width == 1U ? "=q" : "=r") : "+m";
    if (operation == 0U)
        bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    if (count == 2U) {
        bindings[1] = operation == 0U ?
            stable ? x86_reg(MINIC_X86_REG_R10,64U) :
                x86_addr(MINIC_X86_REG_R10,0) :
            x86_reg(MINIC_X86_REG_RAX,(unsigned int)(width*8U));
        bindings[1].asm_name = operation == 0U ? "var" : "val";
        bindings[1].asm_constraint = operation == 0U ?
            (stable ? "p" : "m") :
            (width == 1U ? "qi" : width == 8U ? "re" : "ri");
        if (stable || operation != 0U)
            bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    if (operation >= 2U) {
        flags.reg = (MinicMachineRegister){0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
        flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,(operation == 0U ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                operation == 1U ? MINIC_MACHINE_EFFECT_MEMORY_WRITE :
                    MINIC_MACHINE_EFFECT_MEMORY_READ |
                    MINIC_MACHINE_EFFECT_MEMORY_WRITE),
            bindings,count,operation >= 2U ? &flags : NULL,
            operation >= 2U ? 1U : 0U,false,false)) return false;
    return operation != 0U ||
        (x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11) &&
         x86_2(sel,width == 1U ? MINIC_X86_MI_MOV8_RA :
             width == 2U ? MINIC_X86_MI_MOV16_RA :
             width == 4U ? MINIC_X86_MI_MOV32_RA :
                           MINIC_X86_MI_MOV64_RA,
             x86_reg(MINIC_X86_REG_RAX,(unsigned int)(width*8U)),
             x86_addr(MINIC_X86_REG_R11,0)));
}

/* Linux GS per-CPU ADD/SUB/AND/OR/XOR: exactly five source opcodes and two
 * LP64 widths share one sealed tied-register transport.  The memory input is
 * GS-relative hardware memory, not a C generic address dereference. */
static bool x86_source_gs_tied_alu(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect flags = {0};
    size_t width, source_width = 0U;
    unsigned int operation;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 3U) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    operation = minic_x86_64_gs_tied_alu_form(
        source->template_text,source->template_length,&source_width);
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_GS_TIED_ALU ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->target_machine_form_variant != operation ||
        operation < 1U || operation > 5U ||
        source->is_goto || source->has_memory_clobber ||
        source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length) ||
        roles[0].operand_index != 0U ||
        roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        roles[0].has_fixed_register_binding || roles[0].early_clobber ||
        roles[0].physical_early_clobber || roles[0].physical_read_write ||
        roles[0].has_tied_output ||
        roles[1].operand_index != 1U ||
        roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        roles[1].has_fixed_register_binding || roles[1].early_clobber ||
        roles[1].physical_early_clobber || roles[1].physical_read_write ||
        roles[1].has_tied_output ||
        roles[2].operand_index != 2U ||
        roles[2].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        roles[2].has_fixed_register_binding || roles[2].early_clobber ||
        roles[2].physical_early_clobber || roles[2].physical_read_write ||
        !roles[2].has_tied_output || roles[2].tied_output_index != 0U)
        return false;
    width = source_width ? source_width :
        x86_asm_gp_lvalue_width(sel,roles[0].value,4U) ? 4U : 8U;
    if ((width != 4U && width != 8U) ||
        !x86_asm_gp_lvalue_width(sel,roles[0].value,width) ||
        !x86_asm_gp_lvalue_width(sel,roles[1].value,width) ||
        !x86_asm_gp_input_width(sel,roles[2].value,width) ||
        !x86_core_load(sel, roles[2].value, MINIC_X86_REG_RAX) ||
        !x86_core_load(sel, roles[1].value, MINIC_X86_REG_R10)) return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX, (unsigned int)(width*8U));
    bindings[0].asm_constraint = "=r";
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[1] = x86_addr(MINIC_X86_REG_R10, 0);
    bindings[1].value.address.has_segment = true;
    bindings[1].value.address.segment = (MinicMachineRegister){
        MINIC_X86_SEG_GS, 16U, MINIC_MACHINE_REGISTER_SYSTEM};
    bindings[1].asm_constraint = "m";
    bindings[2] = x86_reg(MINIC_X86_REG_RAX, (unsigned int)(width*8U));
    bindings[2].asm_constraint = "0";
    bindings[2].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    flags.reg = (MinicMachineRegister){
        0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
    flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,MINIC_MACHINE_EFFECT_MEMORY_READ,
            bindings,3U,&flags,1U,false,false) ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11))
        return false;
    return x86_2(sel,width == 8U ? MINIC_X86_MI_MOV64_RA :
        MINIC_X86_MI_MOV32_RA,
        x86_reg(MINIC_X86_REG_RAX,(unsigned int)(width*8U)),
        x86_addr(MINIC_X86_REG_R11,0));
}

/* Original Linux ip_fast_csum has three GNU outputs, two tied inputs and a
 * hardware carry chain. Its output 1 is a pointer, outputs 0/2 are 32-bit
 * integers. Materialize the two initial values before emitting the single
 * sealed MachineIR source instruction, and store all three physical outputs
 * back through their Core-owned lvalue pointers afterwards. */

/* The x86 checksum carry family is a fixed register/CF dataflow: splitting
 * ADD from ADC would let intervening Core traffic destroy CF.  Keep the
 * complete proven GNU form in one source-ASM MachineIR instruction; tied
 * inputs occupy the exact same RAX carrier as their output. */
static bool x86_source_checksum_carry(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[6];
    MinicMachineRegisterEffect flags = {0};
    static const unsigned int tcp_regs[5] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_R10, MINIC_X86_REG_R11,
        MINIC_X86_REG_R8, MINIC_X86_REG_RAX
    };
    static const unsigned int partial_regs[5] = {
        MINIC_X86_REG_R10, MINIC_X86_REG_R11, MINIC_X86_REG_R8,
        MINIC_X86_REG_R9, MINIC_X86_REG_RCX
    };
    unsigned int form_id, partial = 0U, bits;
    size_t count, i, seed = SIZE_MAX;
    bool is_partial;
    bool trace;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    form_id = source->target_machine_form_id;
    trace = getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL;
    is_partial = form_id == MINIC_X86_MACHINE_ASM_CSUM_PARTIAL;
    if (form_id != MINIC_X86_MACHINE_ASM_ADD32_CARRY &&
        form_id != MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD &&
        form_id != MINIC_X86_MACHINE_ASM_CHECKSUM && !is_partial)
        return false;
    partial = is_partial ? minic_x86_64_linux_csum_partial_form(
        source->template_text,source->template_length) : 0U;
    count = is_partial ? (partial == 1U || partial == 6U ? 2U :
        partial == 2U ? 6U : 3U) :
        form_id == MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD ? 5U : 3U;
    roles = ins->value.structured_inline_asm.operands;
    if (trace) fprintf(stderr,
        "x86 checksum source: form=%u partial=%u variant=%u count=%zu"
        " actual=%zu owner=%u unique=%u volatile=%u goto=%u"
        " effective_mem=%u explicit_mem=%u reg_clobbers=%zu"
        " immediates=%zu template_ok=%u\n",
        form_id,partial,source->target_machine_form_variant,count,
        ins->value.structured_inline_asm.operand_count,
        (unsigned int)source->operand_plan_owner,
        (unsigned int)(source->source_asm_unique_id != 0U),
        (unsigned int)source->is_volatile,(unsigned int)source->is_goto,
        (unsigned int)source->has_memory_clobber,
        (unsigned int)source->explicit_memory_clobber,
        source->register_clobber_count,source->resolved_immediate_count,
        (unsigned int)minic_x86_64_typed_machine_form_matches_template(
            form_id,source->target_machine_form_variant,
            source->template_text,source->template_length));
    if (roles == NULL || (is_partial && (partial == 0U || partial > 6U)) ||
        ins->value.structured_inline_asm.operand_count != count ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        /* Core marks every target-qualified ASM effect volatile to retain
         * observable results; original GNU nonvolatile syntax was checked
         * exclusively by the C Source contract before Core import. */
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto || source->explicit_memory_clobber ||
        (source->has_memory_clobber &&
            !(is_partial && partial >= 2U && partial <= 5U)) ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            form_id,source->target_machine_form_variant,
            source->template_text,source->template_length))
        return false;
    if (trace) fprintf(stderr,"x86 checksum pass: stage=source-seal\n");
    bits = is_partial ? 64U : 32U;
    if (form_id == MINIC_X86_MACHINE_ASM_ADD32_CARRY) seed = 1U;
    else if (form_id == MINIC_X86_MACHINE_ASM_CHECKSUM) seed = 2U;
    else if (form_id == MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD) seed = 4U;
    for (i = 0U; i < count; ++i) {
        const bool memory = is_partial && partial == 2U && i != 0U;
        const bool tied = i == seed;
        const MinicCoreStructuredInlineAsmOperandKind expected =
            i == 0U ? (is_partial ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) :
                memory ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
                    MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (trace) fprintf(stderr,
            "x86 checksum role: index=%zu actual_index=%zu kind=%u"
            " expected=%u tie=%u expected_tie=%u tied_index=%zu"
            " early=%u physical_early=%u physical_rw=%u fixed=%u"
            " type_width_ok=%u\n",
            i,roles[i].operand_index,(unsigned int)roles[i].kind,
            (unsigned int)expected,
            (unsigned int)roles[i].has_tied_output,(unsigned int)tied,
            roles[i].tied_output_index,(unsigned int)roles[i].early_clobber,
            (unsigned int)roles[i].physical_early_clobber,
            (unsigned int)roles[i].physical_read_write,
            (unsigned int)roles[i].has_fixed_register_binding,
            (unsigned int)((i == 0U || memory) ?
                x86_asm_gp_lvalue_width(sel,roles[i].value,bits/8U) :
                x86_asm_gp_input_width(sel,roles[i].value,
                    is_partial ? 8U : 4U)));
        if (roles[i].operand_index != i ||
            roles[i].kind != expected || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].has_fixed_register_binding ||
            (i == 0U && roles[i].physical_read_write != is_partial) ||
            (i > 0U && roles[i].physical_read_write) ||
            roles[i].has_tied_output != tied ||
            (tied && roles[i].tied_output_index != 0U) ||
            !((i == 0U || memory) ?
                x86_asm_gp_lvalue_width(sel,roles[i].value,bits/8U) :
                x86_asm_gp_input_width(sel,roles[i].value,
                    is_partial ? 8U : 4U)))
            return false;
    }
    if (trace) fprintf(stderr,"x86 checksum pass: stage=roles\n");
    if (is_partial) {
        if (!x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11) ||
            !x86_2(sel,MINIC_X86_MI_MOV64_AR,
                x86_addr(MINIC_X86_REG_R11,0),
                x86_reg(MINIC_X86_REG_RAX,64U))) return false;
    } else if (!x86_core_load(sel,roles[seed].value,
            MINIC_X86_REG_RAX)) return false;
    if (trace) fprintf(stderr,"x86 checksum pass: stage=seed-load\n");
    for (i = 1U; i < count; ++i) {
        unsigned int reg;
        if (i == seed) continue;
        if (is_partial) reg = partial == 2U ?
            partial_regs[i-1U] : i == 1U ? MINIC_X86_REG_R10 :
                MINIC_X86_REG_R11;
        else if (form_id == MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD)
            reg = tcp_regs[i];
        else reg = MINIC_X86_REG_R10;
        if (!x86_core_load(sel,roles[i].value,reg)) return false;
    }
    if (trace) fprintf(stderr,"x86 checksum pass: stage=input-loads\n");
    for (i = 0U; i < count; ++i) {
        unsigned int reg = MINIC_X86_REG_RAX;
        const char *constraint;
        const bool memory = is_partial &&
            ((partial == 2U && i != 0U) ||
             (partial >= 3U && partial <= 5U && i == 2U));
        if (is_partial) {
            reg = i == 0U ? MINIC_X86_REG_RAX :
                partial == 2U ? partial_regs[i-1U] :
                i == 1U ? MINIC_X86_REG_R10 : MINIC_X86_REG_R11;
            constraint = i == 0U ? "+r" : memory ? "m" : "r";
        } else if (form_id == MINIC_X86_MACHINE_ASM_TCPUDP_NOFOLD) {
            reg = tcp_regs[i];
            constraint = i == 0U ? "=r" : i == 4U ? "0" : "g";
        } else if (form_id == MINIC_X86_MACHINE_ASM_ADD32_CARRY) {
            reg = i == 2U ? MINIC_X86_REG_R10 : MINIC_X86_REG_RAX;
            constraint = i == 0U ? "=r" : i == 1U ? "0" : "rm";
        } else {
            reg = i == 1U ? MINIC_X86_REG_R10 : MINIC_X86_REG_RAX;
            constraint = i == 0U ? "=r" : i == 1U ? "r" : "0";
        }
        bindings[i] = memory ? x86_addr(reg,0) : x86_reg(reg,bits);
        bindings[i].asm_constraint = (char *)constraint;
        if (!memory)
            bindings[i].value.physical.access = i == 0U ?
                is_partial ? MINIC_MACHINE_REGISTER_READ |
                    MINIC_MACHINE_REGISTER_WRITE :
                    MINIC_MACHINE_REGISTER_WRITE :
                MINIC_MACHINE_REGISTER_READ;
    }
    if (trace) fprintf(stderr,"x86 checksum pass: stage=bindings\n");
    flags.reg = (MinicMachineRegister){
        0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
    flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            is_partial && partial >= 2U && partial <= 5U ?
                MINIC_MACHINE_EFFECT_MEMORY_READ : 0U,
            bindings,count,&flags,1U,false,false) ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11))
        return false;
    if (trace) fprintf(stderr,"x86 checksum pass: stage=source-emit-and-output-address\n");
    return x86_2(sel,is_partial ? MINIC_X86_MI_MOV64_RA :
            MINIC_X86_MI_MOV32_RA,
        x86_reg(MINIC_X86_REG_RAX,bits),
        x86_addr(MINIC_X86_REG_R11,0));
}

static bool x86_source_ip_fast_csum(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[5];
    MinicMachineRegisterEffect flags = {0};
    static const unsigned int regs[5] = {
        MINIC_X86_REG_RAX, MINIC_X86_REG_R9, MINIC_X86_REG_R11,
        MINIC_X86_REG_R9, MINIC_X86_REG_R11
    };
    static const unsigned int bits[5] = {32U,64U,32U,64U,32U};
    static const char *const constraints[5] = {"=r","=r","=r","1","2"};
    size_t i;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 5U) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    /* CI-only guard diagnostics for the exact Linux ip_fast_csum form. */
    if (source->target_machine_form_id == MINIC_X86_MACHINE_ASM_IP_FAST_CSUM &&
        getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL) {
        fprintf(stderr,
            "x86 ipfast source: owner=%u variant=%u unique=%u"
            " volatile=%u mem=%u explicit=%u clobbers=%zu"
            " immediate=%zu roles=%zu template=%u\n",
            (unsigned int)source->operand_plan_owner,
            source->target_machine_form_variant,
            (unsigned int)(source->source_asm_unique_id != 0U),
            (unsigned int)source->is_volatile,
            (unsigned int)source->has_memory_clobber,
            (unsigned int)source->explicit_memory_clobber,
            source->register_clobber_count,
            source->resolved_immediate_count,
            ins->value.structured_inline_asm.operand_count,
            (unsigned int)minic_x86_64_typed_machine_form_matches_template(
                source->target_machine_form_id,
                source->target_machine_form_variant,
                source->template_text,source->template_length));
        if (roles != NULL) {
            size_t diag;
            for (diag = 0U; diag < 5U &&
                diag < ins->value.structured_inline_asm.operand_count; ++diag) {
                const MinicCoreStructuredInlineAsmOperand *r = &roles[diag];
                const size_t role_width = diag == 1U || diag == 3U ? 8U : 4U;
                const bool shape = diag < 3U ?
                    (diag == 1U ?
                        x86_percpu_scalar_shape(sel,r->value,8U,true) :
                        x86_asm_gp_lvalue_width(sel,r->value,4U)) :
                    x86_asm_gp_input_width(sel,r->value,role_width);
                fprintf(stderr,
                    "x86 ipfast role%zu: pos=%zu kind=%u fixed=%u"
                    " early=%u phys_early=%u rw=%u tie=%u to=%zu"
                    " value=%u shape=%u\n",
                    diag,r->operand_index,(unsigned int)r->kind,
                    (unsigned int)r->has_fixed_register_binding,
                    (unsigned int)r->early_clobber,
                    (unsigned int)r->physical_early_clobber,
                    (unsigned int)r->physical_read_write,
                    (unsigned int)r->has_tied_output,
                    r->tied_output_index,(unsigned int)r->value,
                    (unsigned int)shape);
            }
        }
    }
    if (roles == NULL ||
        source->target_machine_form_id != MINIC_X86_MACHINE_ASM_IP_FAST_CSUM ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        source->is_goto || !source->has_memory_clobber ||
        !source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length)) return false;
    for (i = 0U; i < 5U; ++i) {
        const bool output = i < 3U;
        const bool tied = i >= 3U;
        if (roles[i].operand_index != i ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber || roles[i].physical_read_write ||
            roles[i].has_tied_output != tied ||
            (tied && roles[i].tied_output_index != i-2U) ||
            !(output ? (i == 1U ?
                x86_percpu_scalar_shape(sel,roles[i].value,8U,true) :
                x86_asm_gp_lvalue_width(sel,roles[i].value,4U)) :
                x86_asm_gp_input_width(sel,roles[i].value,bits[i]/8U)))
            return false;
    }
    if (!x86_core_load(sel,roles[3].value,MINIC_X86_REG_R9) ||
        !x86_core_load(sel,roles[4].value,MINIC_X86_REG_R11))
        return false;
    for (i = 0U; i < 5U; ++i) {
        bindings[i] = x86_reg(regs[i],bits[i]);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = i < 3U ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    }
    flags.reg = (MinicMachineRegister){0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
    flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,5U,&flags,1U,false,false)) return false;
    for (i = 0U; i < 3U; ++i) {
        if (!x86_core_load(sel,roles[i].value,MINIC_X86_REG_R10) ||
            !x86_2(sel,i == 1U ? MINIC_X86_MI_MOV64_RA :
                MINIC_X86_MI_MOV32_RA,
                x86_reg(regs[i],bits[i]),
                x86_addr(MINIC_X86_REG_R10,0))) return false;
    }
    return true;
}

/* SMP lock-bearing XADD is an indivisible hardware RMW and a .smp_locks
 * relocation record. Preserve the original pointer-indirected +r and +m
 * semantics with actual AL/AX/EAX/RAX and R10-relative memory operands. */
static bool x86_source_smp_xadd(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicX86GsAtomicPlan plan;
    MinicMachineOperand bindings[2];
    MinicMachineRegisterEffect flags = {0};
    MinicX86MachineOpcode load,store;
    const char *reg_constraint;
    unsigned int bits;
    size_t width;
    size_t i;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 2U) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    plan = minic_x86_64_gs_atomic_plan(source->template_text,
        source->template_length);
    width = plan.width;
    if (roles == NULL ||
        source->target_machine_form_id != MINIC_X86_MACHINE_ASM_GS_ATOMIC ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        plan.kind != MINIC_X86_GS_ATOMIC_XADD_SMP ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber || !source->explicit_memory_clobber ||
        source->register_clobber_count != 1U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length)) return false;
    for (i = 0U; i < 2U; ++i) {
        if (roles[i].operand_index != i ||
            roles[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber || roles[i].has_tied_output ||
            !x86_asm_gp_lvalue_width(sel,roles[i].value,width)) return false;
    }
    bits = (unsigned int)(width*8U);
    reg_constraint = width == 1U ? "+q" : "+r";
    load = width == 1U ? MINIC_X86_MI_MOV8_AR :
        width == 2U ? MINIC_X86_MI_MOV16_AR :
        width == 4U ? MINIC_X86_MI_MOV32_AR : MINIC_X86_MI_MOV64_AR;
    store = width == 1U ? MINIC_X86_MI_MOV8_RA :
        width == 2U ? MINIC_X86_MI_MOV16_RA :
        width == 4U ? MINIC_X86_MI_MOV32_RA : MINIC_X86_MI_MOV64_RA;
    if (!x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11) ||
        !x86_2(sel,load,x86_addr(MINIC_X86_REG_R11,0),
            x86_reg(MINIC_X86_REG_RAX,bits)) ||
        !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R10)) return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX,bits);
    bindings[0].asm_constraint = (char *)reg_constraint;
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_READ |
        MINIC_MACHINE_REGISTER_WRITE;
    bindings[1] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[1].asm_constraint = "+m";
    flags.reg = (MinicMachineRegister){0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
    flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,2U,&flags,1U,false,false) &&
        x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11) &&
        x86_2(sel,store,x86_reg(MINIC_X86_REG_RAX,bits),
            x86_addr(MINIC_X86_REG_R11,0));
}

/* Linux _THIS_IP_ returns the instruction's current RIP, not an address
 * computed from a source-C constant or guessed symbol expression. */

/* Non-SMP local and GS per-CPU atomics retain the original XADD value
 * exchange and CMPXCHG accumulator semantics.  The GS address is a physical
 * segment-qualified MachineIR operand; it is never a process-memory load. */
static bool x86_source_gs_atomic_extended(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicX86GsAtomicPlan plan;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect flags = {0};
    MinicX86MachineOpcode load, store;
    const bool structured = ins->kind ==
        MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    bool cmp, retry, gs;
    size_t count, width, i;
    unsigned int bits;
    if (!structured || ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_GS_ATOMIC)
        return false;
    roles = ins->value.structured_inline_asm.operands;
    plan = minic_x86_64_gs_atomic_plan(source->template_text,
        source->template_length);
    if (plan.kind != MINIC_X86_GS_ATOMIC_XADD_LOCAL &&
        plan.kind != MINIC_X86_GS_ATOMIC_XADD_PERCPU &&
        plan.kind != MINIC_X86_GS_ATOMIC_CMPXCHG &&
        plan.kind != MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY)
        return false;
    width = plan.width;
    bits = (unsigned int)(width*8U);
    cmp = plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG ||
        plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY;
    retry = plan.kind == MINIC_X86_GS_ATOMIC_CMPXCHG_RETRY;
    gs = plan.kind != MINIC_X86_GS_ATOMIC_XADD_LOCAL;
    count = cmp ? 3U : 2U;
    if ((width != 1U && width != 2U && width != 4U && width != 8U) ||
        (plan.kind == MINIC_X86_GS_ATOMIC_XADD_LOCAL && width < 4U) ||
        roles == NULL ||
        ins->value.structured_inline_asm.operand_count != count ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || source->is_goto ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->register_clobber_count != 0U ||
        (plan.kind == MINIC_X86_GS_ATOMIC_XADD_LOCAL &&
            (!source->is_volatile || !source->has_memory_clobber ||
             !source->explicit_memory_clobber)) ||
        (cmp && (!source->has_memory_clobber ||
                 !source->explicit_memory_clobber)) ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length))
        return false;
    for (i = 0U; i < count; ++i) {
        const bool memory = i == 1U;
        const MinicCoreStructuredInlineAsmOperandKind expected =
            memory ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            i == 0U ? retry ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (roles[i].operand_index != i || roles[i].kind != expected ||
            roles[i].early_clobber ||
            roles[i].physical_early_clobber != (i == 0U && retry) ||
            roles[i].has_fixed_register_binding || roles[i].has_tied_output ||
            (i == 0U && roles[i].physical_read_write != !retry) ||
            (i == 2U && roles[i].physical_read_write) ||
            !(memory ? (gs && width == 8U ?
                x86_percpu_scalar_shape(sel,roles[i].value,width,true) :
                x86_asm_gp_lvalue_width(sel,roles[i].value,width)) :
                i == 0U ? x86_asm_gp_lvalue_width(sel,roles[i].value,width) :
                    x86_asm_gp_input_width(sel,roles[i].value,width)))
            return false;
    }
    load = width == 1U ? MINIC_X86_MI_MOV8_AR :
        width == 2U ? MINIC_X86_MI_MOV16_AR :
        width == 4U ? MINIC_X86_MI_MOV32_AR : MINIC_X86_MI_MOV64_AR;
    store = width == 1U ? MINIC_X86_MI_MOV8_RA :
        width == 2U ? MINIC_X86_MI_MOV16_RA :
        width == 4U ? MINIC_X86_MI_MOV32_RA : MINIC_X86_MI_MOV64_RA;
    if ((!retry &&
            (!x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11) ||
             !x86_2(sel,load,x86_addr(MINIC_X86_REG_R11,0),
                 x86_reg(MINIC_X86_REG_RAX,bits)))) ||
        !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R10) ||
        (cmp && !x86_core_load(sel,roles[2].value,MINIC_X86_REG_R11)))
        return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RAX,bits);
    bindings[0].asm_constraint = retry ? "=&a" : cmp ? "+a" :
        plan.kind == MINIC_X86_GS_ATOMIC_XADD_PERCPU && width == 1U ?
            "+q" : "+r";
    bindings[0].value.physical.access = retry ?
        MINIC_MACHINE_REGISTER_WRITE :
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
    bindings[1] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[1].asm_constraint = "+m";
    if (gs) {
        bindings[1].value.address.has_segment = true;
        bindings[1].value.address.segment = (MinicMachineRegister){
            MINIC_X86_SEG_GS,16U,MINIC_MACHINE_REGISTER_SYSTEM};
    }
    if (cmp) {
        bindings[2] = x86_reg(MINIC_X86_REG_R11,bits);
        bindings[2].asm_constraint = width == 1U ? "q" : "r";
        bindings[2].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    flags.reg = (MinicMachineRegister){
        0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
    flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,count,&flags,1U,false,false) &&
        x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11) &&
        x86_2(sel,store,x86_reg(MINIC_X86_REG_RAX,bits),
            x86_addr(MINIC_X86_REG_R11,0));
}

/* Migrate the existing x86 Linux FXSAVE/VERW/TLB-cache Source contracts to
 * physical MachineIR.  Source qualification already froze each exact GNU
 * template, constraint and semantic variant. Re-check its immutable identity,
 * use typed Core pointer transport, and bind an actual memory/register operand.
 * Never emit an unqualified assembly string or borrow the retired FILE* path. */
/* Exact Linux descriptor-table operations were Source-admitted prior to
 * MachineIR migration. Restore all four in one target-owned physical path,
 * retaining the ten-byte packed record ABI and read/write distinction. */
static bool x86_source_descriptor_table(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *role;
    MinicCoreTypeId pointee;
    const MinicCoreType *object;
    MinicMachineOperand binding;
    unsigned int form;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_LIDT)
        return false;
    form = minic_x86_64_linux_descriptor_table_form(
        source->template_text,source->template_length);
    role = &ins->value.structured_inline_asm.operands[0];
    if (form < 1U || form > 4U ||
        source->target_machine_form_variant != form ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        source->source_asm_unique_id == 0U ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !source->implicit_memory_effect ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,form,
            source->template_text,source->template_length) ||
        role->operand_index != 0U ||
        role->kind != (form <= 2U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT) ||
        role->value >= sel->core->value_count ||
        role->has_fixed_register_binding || role->has_tied_output ||
        role->early_clobber || role->physical_early_clobber ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[role->value].core_type_id,&pointee) ||
        (object = x86_type(sel,pointee)) == NULL ||
        object->kind != MINIC_CORE_TYPE_RECORD || !object->has_layout ||
        object->size != 10U || object->alignment != 1U) return false;
    binding = x86_addr(MINIC_X86_REG_R10,0);
    binding.asm_constraint = form <= 2U ? "m" : "=m";
    return x86_core_load(sel,role->value,MINIC_X86_REG_R10) &&
        minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,form <= 2U ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            &binding,1U,NULL,0U,false,false);
}

static bool x86_source_legacy_memory(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *role;
    const MinicCoreType *object;
    MinicCoreTypeId pointee_id;
    MinicMachineOperand binding;
    MinicMachineRegisterEffect flags = {0};
    unsigned int form, effects;
    bool fx, verw, tlb;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 1U)
        return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    role = &ins->value.structured_inline_asm.operands[0];
    fx = source->target_machine_form_id == MINIC_X86_MACHINE_ASM_FXSAVE;
    verw = source->target_machine_form_id == MINIC_X86_MACHINE_ASM_VERW_CLEAR;
    tlb = source->target_machine_form_id == MINIC_X86_MACHINE_ASM_TLB_CACHE;
    if (!fx && !verw && !tlb) return false;
    form = fx ? minic_x86_64_linux_fxsave_form(
            source->template_text,source->template_length) :
        verw ? (minic_x86_64_linux_verw_clear_form(
            source->template_text,source->template_length) ? 1U : 0U) :
        minic_x86_64_linux_tlb_cache_form(
            source->template_text,source->template_length);
    if (form == 0U || source->target_machine_form_variant != form ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || source->is_goto ||
        !source->is_volatile ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,form,
            source->template_text,source->template_length) ||
        role->operand_index != 0U || role->has_fixed_register_binding ||
        role->early_clobber || role->physical_early_clobber ||
        role->has_tied_output ||
        role->value >= sel->core->value_count)
        return false;
    /* INVLPG consumes the pointer value in a register; FXSAVE, VERW and
     * CLFLUSH address the pointee object directly. */
    if (tlb && form == 1U) {
        const MinicCoreType *argument = x86_type(sel,
            sel->core->values[role->value].core_type_id);
        if (role->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            argument == NULL || !argument->has_layout ||
            (argument->kind != MINIC_CORE_TYPE_POINTER &&
             (argument->kind != MINIC_CORE_TYPE_INTEGER ||
              argument->size != 8U))) return false;
        binding = x86_reg(MINIC_X86_REG_R10,64U);
        binding.asm_constraint = "r";
        binding.value.physical.access = MINIC_MACHINE_REGISTER_READ;
        effects = MINIC_MACHINE_EFFECT_MEMORY_READ |
            MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    } else {
        if (!minic_lp64_core_pointer_pointee(sel->core->type_context,
                sel->core->values[role->value].core_type_id,&pointee_id) ||
            (object = x86_type(sel,pointee_id)) == NULL ||
            !object->has_layout ||
            (fx ? (object->size < 512U || object->alignment < 16U ||
                   role->kind != (form == 1U ?
                       MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                       MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT)) :
             verw ? (object->kind != MINIC_CORE_TYPE_INTEGER ||
                     object->size != 2U ||
                     role->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) :
                    role->kind !=
                        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE))
            return false;
        binding = x86_addr(MINIC_X86_REG_R10,0);
        binding.asm_constraint = fx ? (form == 1U ? "+m" : "=m") :
            verw ? "m" : "+m";
        if (fx && form == 2U) binding.asm_name = "fx";
        if (verw) binding.asm_name = "ds";
        effects = fx && form == 2U ? MINIC_MACHINE_EFFECT_MEMORY_WRITE :
            verw ? MINIC_MACHINE_EFFECT_MEMORY_READ :
            MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    }
    if (verw) {
        const MinicCoreInlineAsmRegisterClobber *clobber;
        if (source->register_clobber_count != 1U ||
            source->register_clobbers == NULL) return false;
        clobber = &source->register_clobbers[0];
        if (clobber->name == NULL || clobber->name_length != 2U ||
            memcmp(clobber->name,"cc",2U) != 0) return false;
        flags.reg = (MinicMachineRegister){
            0U,64U,MINIC_MACHINE_REGISTER_FLAGS};
        flags.access = MINIC_MACHINE_REGISTER_CLOBBER;
    } else if (source->register_clobber_count != 0U) return false;
    return x86_core_load(sel,role->value,MINIC_X86_REG_R10) &&
        minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,effects,&binding,1U,verw ? &flags : NULL,
            verw ? 1U : 0U,false,false);
}

/* Restore the exact Linux LAR selector and identity-load GNU ABI. The
 * Source-qualified operands are selected as physical register/memory roles;
 * Core lvalue materialization is still a separate verified machine STORE. */
/* The old Linux delay calibration MUL uses two tied physical registers:
 * 64-bit unsigned-long output %rdx (=d) / input 0 and 32-bit scratch %eax
 * (=&a) / input 1.  The ISA is MUL EDX, implicit EAX/EDX pair. Do not
 * substitute a 64-bit MULQ or model these as independent arbitrary regs. */
static bool x86_source_delay_mul(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *src;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand binding[4];
    static char constraints[4][4] = {"=d", "=&a", "1", "0"};
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >= sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 4U)
        return false;
    src = &sel->core->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    if (src->target_machine_form_id != MINIC_X86_MACHINE_ASM_DELAY_MUL)
        return false;
    roles = ins->value.structured_inline_asm.operands;
    if (src->target_machine_form_variant != 1U ||
        src->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !src->is_volatile || src->is_goto || src->has_memory_clobber ||
        src->register_clobber_count != 0U || src->source_asm_unique_id == 0U ||
        src->resolved_immediate_count != 0U ||
        !minic_x86_64_typed_machine_form_matches_template(
            src->target_machine_form_id,1U,src->template_text,src->template_length))
        return false;
    for (i = 0U; i < 4U; ++i) {
        const bool output = i < 2U;
        if (roles[i].operand_index != i ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding ||
            (output ? (i == 0U && !x86_asm_u64_lvalue(sel,roles[i].value)) :
                !x86_asm_u64_input(sel,roles[i].value))) return false;
        binding[i] = x86_reg(i == 0U || i == 3U ?
            MINIC_X86_REG_RDX : MINIC_X86_REG_RAX,
            i == 1U || i == 2U ? 32U : 64U);
        binding[i].asm_constraint = constraints[i];
        binding[i].value.physical.access = output ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    }
    if (!x86_core_load(sel, roles[2].value, MINIC_X86_REG_RAX) ||
        !x86_core_load(sel, roles[3].value, MINIC_X86_REG_RDX) ||
        !minic_machine_builder_emit_core_source_asm(&sel->builder,
            src,0U,binding,4U,NULL,0U,false,false)) return false;
    return x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RDX,8U) &&
           x86_scalar_output_store(sel,roles[1].value,MINIC_X86_REG_RAX,4U);
}

/* Original Linux calibration loop and TPAUSE fixed-register signatures.
 * The Source policy freezes every assembler token, while MachineIR owns
 * physical RAX / RCX / RDX identity and the register side effects. */
static bool x86_source_delay_wait(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *src;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand binding[3];
    static char constraints[3][3] = {"c", "d", "a"};
    const uint32_t regs[3] = {MINIC_X86_REG_RCX, MINIC_X86_REG_RDX,
        MINIC_X86_REG_RAX};
    unsigned int form;
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >= sel->core->inline_asm_count)
        return false;
    src = &sel->core->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    if (src->target_machine_form_id != MINIC_X86_MACHINE_ASM_DELAY_WAIT)
        return false;
    roles = ins->value.structured_inline_asm.operands;
    form = minic_x86_64_linux_delay_wait_form(src->template_text,src->template_length);
    if ((form != 1U && form != 2U) ||
        src->target_machine_form_variant != form ||
        src->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !src->is_volatile || src->is_goto || src->has_memory_clobber ||
        src->register_clobber_count != 0U || src->source_asm_unique_id == 0U ||
        src->resolved_immediate_count != 0U ||
        ins->value.structured_inline_asm.operand_count != (form == 1U ? 1U : 3U) ||
        !minic_x86_64_typed_machine_form_matches_template(
            src->target_machine_form_id,form,src->template_text,src->template_length))
        return false;
    if (form == 1U) {
        if (roles[0].operand_index != 0U ||
            roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE ||
            !x86_asm_u64_lvalue(sel,roles[0].value) ||
            !x86_core_load(sel,roles[0].value,MINIC_X86_REG_R11)) return false;
        /* Core carries the lvalue address. Read its old scalar value before
         * the +a operation; the selected ASM updates RAX in place. */
        if (!x86_2(sel,MINIC_X86_MI_MOV64_AR,
                x86_addr(MINIC_X86_REG_R11,0),
                x86_reg(MINIC_X86_REG_RAX,64U))) return false;
        binding[0] = x86_reg(MINIC_X86_REG_RAX,64U);
        binding[0].asm_constraint = "+a";
        binding[0].value.physical.access = (MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE);
        return minic_machine_builder_emit_core_source_asm(&sel->builder,
                src,0U,binding,1U,NULL,0U,false,false) &&
            x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RAX,8U);
    }
    for (i = 0U; i < 3U; ++i) {
        if (roles[i].operand_index != i ||
            roles[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            roles[i].value >= sel->core->value_count ||
            !x86_core_load(sel,roles[i].value,regs[i])) return false;
        binding[i] = x86_reg(regs[i],32U);
        binding[i].asm_constraint = constraints[i];
        binding[i].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        src,0U,binding,3U,NULL,0U,false,false);
}

/* A Linux preemption static-call +r(current_stack_pointer) keeps RSP live
 * across the trampoline; it is not a C assignment to the stack pointer.
 * The Source-side qualifier proves the two exact symbols, and MachineIR
 * independently seals the physical RSP binding and hardware call effects. */
static bool x86_source_preempt_static_call(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *src;
    const MinicCoreStructuredInlineAsmOperand *role;
    const MinicCoreFixedRegisterBinding *fixed;
    MinicMachineOperand physical;
    unsigned int variant;
    const MinicCoreType *type;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        ins->value.structured_inline_asm.inline_asm_id >= sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL) return false;
    src = &sel->core->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    if (src->target_machine_form_id != MINIC_X86_MACHINE_ASM_PREEMPT_STATIC_CALL)
        return false;
    variant = src->target_machine_form_variant;
    role = ins->value.structured_inline_asm.operands;
    if ((variant != 1U && variant != 2U) ||
        src->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !src->is_volatile || src->is_goto || !src->has_memory_clobber ||
        src->explicit_memory_clobber || !src->implicit_memory_effect ||
        src->register_clobber_count != 0U || src->resolved_immediate_count != 0U ||
        src->source_asm_unique_id == 0U ||
        src->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            src->target_machine_form_id, variant,
            src->template_text, src->template_length) ||
        role->operand_index != 0U ||
        role->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE ||
        !role->physical_read_write || role->early_clobber ||
        role->physical_early_clobber || role->has_tied_output ||
        !role->has_fixed_register_binding ||
        role->fixed_register_binding_id >= sel->core->fixed_register_binding_count ||
        role->value >= sel->core->value_count) return false;
    fixed = &sel->core->fixed_register_bindings[role->fixed_register_binding_id];
    type = x86_type(sel, sel->core->values[role->value].core_type_id);
    if (fixed->register_name == NULL || fixed->register_name_length != 3U ||
        memcmp(fixed->register_name, "rsp", 3U) != 0 ||
        type == NULL || type->kind != MINIC_CORE_TYPE_INTEGER ||
        !type->has_layout || type->size != 8U) return false;
    physical = x86_reg(MINIC_X86_REG_RSP, 64U);
    physical.asm_constraint = "+r";
    physical.value.physical.access =
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        src, MINIC_MACHINE_EFFECT_CALL |
             MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        &physical, 1U, NULL, 0U, false, false);
}

/* Linux PVOP_VCALL0..4 is an externally callable patch site, not ordinary
 * scalar inline ASM. A genuine *memory-indirect* CALL must be paired with
 * the RETPOLINE-safe and parainstruction metadata. The 4 argument registers
 * are input-only and the corresponding 4 GNU outputs are separate writable
 * C lvalues; +r(current_stack_pointer) denotes the unmodified physical RSP.
 * Every relocation/constant/register is frozen in typed MachineIR. */
/* Original Linux exception/IRQ stack calls: retain the old backend's
 * physical calling convention, including R11/RSP stack exchange, the
 * callee-saved R12/R13 values across irq_enter_rcu, and the objtool PC32
 * reachability record. The source template and its C operands have already
 * been qualified; this selector only binds physical MachineIR resources. */
static bool x86_source_exception_stack_call(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int clobber_regs[8] = {
        MINIC_X86_REG_RAX,MINIC_X86_REG_RCX,MINIC_X86_REG_RDX,
        MINIC_X86_REG_RSI,MINIC_X86_REG_RDI,MINIC_X86_REG_R8,
        MINIC_X86_REG_R9,MINIC_X86_REG_R10
    };
    static const char *const clobber_names[9] = {
        "cc","rax","rcx","rdx","rsi","rdi","r8","r9","r10"
    };
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreInstruction *callee;
    const MinicCoreFixedRegisterBinding *fixed;
    MinicMachineRegisterEffect implicit[9] = {0};
    MinicMachineOperand bindings[7] = {0};
    const char *symbol;
    size_t i, count;
    unsigned int form;
    bool irq;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    irq = source->target_machine_form_id == MINIC_X86_MACHINE_ASM_IRQ_STACK_CALL;
    if (!irq && source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_EXCEPTION_STACK_CALL) return false;
    form = source->target_machine_form_variant;
    count = irq ? (form == 1U ? 5U : form == 2U ? 6U :
        form == 3U ? 4U : 0U) : 7U;
    roles = ins->value.structured_inline_asm.operands;
    if (count == 0U || roles == NULL ||
        ins->value.structured_inline_asm.operand_count != count ||
        (!irq && form != 0U) || !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        source->register_clobber_count != 9U ||
        source->register_clobbers == NULL ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,form,
            source->template_text,source->template_length)) return false;
    for (i = 0U; i < 9U; ++i) {
        if (source->register_clobbers[i].name == NULL ||
            strcmp(source->register_clobbers[i].name,clobber_names[i]) != 0)
            return false;
        implicit[i] = i == 0U ? x86_scalar_flags_clobber() :
            (MinicMachineRegisterEffect){
                .reg={clobber_regs[i-1U],64U,MINIC_MACHINE_REGISTER_GENERAL},
                .access=MINIC_MACHINE_REGISTER_CLOBBER};
    }
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *op = &roles[i];
        const MinicCoreStructuredInlineAsmOperandKind expected = i == 0U ?
            (irq ? MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) :
            i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (op->operand_index != i || op->value >= sel->core->value_count ||
            op->kind != expected || op->early_clobber ||
            op->physical_early_clobber || op->has_tied_output ||
            (i < 2U && !op->physical_read_write) ||
            (i >= 2U && op->physical_read_write)) return false;
        if (i == 0U && !irq && op->has_fixed_register_binding) {
            if (op->fixed_register_binding_id >=
                    sel->core->fixed_register_binding_count) return false;
            fixed = &sel->core->fixed_register_bindings[
                op->fixed_register_binding_id];
            if (fixed->register_name == NULL ||
                strcmp(fixed->register_name,"r11") != 0) return false;
        }
        if ((irq && i == 0U) || i == 1U || i == 3U) {
            if (!op->has_fixed_register_binding ||
                op->fixed_register_binding_id >=
                    sel->core->fixed_register_binding_count) return false;
            fixed = &sel->core->fixed_register_bindings[
                op->fixed_register_binding_id];
            if (fixed->register_name == NULL ||
                strcmp(fixed->register_name,i == 1U ? "rsp" : "r11") != 0)
                return false;
        } else if (i != 0U && op->has_fixed_register_binding) return false;
    }
    if (sel->core->values[roles[2].value].definition >=
            sel->core->instruction_count) return false;
    callee = &sel->core->instructions[
        sel->core->values[roles[2].value].definition];
    if (callee->kind != MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS ||
        callee->value.function_symbol_id >=
            sel->core->function_symbol_count ||
        sel->core->function_symbols == NULL ||
        (symbol = sel->core->function_symbols[
            callee->value.function_symbol_id].name) == NULL ||
        !minic_core_inline_asm_symbol_valid(symbol)) return false;
    /* On the original stack, save the RCU arguments in callee-saved GPRs.
     * Loading them after the RSP switch would address the wrong frame. */
    if (irq && form != 3U &&
        (!x86_1(sel,MINIC_X86_MI_PUSH64_R,x86_reg(MINIC_X86_REG_R12,64U)) ||
         !x86_1(sel,MINIC_X86_MI_PUSH64_R,x86_reg(MINIC_X86_REG_R13,64U)) ||
         !x86_core_load(sel,roles[4].value,MINIC_X86_REG_R12) ||
         (form == 2U && !x86_core_load(sel,roles[5].value,MINIC_X86_REG_R13))))
        return false;
    if (!irq &&
        (!x86_core_load(sel,roles[6].value,MINIC_X86_REG_RDX) ||
         !x86_core_load(sel,roles[5].value,MINIC_X86_REG_RSI) ||
         !x86_core_load(sel,roles[4].value,MINIC_X86_REG_RDI))) return false;
    if (!x86_core_load(sel,roles[3].value,MINIC_X86_REG_R11)) return false;
    bindings[0] = x86_reg(MINIC_X86_REG_R11,64U);
    bindings[1] = x86_reg(MINIC_X86_REG_RSP,64U);
    for (i = 0U; i < 2U; ++i) {
        bindings[i].asm_constraint = "+r";
        bindings[i].value.physical.access =
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
    }
    bindings[2] = x86_symbol(symbol,MINIC_X86_MACHINE_RELOC_PLT32);
    bindings[2].asm_constraint = "i";
    for (i = 3U; i < count; ++i) {
        const unsigned int reg = i == 3U ? MINIC_X86_REG_R11 :
            irq ? (i == 4U ? MINIC_X86_REG_R12 : MINIC_X86_REG_R13) :
            (i == 4U ? MINIC_X86_REG_RDI : i == 5U ?
                MINIC_X86_REG_RSI : MINIC_X86_REG_RDX);
        bindings[i] = x86_reg(reg,64U);
        bindings[i].asm_constraint = "r";
        bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_CALL | MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,count,implicit,9U,false,false)) return false;
    return !irq || form == 3U ||
        (x86_1(sel,MINIC_X86_MI_POP64_R,x86_reg(MINIC_X86_REG_R13,64U)) &&
         x86_1(sel,MINIC_X86_MI_POP64_R,x86_reg(MINIC_X86_REG_R12,64U)));
}

/* Two original Linux 6.6.143 instruction-sequence contracts:
 * sync_core's IRET-to-self and alternatives' INT3 call emulation selftest.
 * Only the complete source template is admitted; the target owns the
 * physical RSP, RDI, R10/R11 placement and the objtool no-ENDBR record. */
static bool x86_source_iret_int3(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreFixedRegisterBinding *fixed;
    MinicMachineOperand operands[2] = {0};
    MinicMachineRegisterEffect implicit[2] = {0};
    bool iret;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    iret = source->target_machine_form_id == MINIC_X86_MACHINE_ASM_IRET_TO_SELF;
    if (!iret && source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_INT3_SELFTEST) return false;
    roles = ins->value.structured_inline_asm.operands;
    if (!roles || ins->value.structured_inline_asm.operand_count != 2U ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->target_machine_form_variant != 0U ||
        source->register_clobber_count != (iret ? 1U : 0U) ||
        (iret && (!source->register_clobbers ||
            !source->register_clobbers[0].name ||
            strcmp(source->register_clobbers[0].name,"cc") != 0)) ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,0U,
            source->template_text,source->template_length) ||
        roles[0].operand_index != 0U || roles[1].operand_index != 1U ||
        roles[0].value >= sel->core->value_count ||
        roles[1].value >= sel->core->value_count ||
        roles[0].kind != (iret ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE) ||
        roles[1].kind != (iret ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[0].physical_early_clobber != iret ||
        roles[0].physical_read_write == iret ||
        roles[1].physical_read_write != iret ||
        roles[1].physical_early_clobber ||
        roles[0].has_tied_output || roles[1].has_tied_output ||
        roles[0].early_clobber || roles[1].early_clobber) return false;
    if ((iret && (roles[0].has_fixed_register_binding ||
            !roles[1].has_fixed_register_binding)) ||
        (!iret && (!roles[0].has_fixed_register_binding ||
            roles[1].has_fixed_register_binding))) return false;
    if ((iret ? roles[1].fixed_register_binding_id :
            roles[0].fixed_register_binding_id) >=
            sel->core->fixed_register_binding_count) return false;
    fixed = &sel->core->fixed_register_bindings[iret ?
        roles[1].fixed_register_binding_id : roles[0].fixed_register_binding_id];
    if (fixed->register_name == NULL ||
        strcmp(fixed->register_name,"rsp") != 0) return false;
    if (!x86_core_load(sel,iret ? roles[0].value : roles[1].value,
            iret ? MINIC_X86_REG_R10 : MINIC_X86_REG_RDI)) return false;
    if (iret) {
        /* R10 is an output lvalue address. The instruction writes the
         * temporary 32-bit selector in R11, then stores to that address. */
        operands[0] = x86_reg(MINIC_X86_REG_R10,64U);
        operands[0].asm_constraint = "=&r";
        operands[0].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        operands[1] = x86_reg(MINIC_X86_REG_RSP,64U);
        operands[1].asm_constraint = "+r";
        operands[1].value.physical.access =
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
        implicit[0] = x86_scalar_flags_clobber();
        implicit[1] = (MinicMachineRegisterEffect){
            .reg={MINIC_X86_REG_R11,64U,MINIC_MACHINE_REGISTER_GENERAL},
            .access=MINIC_MACHINE_REGISTER_CLOBBER};
    } else {
        operands[0] = x86_reg(MINIC_X86_REG_RSP,64U);
        operands[0].asm_constraint = "+r";
        operands[0].value.physical.access =
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
        operands[1] = x86_reg(MINIC_X86_REG_RDI,64U);
        operands[1].asm_constraint = "D";
        operands[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        operands,2U,iret ? implicit : NULL,iret ? 2U : 0U,false,false);
}

/* Linux alternatives' relocation selftest is a complete patch-site, not
 * arbitrary GNU extended asm. Resolve its `m` operand from the Core global
 * address, rather than silently substituting the old hardcoded symbol. */
static bool x86_source_alt_reloc_selftest(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *role;
    const MinicCoreInstruction *definition;
    const char *name;
    MinicMachineOperand binding;
    MinicMachineRegisterEffect clobber;
    if (!sel || !ins ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 1U) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    role = &ins->value.structured_inline_asm.operands[0];
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_ALT_RELOC_SELFTEST ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber || source->explicit_memory_clobber ||
        !source->implicit_memory_effect ||
        source->register_clobber_count != 1U ||
        !source->register_clobbers ||
        !source->register_clobbers[0].name ||
        strcmp(source->register_clobbers[0].name,"rdi") != 0 ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,0U,
            source->template_text,source->template_length) ||
        role->operand_index != 0U ||
        role->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        role->value >= sel->core->value_count ||
        role->has_fixed_register_binding || role->has_tied_output ||
        role->early_clobber || role->physical_early_clobber ||
        role->physical_read_write ||
        sel->core->values[role->value].definition >=
            sel->core->instruction_count) return false;
    definition = &sel->core->instructions[
        sel->core->values[role->value].definition];
    if (definition->kind != MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS ||
        definition->result != role->value ||
        definition->value.global_id >= sel->core->global_count ||
        sel->core->globals == NULL) return false;
    name = sel->core->globals[definition->value.global_id].name;
    if (!minic_core_inline_asm_symbol_valid(name)) return false;
    binding = x86_symbol(name,MINIC_X86_MACHINE_RELOC_PC32);
    binding.asm_constraint = "m";
    binding.asm_name = memchr(source->template_text,'[',
        source->template_length) ? "mem" : NULL;
    clobber.reg = (MinicMachineRegister){
        MINIC_X86_REG_RDI,64U,MINIC_MACHINE_REGISTER_GENERAL};
    clobber.access = MINIC_MACHINE_REGISTER_CLOBBER;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
        MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        &binding,1U,&clobber,1U,false,false);
}

static bool x86_source_pv_indirect_call(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int registers[4] = {
        MINIC_X86_REG_RDI, MINIC_X86_REG_RSI,
        MINIC_X86_REG_RDX, MINIC_X86_REG_RCX
    };
    static const char *const outputs[4] = {"=D", "=S", "=d", "=c"};
    static const char *const inputs[4] = {"D", "S", "d", "c"};
    static const unsigned int clobber_registers[5] = {
        MINIC_X86_REG_RAX,MINIC_X86_REG_R8,MINIC_X86_REG_R9,
        MINIC_X86_REG_R10,MINIC_X86_REG_R11
    };
    static const char *const clobber_names[6] = {
        "cc", "rax", "r8", "r9", "r10", "r11"
    };
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreFixedRegisterBinding *fixed;
    const MinicCoreInstruction *number_definition;
    MinicMachineRegisterEffect implicit[6] = {0};
    MinicMachineOperand bindings[11];
    size_t count, i;
    int64_t type_number;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (roles == NULL ||
        source->target_machine_form_id != MINIC_X86_MACHINE_ASM_PV_INDIRECT_CALL ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || source->is_goto ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length)) return false;
    count = ins->value.structured_inline_asm.operand_count;
    if (source->target_machine_form_variant != 0U || count < 7U ||
        count > 11U || !source->is_volatile || !source->has_memory_clobber ||
        !source->explicit_memory_clobber || !source->implicit_memory_effect ||
        source->register_clobber_count != 6U ||
        source->register_clobbers == NULL ||
        source->resolved_immediate_count != 0U) return false;
    for (i = 0U; i < 6U; ++i) {
        if (source->register_clobbers[i].name == NULL ||
            strcmp(source->register_clobbers[i].name,clobber_names[i]) != 0)
            return false;
        implicit[i].reg = i == 0U ?
            (MinicMachineRegister){0U,64U,MINIC_MACHINE_REGISTER_FLAGS} :
            (MinicMachineRegister){clobber_registers[i-1U],64U,
                MINIC_MACHINE_REGISTER_GENERAL};
        implicit[i].access = MINIC_MACHINE_REGISTER_CLOBBER;
    }
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *role = &roles[i];
        const MinicCoreStructuredInlineAsmOperandKind kind = i < 4U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            i == 4U ? MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE :
            i == 6U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (role->operand_index != i || role->kind != kind ||
            role->value >= sel->core->value_count ||
            role->early_clobber || role->physical_early_clobber ||
            role->has_tied_output ||
            role->physical_read_write != (i == 4U) ||
            role->has_fixed_register_binding != (i == 4U)) return false;
        if (i < 4U) {
            if (!x86_asm_gp_lvalue_width(sel,role->value,8U)) return false;
            bindings[i] = x86_reg(registers[i],64U);
            bindings[i].asm_constraint = (char *)outputs[i];
            bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        } else if (i == 4U) {
            const MinicCoreType *stack_type = x86_type(sel,
                sel->core->values[role->value].core_type_id);
            if (role->fixed_register_binding_id >=
                    sel->core->fixed_register_binding_count ||
                (fixed = &sel->core->fixed_register_bindings[
                    role->fixed_register_binding_id])->register_name == NULL ||
                strcmp(fixed->register_name,"rsp") != 0 ||
                stack_type == NULL || !stack_type->has_layout ||
                stack_type->kind != MINIC_CORE_TYPE_INTEGER ||
                stack_type->size != 8U) return false;
            bindings[i] = x86_reg(MINIC_X86_REG_RSP,64U);
            bindings[i].asm_constraint = "+r";
            bindings[i].value.physical.access =
                MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
        } else if (i == 5U) {
            if (!minic_core_function_value_is_kind(sel->core,
                    role->value,MINIC_CORE_TYPE_INTEGER) ||
                sel->core->values[role->value].definition >=
                    sel->core->instruction_count) return false;
            number_definition = &sel->core->instructions[
                sel->core->values[role->value].definition];
            if (number_definition->kind !=
                    MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
                number_definition->result != role->value ||
                number_definition->value.integer_value < 0 ||
                number_definition->value.integer_value > 255) return false;
            type_number = number_definition->value.integer_value;
            bindings[i] = x86_imm(type_number);
            bindings[i].asm_constraint = "i";
            bindings[i].asm_name = "paravirt_typenum";
        } else if (i == 6U) {
            if (!x86_asm_gp_lvalue_width(sel,role->value,8U)) return false;
            bindings[i] = x86_addr(MINIC_X86_REG_R10,0);
            bindings[i].asm_constraint = "m";
            bindings[i].asm_name = "paravirt_opptr";
        } else {
            if (!x86_asm_gp_input_width(sel,role->value,8U)) return false;
            bindings[i] = x86_reg(registers[i-7U],64U);
            bindings[i].asm_constraint = (char *)inputs[i-7U];
            bindings[i].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        }
    }
    /* Load inputs before the memory-indirect CALL. GNU %6 must remain the
     * true lvalue of the function pointer; R10 is available until CALL. */
    for (i = 7U; i < count; ++i)
        if (!x86_core_load(sel,roles[i].value,registers[i-7U])) return false;
    if (!x86_core_load(sel,roles[6].value,MINIC_X86_REG_R10) ||
        !minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_CALL | MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,count,implicit,6U,false,false)) return false;
    /* x86_scalar_output_store uses RDI for the destination address, so
     * preserve output D in caller-saved RAX before writing any C local. */
    if (!x86_rr(sel,MINIC_X86_MI_MOV64_RR,64U,
            MINIC_X86_REG_RDI,MINIC_X86_REG_RAX) ||
        !x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RAX,8U))
        return false;
    for (i = 1U; i < 4U; ++i)
        if (!x86_scalar_output_store(sel,roles[i].value,registers[i],8U))
            return false;
    return true;
}

/* Linux clear_page's two alternatives are three distinct linker-resolved
 * CALL targets, not runtime function-pointer operands. Core has admitted
 * the complete GNU source spelling and kept the original FUNCTION_ADDRESS
 * definitions; this selector independently captures those exact symbols,
 * binds +r(current_stack_pointer) to RSP and places the page input in RDI.
 * The two replacement CALLs retain their own ELF relocations and patch
 * feature records. No generic textual CALL path is admitted. */
static bool x86_source_three_call_alternative(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *src;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreFixedRegisterBinding *fixed;
    const MinicCoreType *rsp_type;
    MinicMachineOperand bindings[6];
    MinicMachineRegisterEffect implicit[3] = {0};
    size_t i;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >= sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 6U)
        return false;
    src = &sel->core->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    if (src->target_machine_form_id != MINIC_X86_MACHINE_ASM_THREE_CALL_ALTERNATIVE ||
        src->target_machine_form_variant != 0U ||
        src->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !src->is_volatile || src->is_goto ||
        !src->has_memory_clobber || !src->explicit_memory_clobber ||
        !src->implicit_memory_effect ||
        src->register_clobber_count != 3U ||
        src->resolved_immediate_count != 0U ||
        src->source_asm_unique_id == 0U ||
        src->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            src->target_machine_form_id,0U,
            src->template_text,src->template_length) ||
        roles[0].operand_index != 0U ||
        roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        roles[0].has_fixed_register_binding || roles[0].early_clobber ||
        roles[0].physical_early_clobber || roles[0].physical_read_write ||
        roles[0].has_tied_output ||
        !x86_asm_gp_lvalue_width(sel,roles[0].value,8U) ||
        roles[1].operand_index != 1U ||
        roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE ||
        !roles[1].has_fixed_register_binding ||
        !roles[1].physical_read_write || roles[1].early_clobber ||
        roles[1].physical_early_clobber || roles[1].has_tied_output ||
        roles[1].fixed_register_binding_id >= sel->core->fixed_register_binding_count ||
        roles[1].value >= sel->core->value_count) return false;
    fixed = &sel->core->fixed_register_bindings[roles[1].fixed_register_binding_id];
    rsp_type = x86_type(sel,sel->core->values[roles[1].value].core_type_id);
    if (fixed->register_name == NULL || fixed->register_name_length != 3U ||
        memcmp(fixed->register_name,"rsp",3U) != 0 ||
        rsp_type == NULL || rsp_type->kind != MINIC_CORE_TYPE_INTEGER ||
        !rsp_type->has_layout || rsp_type->size != 8U) return false;
    bindings[0] = x86_reg(MINIC_X86_REG_RDI,64U);
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[0].asm_constraint = "=D";
    bindings[1] = x86_reg(MINIC_X86_REG_RSP,64U);
    bindings[1].value.physical.access =
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE;
    bindings[1].asm_constraint = "+r";
    for (i = 2U; i < 5U; ++i) {
        const MinicCoreInstruction *producer;
        MinicCoreValueId value = roles[i].value;
        MinicCoreInstructionId definition;
        MinicCoreFunctionSymbolId symbol_id;
        const char *name;
        if (roles[i].operand_index != i ||
            roles[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber || roles[i].physical_read_write ||
            roles[i].has_tied_output ||
            !x86_asm_gp_input_width(sel,value,8U) ||
            (definition = sel->core->values[value].definition) >=
                sel->core->instruction_count) return false;
        producer = &sel->core->instructions[definition];
        if (producer->kind != MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS ||
            (symbol_id = producer->value.function_symbol_id) >=
                sel->core->function_symbol_count ||
            sel->core->function_symbols == NULL ||
            (name = sel->core->function_symbols[symbol_id].name) == NULL ||
            !minic_core_inline_asm_symbol_valid(name)) return false;
        bindings[i] = x86_symbol(name,MINIC_X86_MACHINE_RELOC_PLT32);
        bindings[i].asm_constraint = "i";
    }
    if (roles[5].operand_index != 5U ||
        roles[5].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        roles[5].has_fixed_register_binding || roles[5].early_clobber ||
        roles[5].physical_early_clobber || roles[5].physical_read_write ||
        !roles[5].has_tied_output || roles[5].tied_output_index != 0U ||
        !x86_asm_gp_input_width(sel,roles[5].value,8U) ||
        !x86_core_load(sel,roles[5].value,MINIC_X86_REG_RDI)) return false;
    bindings[5] = x86_reg(MINIC_X86_REG_RDI,64U);
    bindings[5].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    bindings[5].asm_constraint = "0";
    implicit[0] = x86_scalar_flags_clobber();
    implicit[1].reg = (MinicMachineRegister){MINIC_X86_REG_RAX,64U,
        MINIC_MACHINE_REGISTER_GENERAL};
    implicit[1].access = MINIC_MACHINE_REGISTER_CLOBBER;
    implicit[2].reg = (MinicMachineRegister){MINIC_X86_REG_RCX,64U,
        MINIC_MACHINE_REGISTER_GENERAL};
    implicit[2].access = MINIC_MACHINE_REGISTER_CLOBBER;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,src,
            MINIC_MACHINE_EFFECT_CALL |
                MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,6U,implicit,3U,false,false) &&
        x86_rr(sel,MINIC_X86_MI_MOV64_RR,64U,
            MINIC_X86_REG_RDI,MINIC_X86_REG_RAX) &&
        x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RAX,8U);
}

/* Source-admitted kernel HLT is a zero-operand privileged instruction,
 * not a no-op barrier. Preserve it as an exact typed Machine source opcode;
 * tests assemble it but never execute HLT in user mode. */
static bool x86_source_lowlevel_halt(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *src;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != 0U ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >= sel->core->inline_asm_count)
        return false;
    src = &sel->core->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    return src->target_machine_form_id == MINIC_X86_MACHINE_ASM_LOWLEVEL_SCALAR &&
        src->target_machine_form_variant == 1U &&
        src->operand_plan_owner == MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED &&
        src->is_volatile && !src->is_goto && !src->has_memory_clobber &&
        src->register_clobber_count == 0U && src->source_asm_unique_id != 0U &&
        minic_x86_64_typed_machine_form_matches_template(
            src->target_machine_form_id,1U,src->template_text,src->template_length) &&
        minic_machine_builder_emit_core_source_asm(&sel->builder,
            src,0U,NULL,0U,NULL,0U,false,false);
}

/* Linux write-combining MOVNTI: physical output memory address and
 * integer source register. Emit only the Source-qualified 32/64-bit forms. */
static bool x86_source_movnti(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *src;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreType *mem;
    MinicCoreTypeId pointee;
    MinicMachineOperand bindings[2];
    unsigned int form, bits;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >= sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 2U) return false;
    src = &sel->core->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    if (src->target_machine_form_id != MINIC_X86_MACHINE_ASM_LOWLEVEL_SCALAR)
        return false;
    form = minic_x86_64_linux_lowlevel_scalar_form(src->template_text,src->template_length);
    if ((form != 7U && form != 8U) ||
        form != src->target_machine_form_variant ||
        src->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        src->source_asm_unique_id == 0U || src->is_goto ||
        src->register_clobber_count != 0U ||
        !minic_x86_64_typed_machine_form_matches_template(
            src->target_machine_form_id,form,src->template_text,src->template_length))
        return false;
    bits = form == 7U ? 32U : 64U;
    roles = ins->value.structured_inline_asm.operands;
    if (roles[0].operand_index != 0U ||
        roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
        roles[1].operand_index != 1U ||
        roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[roles[0].value].core_type_id,&pointee) ||
        (mem = x86_type(sel,pointee)) == NULL || !mem->has_layout ||
        mem->kind != MINIC_CORE_TYPE_INTEGER || mem->size != bits / 8U ||
        roles[1].value >= sel->core->value_count ||
        (mem = x86_type(sel,sel->core->values[roles[1].value].core_type_id)) == NULL ||
        mem->kind != MINIC_CORE_TYPE_INTEGER || !mem->has_layout ||
        (mem->size != bits / 8U && !(bits == 32U && mem->size == 8U)) ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_R10) ||
        !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R11)) return false;
    bindings[0] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[0].asm_constraint = "=m";
    bindings[1] = x86_reg(MINIC_X86_REG_R11,bits);
    bindings[1].asm_constraint = "r";
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        src,MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        bindings,2U,NULL,0U,false,false);
}

/* Boot flag update: exact GNU +m/Ir BTSL with 32-bit bit index and
 * the real 32-bit memory RMW effect. No widening to BTSQ or fake flags. */
static bool x86_source_boot_test_bit(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_BOOT_FLAGS,
            &source,&roles) ||
        source->target_machine_form_variant != 1U || !source->is_volatile ||
        !source->has_memory_clobber || source->explicit_memory_clobber ||
        !source->implicit_memory_effect ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !x86_scalar_role(&roles[0],0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
        !x86_scalar_role(&roles[1],1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT) ||
        !x86_scalar_role(&roles[2],2U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        roles[0].physical_read_write || roles[0].has_tied_output ||
        roles[1].has_tied_output || roles[2].has_tied_output ||
        !x86_asm_bool_lvalue(sel,roles[0].value) ||
        !x86_asm_gp_lvalue_width(sel,roles[1].value,4U) ||
        !x86_asm_gp_input_width(sel,roles[2].value,4U) ||
        !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R10) ||
        !x86_core_load(sel,roles[2].value,MINIC_X86_REG_R11)) return false;
    bindings[0] = x86_reg(0U,64U);
    bindings[0].value.physical.reg.bank = MINIC_MACHINE_REGISTER_FLAGS;
    bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    bindings[0].asm_constraint = "=@ccc";
    bindings[1] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[1].asm_constraint = "m";
    bindings[2] = x86_reg(MINIC_X86_REG_R11,32U);
    bindings[2].asm_constraint = "Ir";
    bindings[2].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ,
            bindings,3U,&flags,1U,false,false) &&
        x86_1(sel,MINIC_X86_MI_SETC8_R,x86_reg(MINIC_X86_REG_R9,8U)) &&
        x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_R9,1U);
}

/* The Linux compressed-boot ID-flag probe must restore original RFLAGS
 * after its speculative toggle. R10/R11 are private output GPRs and RAX
 * is the independent input bitmask; no generic GNU asm text is emitted. */
static bool x86_source_boot_eflags(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const unsigned int regs[3] = {
        MINIC_X86_REG_R10,MINIC_X86_REG_R11,MINIC_X86_REG_RAX
    };
    static const char *const constraints[3] = {"=&r","=&r","ri"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3];
    MinicMachineRegisterEffect flags = x86_scalar_flags_clobber();
    size_t i;
    if (!x86_scalar_source_base(sel,ins,MINIC_X86_MACHINE_ASM_BOOT_FLAGS,
            &source,&roles) ||
        source->target_machine_form_variant != 4U || !source->is_volatile ||
        !source->has_memory_clobber || source->explicit_memory_clobber ||
        !source->implicit_memory_effect ||
        ins->value.structured_inline_asm.operand_count != 3U) return false;
    for (i = 0U; i < 3U; ++i) {
        const bool output = i < 2U;
        if (roles[i].operand_index != i ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding ||
            roles[i].early_clobber ||
            roles[i].physical_read_write || roles[i].has_tied_output ||
            (output ? !roles[i].physical_early_clobber :
                roles[i].physical_early_clobber) ||
            (output ? !x86_asm_gp_lvalue_width(sel,roles[i].value,8U) :
                !x86_asm_gp_input_width(sel,roles[i].value,8U))) return false;
        bindings[i] = x86_reg(regs[i],64U);
        bindings[i].asm_constraint = (char *)constraints[i];
        bindings[i].value.physical.access = output ?
            MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ;
    }
    return x86_core_load(sel,roles[2].value,MINIC_X86_REG_RAX) &&
        minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,3U,&flags,1U,false,false) &&
        x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_R10,8U) &&
        x86_scalar_output_store(sel,roles[1].value,MINIC_X86_REG_R11,8U);
}

static bool x86_source_boot_bit(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *src;
    const MinicCoreStructuredInlineAsmOperand *roles;
    const MinicCoreType *type;
    MinicCoreTypeId pointee;
    MinicMachineOperand bindings[2];
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >= sel->core->inline_asm_count ||
        ins->value.structured_inline_asm.operand_count != 2U) return false;
    src = &sel->core->inline_asms[ins->value.structured_inline_asm.inline_asm_id];
    if (src->target_machine_form_id != MINIC_X86_MACHINE_ASM_BOOT_FLAGS ||
        src->target_machine_form_variant != 2U) return false;
    roles = ins->value.structured_inline_asm.operands;
    if (src->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !src->is_volatile || src->is_goto || src->register_clobber_count != 0U ||
        src->source_asm_unique_id == 0U ||
        !minic_x86_64_typed_machine_form_matches_template(
            src->target_machine_form_id,2U,src->template_text,src->template_length) ||
        roles[0].operand_index != 0U ||
        roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE ||
        roles[1].operand_index != 1U ||
        roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[roles[0].value].core_type_id,&pointee) ||
        (type = x86_type(sel,pointee)) == NULL ||
        type->kind != MINIC_CORE_TYPE_INTEGER || !type->has_layout ||
        type->size != 4U || roles[1].value >= sel->core->value_count ||
        (type = x86_type(sel,sel->core->values[roles[1].value].core_type_id)) == NULL ||
        type->kind != MINIC_CORE_TYPE_INTEGER || !type->has_layout ||
        type->size != 4U ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_R10) ||
        !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R11)) return false;
    bindings[0] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[0].asm_constraint = "+m";
    bindings[1] = x86_reg(MINIC_X86_REG_R11,32U);
    bindings[1].asm_constraint = "Ir";
    bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        src,MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        bindings,2U,NULL,0U,false,false);
}

static bool x86_source_legacy_scalar(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[2];
    MinicMachineRegisterEffect implicit = {0};
    MinicCoreTypeId pointee;
    const MinicCoreType *type;
    bool lar;
    size_t count;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    lar = source->target_machine_form_id == MINIC_X86_MACHINE_ASM_LAR_SELECTOR;
    if (!lar && source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_IDENTITY_LOAD) return false;
    roles = ins->value.structured_inline_asm.operands;
    count = lar ? 2U : 1U;
    if (ins->value.structured_inline_asm.operand_count != count ||
        source->target_machine_form_variant != 1U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto || source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,1U,
            source->template_text,source->template_length)) return false;
    if (lar) {
        if (source->has_memory_clobber ||
            source->register_clobber_count != 0U ||
            roles[0].operand_index != 0U ||
            roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            roles[1].operand_index != 1U ||
            roles[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            roles[0].has_fixed_register_binding ||
            roles[1].has_fixed_register_binding ||
            roles[0].early_clobber || roles[1].early_clobber ||
            roles[0].physical_early_clobber ||
            roles[1].physical_early_clobber ||
            roles[0].has_tied_output || roles[1].has_tied_output ||
            !x86_asm_gp_lvalue_width(sel,roles[0].value,4U) ||
            roles[1].value >= sel->core->value_count ||
            (type = x86_type(sel,
                sel->core->values[roles[1].value].core_type_id)) == NULL ||
            type->kind != MINIC_CORE_TYPE_INTEGER || !type->has_layout ||
            type->size != 2U ||
            !x86_core_load(sel,roles[1].value,MINIC_X86_REG_R10))
            return false;
        bindings[0] = x86_reg(MINIC_X86_REG_RAX,32U);
        bindings[0].asm_constraint = "=r";
        bindings[0].asm_name = "ar";
        bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        bindings[1] = x86_reg(MINIC_X86_REG_R10,16U);
        bindings[1].asm_constraint = "rm";
        bindings[1].asm_name = "old_ss";
        bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        return minic_machine_builder_emit_core_source_asm(&sel->builder,
                source,0U,bindings,2U,NULL,0U,false,false) &&
            x86_scalar_output_store(sel,roles[0].value,MINIC_X86_REG_RAX,4U);
    }
    if (source->register_clobber_count != 1U ||
        source->register_clobbers == NULL ||
        source->register_clobbers[0].name == NULL ||
        source->register_clobbers[0].name_length != 2U ||
        memcmp(source->register_clobbers[0].name,"r9",2U) != 0 ||
        roles[0].operand_index != 0U ||
        roles[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
        roles[0].has_fixed_register_binding || roles[0].early_clobber ||
        roles[0].physical_early_clobber || roles[0].has_tied_output ||
        roles[0].value >= sel->core->value_count ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[roles[0].value].core_type_id,&pointee) ||
        (type = x86_type(sel,pointee)) == NULL || !type->has_layout ||
        type->kind != MINIC_CORE_TYPE_INTEGER || type->size != 8U ||
        !x86_core_load(sel,roles[0].value,MINIC_X86_REG_R10)) return false;
    bindings[0] = x86_addr(MINIC_X86_REG_R10,0);
    bindings[0].asm_constraint = "g";
    bindings[0].asm_name = "address";
    implicit.reg = (MinicMachineRegister){
        MINIC_X86_REG_R9,64U,MINIC_MACHINE_REGISTER_GENERAL};
    implicit.access = MINIC_MACHINE_REGISTER_CLOBBER;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source,MINIC_MACHINE_EFFECT_MEMORY_READ,
        bindings,1U,&implicit,1U,false,false);
}

static bool x86_source_ip_here(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *role;
    MinicMachineOperand output;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 1U) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    role = &ins->value.structured_inline_asm.operands[0];
    if (source->target_machine_form_id != MINIC_X86_MACHINE_ASM_IP_HERE ||
        source->target_machine_form_variant != 1U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->is_goto || source->has_memory_clobber ||
        source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length) ||
        role->operand_index != 0U ||
        role->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        role->has_fixed_register_binding || role->early_clobber ||
        role->physical_early_clobber || role->physical_read_write ||
        role->has_tied_output ||
        !x86_asm_gp_lvalue_width(sel,role->value,8U)) return false;
    output = x86_reg(MINIC_X86_REG_RAX,64U);
    output.asm_constraint = "=r";
    output.value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
            source,0U,&output,1U,NULL,0U,false,false) &&
        x86_core_load(sel,role->value,MINIC_X86_REG_R11) &&
        x86_2(sel,MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RAX,64U),
            x86_addr(MINIC_X86_REG_R11,0));
}

/* Linux BUG/WARN GNU %c operands are a target-qualified, C-ingress-owned
 * static symbol+three numeric fields.  Core captures each as a typed
 * immediate; selection must not reparse source C or guess values from the
 * ASM template.  Variant1 emits optional .discard.reachable and variant2
 * preserves named GNU inputs for the same ELF ABI. */
/* A Core GNU asm-goto is an instruction with two CFG exits: one named C
 * target and ordinary fallthrough. x86 must split the physical block at
 * that instruction; a source-template JMP alone cannot represent the C
 * continuation or prove the __jump_table's relocations. */
static bool x86_source_jump_label(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    const MinicCoreInlineAsm *source;
    const MinicCoreInlineAsmImmediate *key, *flags;
    MinicMachineOperand bindings[3];
    const X86AuxBlocks *aux;
    unsigned int form;
    bool named_label;
    if (ins->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.inline_asm_id >= sel->core->inline_asm_count ||
        instruction_id >= sel->core->instruction_count) return false;
    source = &sel->core->inline_asms[ins->value.inline_asm_id];
    form = minic_x86_64_linux_jump_label_form(
        source->template_text,source->template_length);
    if (form == 0U || !source->is_goto || !source->is_volatile ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_NONE ||
        source->target_machine_form_id != 0U ||
        source->target_machine_form_variant != 0U ||
        source->has_memory_clobber || source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        (source->symbol_reference_count > 1U ||
         (source->symbol_reference_count == 1U &&
          source->symbol_references == NULL)) ||
        source->deferred_immediate_symbol != NULL ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 2U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->goto_target >= sel->core->block_count ||
        !sel->reachable[source->goto_target] ||
        sel->blocks[source->goto_target] == SIZE_MAX) return false;
    aux = &sel->instruction_aux[instruction_id];
    if (aux->first == SIZE_MAX || aux->second != aux->first + 1U)
        return false;
    named_label = strstr(source->template_text, "%l[l_yes]") != NULL;
    if (named_label ?
        (source->goto_label_names[0] == NULL ||
         strcmp(source->goto_label_names[0],"l_yes") != 0) :
        (source->goto_label_names[0] != NULL &&
         strcmp(source->goto_label_names[0],"l_yes") != 0)) return false;
    key = &source->resolved_immediates[0];
    flags = &source->resolved_immediates[1];
    if (!minic_core_inline_asm_relocation_valid(key) ||
        key->asm_name != NULL ||
        flags->kind != MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER ||
        flags->asm_name != NULL || flags->symbol != NULL ||
        flags->byte_addend != 0 || flags->integer < 0 ||
        flags->integer > 3) return false;
    if (source->symbol_reference_count == 1U) {
        const MinicCoreInlineAsmSymbolReference *ref =
            &source->symbol_references[0];
        if (ref->kind != MINIC_CORE_INLINE_ASM_SYMBOL_OBJECT ||
            ref->name == NULL || ref->name_length != strlen(key->symbol) ||
            memcmp(ref->name,key->symbol,ref->name_length) != 0 ||
            ref->byte_addend != key->byte_addend) return false;
    }
    bindings[0] = x86_symbol(key->symbol,MINIC_X86_MACHINE_RELOC_PC64);
    bindings[0].value.symbol.addend = key->byte_addend;
    bindings[0].asm_constraint = "i";
    bindings[1] = x86_imm(flags->integer);
    bindings[1].asm_constraint = "i";
    bindings[2] = x86_block(sel->blocks[source->goto_target]);
    bindings[2].asm_name = named_label ? "l_yes" : NULL;
    /* An explicit JMP enters the immediately adjacent split island. Its
     * source ASM is the last instruction of that island, with two explicit
     * machine successors; the next C instruction goes in aux->second. */
    return x86_1(sel,MINIC_X86_MI_JMP_B,x86_block(aux->first)) &&
        minic_machine_builder_add_successor(&sel->builder,aux->first) &&
        minic_machine_builder_position_at(&sel->builder,aux->first) &&
        minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_BRANCH,bindings,3U,NULL,0U,true,true) &&
        minic_machine_builder_add_successor(&sel->builder,
            sel->blocks[source->goto_target]) &&
        minic_machine_builder_add_successor(&sel->builder,aux->second) &&
        minic_machine_builder_set_fallthrough(&sel->builder,aux->second) &&
        minic_machine_builder_position_at(&sel->builder,aux->second);
}

/* x86 _static_cpu_has is another GNU asm-goto CFG island, but its two named
 * C targets and three proven immediates describe a distinct pair of patch
 * records plus the .altinstr_aux test path. Never substitute a runtime
 * register for feature/bit number/boot capability relocation. */
static bool x86_source_cpu_has_goto(X86CoreSelector *sel,
    const MinicCoreInstruction *ins, size_t instruction_id) {
    static const char *const input_names[] = {
        "feature","bitnum","cap_byte"
    };
    static const char *const label_names[] = {"t_yes","t_no"};
    const MinicCoreInlineAsm *source;
    const X86AuxBlocks *aux;
    const MinicCoreInlineAsmImmediate *feature, *bitnum, *cap;
    MinicMachineOperand bindings[5];
    size_t i;
    if (ins->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        sel->core->inline_asms == NULL ||
        ins->value.inline_asm_id >= sel->core->inline_asm_count ||
        instruction_id >= sel->core->instruction_count)
        return false;
    source = &sel->core->inline_asms[ins->value.inline_asm_id];
    if (!minic_x86_64_linux_cpu_has_goto_form(
            source->template_text,source->template_length) ||
        !source->is_goto || !source->is_volatile ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_NONE ||
        source->target_machine_form_id != 0U ||
        source->target_machine_form_variant != 0U ||
        source->has_memory_clobber || source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        source->symbol_reference_count > 2U ||
        (source->symbol_reference_count != 0U &&
         source->symbol_references == NULL) ||
        source->deferred_immediate_symbol != NULL ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 3U ||
        source->goto_target >= sel->core->block_count ||
        source->goto_second_target >= sel->core->block_count ||
        source->goto_target == source->goto_second_target ||
        !sel->reachable[source->goto_target] ||
        !sel->reachable[source->goto_second_target] ||
        sel->blocks[source->goto_target] == SIZE_MAX ||
        sel->blocks[source->goto_second_target] == SIZE_MAX)
        return false;
    aux = &sel->instruction_aux[instruction_id];
    if (aux->first == SIZE_MAX || aux->second != aux->first + 1U)
        return false;
    for (i = 0U; i < 2U; ++i)
        if (source->goto_label_names[i] == NULL ||
            strcmp(source->goto_label_names[i],label_names[i]) != 0)
            return false;
    feature = &source->resolved_immediates[0];
    bitnum = &source->resolved_immediates[1];
    cap = &source->resolved_immediates[2];
    if (feature->kind != MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER ||
        feature->symbol != NULL || feature->byte_addend != 0 ||
        feature->integer < 0 || feature->integer >= 24 * 32 ||
        bitnum->kind != MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER ||
        bitnum->symbol != NULL || bitnum->byte_addend != 0 ||
        bitnum->integer != (INT64_C(1) << (feature->integer & 7)) ||
        !minic_core_inline_asm_relocation_valid(cap) ||
        cap->byte_addend < 0 || cap->byte_addend > INT32_MAX)
        return false;
    for (i = 0U; i < 3U; ++i)
        if (source->resolved_immediates[i].asm_name == NULL ||
            strcmp(source->resolved_immediates[i].asm_name,
                input_names[i]) != 0) return false;
    for (i = 0U; i < source->symbol_reference_count; ++i) {
        const MinicCoreInlineAsmSymbolReference *ref =
            &source->symbol_references[i];
        if (ref->kind != MINIC_CORE_INLINE_ASM_SYMBOL_OBJECT ||
            ref->name == NULL || ref->name_length != strlen(cap->symbol) ||
            memcmp(ref->name,cap->symbol,ref->name_length) != 0 ||
            (ref->byte_addend != 0 &&
             ref->byte_addend != cap->byte_addend)) return false;
    }
    bindings[0] = x86_imm(feature->integer);
    bindings[1] = x86_imm(bitnum->integer);
    bindings[2] = x86_symbol(cap->symbol,MINIC_X86_MACHINE_RELOC_PC32);
    bindings[2].value.symbol.addend = cap->byte_addend;
    for (i = 0U; i < 3U; ++i) {
        bindings[i].asm_constraint = "i";
        bindings[i].asm_name = (char *)input_names[i];
    }
    bindings[3] = x86_block(sel->blocks[source->goto_target]);
    bindings[3].asm_name = (char *)label_names[0];
    bindings[4] = x86_block(sel->blocks[source->goto_second_target]);
    bindings[4].asm_name = (char *)label_names[1];
    return x86_1(sel,MINIC_X86_MI_JMP_B,x86_block(aux->first)) &&
        minic_machine_builder_add_successor(&sel->builder,aux->first) &&
        minic_machine_builder_position_at(&sel->builder,aux->first) &&
        minic_machine_builder_emit_core_source_asm(&sel->builder,source,
            MINIC_MACHINE_EFFECT_BRANCH,bindings,5U,NULL,0U,true,true) &&
        minic_machine_builder_add_successor(&sel->builder,
            sel->blocks[source->goto_target]) &&
        minic_machine_builder_add_successor(&sel->builder,
            sel->blocks[source->goto_second_target]) &&
        minic_machine_builder_add_successor(&sel->builder,aux->second) &&
        minic_machine_builder_set_fallthrough(&sel->builder,aux->second) &&
        minic_machine_builder_position_at(&sel->builder,aux->second);
}

static bool x86_source_bug_table_c(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const char *const names[4] = {"path","ln","fl","sz"};
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[4];
    unsigned int variant;
    size_t i;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count || sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 4U) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    variant = source->target_machine_form_variant;
    if (source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_BUG_TABLE_C_IMMEDIATE ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        (variant != 1U && variant != 2U && variant != 3U) ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 4U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text,source->template_length)) return false;
    for (i = 0U; i < 4U; ++i) {
        const MinicCoreInlineAsmImmediate *immediate =
            &source->resolved_immediates[i];
        if (roles[i].operand_index != i ||
            roles[i].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write || roles[i].has_tied_output ||
            (variant != 2U ? immediate->asm_name != NULL :
                (immediate->asm_name == NULL ||
                 strcmp(immediate->asm_name,names[i]) != 0)))
            return false;
        if (i == 0U) {
            if (immediate->kind != MINIC_CORE_INLINE_ASM_IMMEDIATE_SYMBOL ||
                !minic_core_inline_asm_relocation_valid(immediate))
                return false;
            bindings[i] = x86_symbol(immediate->symbol,
                MINIC_X86_MACHINE_RELOC_PC32);
            bindings[i].value.symbol.addend = immediate->byte_addend;
        } else {
            if (immediate->kind != MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER ||
                immediate->symbol != NULL ||
                (i == 3U ? immediate->integer != 12 :
                    immediate->integer < 0 || immediate->integer > 65535))
                return false;
            bindings[i] = x86_imm(immediate->integer);
        }
        bindings[i].asm_constraint = "i";
        if (variant == 2U) bindings[i].asm_name = (char *)names[i];
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source,0U,bindings,4U,NULL,0U,false,false);
}

/* Scalar GNU lowercase-i qualifier: capture its immutable numeric witness
 * in a real MachineIR immediate, even though the native NOP consumes no
 * physical register. The separate verifier proves the complete instruction. */
static bool x86_source_nop_const_i(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *role;
    const MinicCoreInlineAsmImmediate *immediate;
    MinicMachineOperand binding;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    role = ins->value.structured_inline_asm.operands;
    if (role == NULL ||
        source->target_machine_form_id != MINIC_X86_MACHINE_ASM_NOP_CONST_I ||
        source->target_machine_form_variant != 1U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->source_asm_unique_id == 0U || !source->is_volatile ||
        source->is_goto || source->has_memory_clobber ||
        source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 1U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        role[0].operand_index != 0U ||
        role[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        role[0].has_fixed_register_binding || role[0].early_clobber ||
        role[0].physical_early_clobber || role[0].physical_read_write ||
        role[0].has_tied_output ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,source->target_machine_form_variant,
            source->template_text,source->template_length)) return false;
    immediate = &source->resolved_immediates[0];
    if (immediate->kind != MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER ||
        immediate->symbol != NULL || immediate->asm_name != NULL) return false;
    binding = x86_imm(immediate->integer);
    binding.asm_constraint = "i";
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source,0U,&binding,1U,NULL,0U,false,false);
}

static bool x86_source_accumulator_math(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[4];
    MinicMachineRegisterEffect rdx_clobber = {0};
    bool muldiv;
    size_t i;
    if (ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->value.structured_inline_asm.inline_asm_id >=
            sel->core->inline_asm_count ||
        sel->core->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 4U ||
        ins->result != MINIC_CORE_VALUE_INVALID) return false;
    source = &sel->core->inline_asms[
        ins->value.structured_inline_asm.inline_asm_id];
    roles = ins->value.structured_inline_asm.operands;
    muldiv = source->target_machine_form_id ==
        MINIC_X86_MACHINE_ASM_MULDIV_U64;
    if (!muldiv && source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_KASLR_MUL) return false;
    if (source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->explicit_memory_clobber ||
        source->source_asm_unique_id == 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->deferred_immediate_symbol != NULL ||
        source->template_text == NULL ||
        !minic_x86_64_typed_machine_form_matches_template(
            source->target_machine_form_id,
            source->target_machine_form_variant,
            source->template_text, source->template_length) ||
        source->register_clobber_count != (muldiv ? 1U : 0U) ||
        (muldiv && (source->register_clobbers == NULL ||
            source->register_clobbers[0].name == NULL ||
            source->register_clobbers[0].name_length != 3U ||
            memcmp(source->register_clobbers[0].name,
                "rdx", 3U) != 0))) return false;
    for (i = 0U; i < 4U; ++i) {
        bool output = muldiv ? i == 0U : i < 2U;
        if (roles[i].operand_index != i ||
            roles[i].kind != (output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            roles[i].has_fixed_register_binding || roles[i].early_clobber ||
            roles[i].physical_early_clobber ||
            roles[i].physical_read_write || roles[i].has_tied_output ||
            !(output ? x86_asm_u64_lvalue(sel, roles[i].value) :
                x86_asm_u64_input(sel, roles[i].value))) return false;
    }
    if (muldiv) {
        if (!x86_core_load(sel, roles[2].value, MINIC_X86_REG_R10) ||
            !x86_core_load(sel, roles[3].value, MINIC_X86_REG_R11) ||
            !x86_core_load(sel, roles[1].value, MINIC_X86_REG_RAX))
            return false;
        bindings[0] = x86_reg(MINIC_X86_REG_RAX, 64U);
        bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        bindings[0].asm_constraint = "=a";
        bindings[1] = x86_reg(MINIC_X86_REG_RAX, 64U);
        bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bindings[1].asm_constraint = "a";
        bindings[2] = x86_reg(MINIC_X86_REG_R10, 64U);
        bindings[2].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bindings[2].asm_constraint = "rm";
        bindings[3] = x86_reg(MINIC_X86_REG_R11, 64U);
        bindings[3].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bindings[3].asm_constraint = "rm";
        rdx_clobber.reg = (MinicMachineRegister){
            MINIC_X86_REG_RDX, 64U, MINIC_MACHINE_REGISTER_GENERAL};
        rdx_clobber.access = MINIC_MACHINE_REGISTER_CLOBBER;
    } else {
        if (!x86_core_load(sel, roles[3].value, MINIC_X86_REG_R10) ||
            !x86_core_load(sel, roles[2].value, MINIC_X86_REG_RAX))
            return false;
        bindings[0] = x86_reg(MINIC_X86_REG_RAX, 64U);
        bindings[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        bindings[0].asm_constraint = "=a";
        bindings[1] = x86_reg(MINIC_X86_REG_RDX, 64U);
        bindings[1].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
        bindings[1].asm_constraint = "=d";
        bindings[2] = x86_reg(MINIC_X86_REG_RAX, 64U);
        bindings[2].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bindings[2].asm_constraint = "a";
        bindings[3] = x86_reg(MINIC_X86_REG_R10, 64U);
        bindings[3].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bindings[3].asm_constraint = "rm";
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, 0U, bindings, 4U,
            muldiv ? &rdx_clobber : NULL, muldiv ? 1U : 0U,
            false, false)) return false;
    if (!x86_core_load(sel, roles[0].value, MINIC_X86_REG_R11) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RAX, 64U),
            x86_addr(MINIC_X86_REG_R11, 0))) return false;
    return muldiv ||
        (x86_core_load(sel, roles[1].value, MINIC_X86_REG_R11) &&
         x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RDX, 64U),
            x86_addr(MINIC_X86_REG_R11, 0)));
}

/* The kernel's uaccess exception helpers have an ABI of their own.  The
 * original C ingress has already qualified the GNU constraints, but Core
 * still carries their exact physical roles.  Materialize a sequence of
 * actual selected x86 instructions, including its direct CALL relocation;
 * never feed the textual template to the assembler as a machine opcode. */
static bool x86_uaccess_lvalue(X86CoreSelector *sel,
    MinicCoreValueId value, size_t expected_width, bool signed_int) {
    const MinicCoreType *pointer;
    const MinicCoreType *pointee;
    MinicCoreTypeId pointee_id;
    if (value >= sel->core->value_count ||
        (pointer = x86_type(sel,
            sel->core->values[value].core_type_id)) == NULL ||
        pointer->kind != MINIC_CORE_TYPE_POINTER ||
        !minic_lp64_core_pointer_pointee(sel->core->type_context,
            sel->core->values[value].core_type_id, &pointee_id) ||
        (pointee = x86_type(sel, pointee_id)) == NULL ||
        !pointee->has_layout || pointee->size != expected_width ||
        pointee->kind != MINIC_CORE_TYPE_INTEGER ||
        pointee->value.integer.is_bool ||
        (signed_int && !pointee->value.integer.is_signed)) return false;
    return true;
}

static bool x86_uaccess_call(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    static const char *const put_symbols[2][4] = {
        {"__put_user_1", "__put_user_2", "__put_user_4", "__put_user_8"},
        {"__put_user_nocheck_1", "__put_user_nocheck_2",
         "__put_user_nocheck_4", "__put_user_nocheck_8"}
    };
    static const char *const get_symbols[2][4] = {
        {"__get_user_1", "__get_user_2", "__get_user_4", "__get_user_8"},
        {"__get_user_nocheck_1", "__get_user_nocheck_2",
         "__get_user_nocheck_4", "__get_user_nocheck_8"}
    };
    static const MinicX86MachineOpcode stores[4] = {
        MINIC_X86_MI_MOV8_RA, MINIC_X86_MI_MOV16_RA,
        MINIC_X86_MI_MOV32_RA, MINIC_X86_MI_MOV64_RA
    };
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *bindings;
    const MinicCoreFixedRegisterBinding *fixed;
    const MinicCoreInstruction *constant;
    MinicCoreInlineAsmId id;
    unsigned int form;
    size_t i, bytes, width_index;
    bool put, nocheck, sign, is_bool;
    const char *symbol;
    if (sel == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->value.structured_inline_asm.operand_count != 5U ||
        (id = ins->value.structured_inline_asm.inline_asm_id) >=
            sel->core->inline_asm_count || !sel->core->inline_asms)
        return false;
    source = &sel->core->inline_asms[id];
    bindings = ins->value.structured_inline_asm.operands;
    put = source->target_machine_form_id ==
        MINIC_X86_MACHINE_ASM_PUT_USER_CALL;
    if (!put && source->target_machine_form_id !=
            MINIC_X86_MACHINE_ASM_GET_USER_CALL) return false;
    form = minic_x86_64_linux_uaccess_core_form_exact(
        source->template_text, source->template_length, put);
    if (source->operand_plan_owner !=
            MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->target_machine_form_variant != form ||
        (put ? (form != 0x200U && form != 0x800U) :
               (form != 3U && form != 4U)) ||
        source->source_asm_unique_id == 0U ||
        source->register_clobber_count != (put ? 1U : 0U) ||
        (put && (!source->register_clobbers ||
            source->register_clobbers[0].name_length != 3U ||
            memcmp(source->register_clobbers[0].name, "ebx", 3U) != 0)) ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID)
        return false;
    for (i = 0U; i < 5U; ++i) {
        const bool output = i == 0U || (!put && i == 1U);
        const bool stack_binding = i == (put ? 1U : 2U);
        const bool bound = put ? (i == 1U || i == 3U) :
            (i == 1U || i == 2U);
        if (bindings[i].operand_index != i ||
            bindings[i].value >= sel->core->value_count ||
            bindings[i].early_clobber ||
            bindings[i].kind != (stack_binding ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_FIXED_REGISTER_READWRITE :
                output ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            bindings[i].has_fixed_register_binding != bound ||
            (bound && bindings[i].fixed_register_binding_id >=
                sel->core->fixed_register_binding_count)) return false;
    }
    if (!x86_uaccess_lvalue(sel, bindings[0].value, 4U, true) ||
        !minic_core_function_value_is_kind(sel->core,
            bindings[put ? 2U : 3U].value, MINIC_CORE_TYPE_POINTER) ||
        !minic_core_function_value_is_kind(sel->core,
            bindings[4].value, MINIC_CORE_TYPE_INTEGER) ||
        sel->core->values[bindings[4].value].definition >=
            sel->core->instruction_count) return false;
    constant = &sel->core->instructions[
        sel->core->values[bindings[4].value].definition];
    if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        constant->result != bindings[4].value) return false;
    bytes = (size_t)constant->value.integer_value;
    switch (bytes) {
    case 1U: width_index = 0U; break;
    case 2U: width_index = 1U; break;
    case 4U: width_index = 2U; break;
    case 8U: width_index = 3U; break;
    default: return false;
    }
    if (put) {
        if (bindings[1].fixed_register_binding_id >=
                sel->core->fixed_register_binding_count ||
            bindings[3].fixed_register_binding_id >=
                sel->core->fixed_register_binding_count) return false;
        fixed = &sel->core->fixed_register_bindings[
            bindings[3].fixed_register_binding_id];
        if (!fixed->register_name ||
            (strcmp(fixed->register_name, "rax") != 0 &&
             strcmp(fixed->register_name, "%rax") != 0) ||
            !x86_scalar_shape(sel,
                sel->core->values[bindings[3].value].core_type_id,
                &i, &sign, &is_bool) || i != bytes || is_bool)
            return false;
        fixed = &sel->core->fixed_register_bindings[
            bindings[1].fixed_register_binding_id];
        if (!fixed->register_name ||
            strcmp(fixed->register_name, "rsp") != 0) return false;
    } else {
        fixed = &sel->core->fixed_register_bindings[
            bindings[1].fixed_register_binding_id];
        if (!fixed->register_name ||
            (strcmp(fixed->register_name, "rdx") != 0 &&
             strcmp(fixed->register_name, "%rdx") != 0) ||
            !x86_uaccess_lvalue(sel, bindings[1].value, bytes, false))
            return false;
        fixed = &sel->core->fixed_register_bindings[
            bindings[2].fixed_register_binding_id];
        if (!fixed->register_name ||
            strcmp(fixed->register_name, "rsp") != 0) return false;
    }
    nocheck = put ? form == 0x800U : form == 4U;
    symbol = put ? put_symbols[nocheck][width_index] :
        get_symbols[nocheck][width_index];
    /* Preserve RBX across even a nonstandard helper that declares its
     * clobber. The 16-byte spill preserves call-site stack alignment. */
    if (!x86_ir(sel, MINIC_X86_MI_SUB64_IR, 16, 64U,
            MINIC_X86_REG_RSP) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_RA,
            x86_reg(MINIC_X86_REG_RBX, 64U),
            x86_addr(MINIC_X86_REG_RSP, 0)) ||
        !x86_core_load(sel, bindings[put ? 2U : 3U].value,
            put ? MINIC_X86_REG_RCX : MINIC_X86_REG_RAX) ||
        (put && !x86_core_load(sel, bindings[3].value,
            MINIC_X86_REG_RAX)) ||
        !x86_1(sel, MINIC_X86_MI_CALL_UACCESS_LOCAL_SYMBOL,
            x86_symbol(symbol, MINIC_X86_MACHINE_RELOC_PC32)) ||
        !x86_2(sel, MINIC_X86_MI_MOV64_AR,
            x86_addr(MINIC_X86_REG_RSP, 0),
            x86_reg(MINIC_X86_REG_RBX, 64U)) ||
        !x86_ir(sel, MINIC_X86_MI_ADD64_IR, 16, 64U,
            MINIC_X86_REG_RSP)) return false;
    if (!put) {
        if (!x86_core_load(sel, bindings[1].value, MINIC_X86_REG_R11) ||
            !x86_2(sel, stores[width_index],
                x86_reg(MINIC_X86_REG_RDX, (unsigned int)(8U*bytes)),
                x86_addr(MINIC_X86_REG_R11, 0))) return false;
    }
    return x86_core_load(sel, bindings[0].value, MINIC_X86_REG_R11) &&
        x86_2(sel, MINIC_X86_MI_MOV32_RA,
            x86_reg(put ? MINIC_X86_REG_RCX : MINIC_X86_REG_RAX, 32U),
            x86_addr(MINIC_X86_REG_R11, 0)) &&
        /* Core coverage also requires one unique source-owned occurrence.
         * This semantic marker prints zero bytes; the actual helper call
         * was selected as physical opcodes immediately above. */
        minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            NULL, 0U, NULL, 0U, false, false);
}

static bool x86_integer_conversion(X86CoreSelector *sel,
    const MinicCoreInstruction *ins) {
    MinicCoreIntegerConversionShape shape = {0};
    if (!minic_core_integer_conversion_shape(sel->core, ins, &shape))
        return false;
    if (shape.source_bytes != 16U && shape.target_bytes != 16U)
        return x86_core_load(sel, ins->value.operand,
                MINIC_X86_REG_RAX) &&
            x86_result(sel, ins);
    if (shape.target_bytes == 16U) {
        if (shape.source_bytes == 16U) {
            if (!x86_i128_load(sel, ins->value.operand,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RDX)) return false;
        } else if (!x86_core_load(sel, ins->value.operand,
                MINIC_X86_REG_RAX) ||
            !(shape.source_signed ?
                x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                    MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) &&
                x86_ir(sel, MINIC_X86_MI_SAR64_IR, 63,
                    64U, MINIC_X86_REG_RDX) :
                x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                    MINIC_X86_REG_RDX, MINIC_X86_REG_RDX)))
            return false;
        return x86_i128_store(sel, ins->result,
            MINIC_X86_REG_RAX, MINIC_X86_REG_RDX);
    }
    if (shape.target_bool)
        return x86_i128_load(sel, ins->value.operand,
                MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) &&
            x86_i128_boolean_result(sel, ins->result, false);
    return x86_i128_load(sel, ins->value.operand,
            MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) &&
        x86_result(sel, ins);
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
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return x86_source_jump_label(sel, ins, instruction_id) ||
            x86_source_cpu_has_goto(sel, ins, instruction_id) ||
            x86_source_zero_barrier(sel, ins) ||
            x86_source_zstd_padding(sel, ins) ||
            x86_source_privileged_zero(sel, ins) ||
            x86_source_mmio_or_xchg(sel, ins) ||
            x86_source_pci_mmconfig(sel, ins) ||
            x86_source_rep_stos(sel, ins) ||
            x86_uaccess_call(sel, ins) ||
            x86_source_accumulator_math(sel, ins) ||
            x86_source_cpuid_matched(sel, ins) ||
            x86_source_mulacc_matched(sel, ins) ||
            x86_source_matched_memory(sel, ins) ||
            x86_source_bitscan_matched(sel, ins) ||
            x86_source_prefetchw_alt(sel, ins) ||
            x86_source_cmpxchg128(sel, ins) ||
            x86_source_gen_endbr(sel, ins) ||
            x86_source_crash_register(sel, ins) ||
            x86_source_futex_extable(sel, ins) ||
            x86_source_fdiv_bug(sel, ins) ||
            x86_source_x87_probe(sel, ins) ||
            x86_source_random_seed(sel, ins) ||
            x86_source_pkru(sel, ins) ||
            x86_source_mxcsr(sel, ins) ||
            x86_source_scalar_port_io(sel, ins) ||
            x86_source_scalar_atomic_rmw(sel, ins) ||
            x86_source_scalar_cmpxchg(sel, ins, false) ||
            x86_source_scalar_cmpxchg(sel, ins, true) ||
            x86_source_scalar_bitops(sel, ins) ||
            x86_source_scalar_atomic_cc(sel, ins) ||
            x86_source_nospec(sel, ins) ||
            x86_source_unwind_context(sel, ins) ||
            x86_source_alternative_wrmsr(sel, ins) ||
            x86_source_apic_amd_alt(sel, ins) ||
            x86_source_xsetbv(sel, ins) ||
            x86_source_monitor_wait(sel, ins) ||
            x86_source_segment_read(sel, ins) ||
            x86_source_cache_flush_alt(sel, ins) ||
            x86_source_preempt_gs_dec(sel, ins) ||
            x86_source_boot_cmpsb(sel, ins) ||
            x86_source_boot_divl(sel, ins) ||
            x86_source_boot_movs(sel, ins) ||
            x86_source_control_register(sel, ins) ||
            x86_source_segment_load_extable(sel, ins) ||
            x86_source_boot_segments(sel, ins) ||
            x86_source_debug_register(sel, ins) ||
            x86_source_task_control(sel, ins) ||
            x86_source_vdso_cpunode(sel, ins) ||
            x86_source_msr(sel, ins) ||
            x86_source_hweight(sel, ins) ||
            x86_source_counter(sel, ins) ||
            x86_source_percpu_gs(sel, ins) ||
            x86_source_checksum_carry(sel, ins) ||
            x86_source_ip_fast_csum(sel, ins) ||
            x86_source_gs_atomic_extended(sel, ins) ||
            x86_source_smp_xadd(sel, ins) ||
            x86_source_gs_tied_alu(sel, ins) ||
            x86_source_descriptor_table(sel, ins) ||
            x86_source_legacy_memory(sel, ins) ||
            x86_source_legacy_scalar(sel, ins) ||
            x86_source_delay_mul(sel, ins) ||
            x86_source_delay_wait(sel, ins) ||
            x86_source_preempt_static_call(sel, ins) ||
            x86_source_exception_stack_call(sel, ins) ||
            x86_source_iret_int3(sel, ins) ||
            x86_source_alt_reloc_selftest(sel, ins) ||
            x86_source_three_call_alternative(sel, ins) ||
            x86_source_pv_indirect_call(sel, ins) ||
            x86_source_lowlevel_halt(sel, ins) ||
            x86_source_movnti(sel, ins) ||
            x86_source_boot_bit(sel, ins) ||
            x86_source_boot_test_bit(sel, ins) ||
            x86_source_boot_eflags(sel, ins) ||
            x86_source_ip_here(sel, ins) ||
            x86_source_bug_table_c(sel, ins) ||
            x86_source_nop_const_i(sel, ins);
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
        if (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
            ins->value.call.extended_float_operation !=
                MINIC_CORE_EXT_FLOAT_NONE)
            return x86_x87_extended_call(sel, ins, instruction_id);
        return x86_scalar_call(sel, ins, instruction_id);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        if (x86_i128_type(sel, ins->core_type_id, &sign))
            return x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                    ins->value.integer_value, 64U,
                    MINIC_X86_REG_RAX) &&
                (sign ?
                    x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) &&
                    x86_ir(sel, MINIC_X86_MI_SAR64_IR, 63,
                        64U, MINIC_X86_REG_RDX) :
                    x86_rr(sel, MINIC_X86_MI_XOR64_RR, 64U,
                        MINIC_X86_REG_RDX, MINIC_X86_REG_RDX)) &&
                x86_i128_store(sel, ins->result,
                    MINIC_X86_REG_RAX, MINIC_X86_REG_RDX);
        return x86_scalar_shape(sel, ins->core_type_id, &bytes,
                &sign, &is_bool) &&
            x86_ir(sel, MINIC_X86_MI_MOVABS64_IR,
                ins->value.integer_value, 64U, MINIC_X86_REG_RAX) &&
            x86_result(sel, ins);
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (x86_x87_shape(sel, ins->core_type_id))
            return x86_x87_constant(sel, ins);
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
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
        return x86_fp_conversion(sel, ins, instruction_id);
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        return x86_fp_compare_or_negate(sel, ins);
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
        if (ins->value.binary.left < sel->core->value_count &&
            x86_i128_type(sel,
                sel->core->values[ins->value.binary.left].core_type_id,
                NULL))
            return x86_i128_binary(sel, ins, instruction_id);
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
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION)
            return x86_integer_conversion(sel, ins);
        if (ins->value.operand >= sel->core->value_count ||
            !x86_scalar_shape(sel, ins->core_type_id,
                &bytes, &sign, &is_bool) ||
            !x86_core_load(sel, ins->value.operand, MINIC_X86_REG_RAX))
            return false;
        if (sel->frame->kernel_head_link_addresses &&
            sel->frame->kernel_head_vma_cast_globals != NULL &&
            ins->result < sel->core->value_count &&
            sel->frame->kernel_head_vma_cast_globals[ins->result] !=
                MINIC_CORE_GLOBAL_INVALID) {
            const MinicCoreGlobalId global =
                sel->frame->kernel_head_vma_cast_globals[ins->result];
            const char *symbol;
            if (global >= sel->core->global_count ||
                sel->core->globals == NULL ||
                (symbol = sel->core->globals[global].name) == NULL ||
                !minic_core_inline_asm_symbol_valid(symbol) ||
                !x86_2(sel, MINIC_X86_MI_LEA64_RIP_SYMBOL_R,
                    x86_symbol(symbol, MINIC_X86_MACHINE_RELOC_PC32),
                    x86_reg(MINIC_X86_REG_R11, 64U)) ||
                !x86_rr(sel, MINIC_X86_MI_SUB64_RR, 64U,
                    MINIC_X86_REG_R11, MINIC_X86_REG_RAX) ||
                !x86_2(sel, MINIC_X86_MI_MOV64_IMM32S_SYMBOL_R,
                    x86_symbol(symbol, MINIC_X86_MACHINE_RELOC_ABS32S),
                    x86_reg(MINIC_X86_REG_R11, 64U)) ||
                !x86_rr(sel, MINIC_X86_MI_ADD64_RR, 64U,
                    MINIC_X86_REG_R11, MINIC_X86_REG_RAX))
                return false;
        }
        return x86_result(sel, ins);
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
            scale > INT64_MAX ||
            !x86_core_load(sel, ins->value.pointer_offset.base,
                MINIC_X86_REG_RAX)) return false;
        /* A zero-sized element has no index contribution. Preserve the
         * base address without requiring an otherwise unused index value. */
        if (scale == 0U) return x86_result(sel, ins);
        if (!x86_core_load(sel, ins->value.pointer_offset.index,
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

/* A Core branch can consume a raw 128-bit SSA value without first
 * converting it to _Bool. Fold both limbs before the physical TEST/JNZ:
 * a high-half-only value is true even when its low 64 bits are zero. */
static bool x86_core_condition_value(X86CoreSelector *sel,
    MinicCoreValueId condition) {
    if (condition >= sel->core->value_count) return false;
    if (x86_i128_type(sel,
            sel->core->values[condition].core_type_id, NULL))
        return x86_i128_load(sel, condition,
                   MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) &&
            x86_rr(sel, MINIC_X86_MI_OR64_RR, 64U,
                MINIC_X86_REG_RDX, MINIC_X86_REG_RAX);
    return x86_core_load(sel, condition, MINIC_X86_REG_RAX);
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
            !x86_core_condition_value(sel,
                term->conditional.condition) ||
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
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH: {
        const unsigned int options =
            minic_x86_64_target_codegen_options(sel->target);
        const bool external = (options &
            MINIC_X86_CODEGEN_INDIRECT_EXTERNAL_THUNK) != 0U;
        const bool inlined = !external && (options &
            MINIC_X86_CODEGEN_INDIRECT_INLINE_THUNK) != 0U;
        if (term->indirect_target >= sel->core->value_count ||
            !x86_core_load(sel, term->indirect_target,
                MINIC_X86_REG_RAX) ||
            !(external ?
                x86_1(sel, MINIC_X86_MI_JMP_INDIRECT_THUNK_RAX,
                    x86_symbol("__x86_indirect_thunk_rax",
                        MINIC_X86_MACHINE_RELOC_PC32)) :
              inlined ?
                x86_1(sel, MINIC_X86_MI_JMP_INLINE_THUNK_RAX,
                    x86_reg(MINIC_X86_REG_RAX, 64U)) :
                x86_1(sel, MINIC_X86_MI_JMP_R,
                    x86_reg(MINIC_X86_REG_RAX, 64U)))) return false;
        for (i = 0U; i < sel->core->block_count; ++i)
            if (sel->reachable[i] && sel->core->blocks[i].indirect_entry &&
                !minic_machine_builder_add_successor(&sel->builder,
                    sel->blocks[i])) return false;
        return true;
    }
    case MINIC_CORE_TERMINATOR_RETURN: {
        const MinicCoreTypeId id =
            minic_core_function_return_type(sel->core);
        const MinicCoreType *type = x86_type(sel, id);
        size_t bytes;
        bool sign, is_bool;
        if (type == NULL) return false;
        if (type->kind == MINIC_CORE_TYPE_RECORD) {
            const MinicCoreObjectId object = term->return_object;
            if (minic_core_function_calling_convention(sel->core) !=
                    MINIC_CALLING_CONVENTION_DEFAULT ||
                term->return_value != MINIC_CORE_VALUE_INVALID ||
                object >= sel->core->object_count ||
                sel->core->objects == NULL ||
                !minic_x86_64_core_record_abi_compatible(
                    sel->core->type_context, id,
                    sel->core->objects[object].core_type_id) ||
                !x86_object_address(sel, object, MINIC_X86_REG_R11))
                return false;
            if (minic_x86_64_core_one_gp_record(sel->core->type_context,
                    id, &bytes)) {
                if (sel->frame->has_sret ||
                    !x86_load_record_gp(sel, bytes, 0U)) return false;
            } else if (minic_x86_64_core_two_gp_record(
                    sel->core->type_context, id)) {
                if (sel->frame->has_sret ||
                    !type->has_layout || type->size < 9U ||
                    type->size > 16U ||
                    !x86_load_record_gp(sel, type->size - 8U, 8U) ||
                    !x86_rr(sel, MINIC_X86_MI_MOV64_RR, 64U,
                        MINIC_X86_REG_RAX, MINIC_X86_REG_RDX) ||
                    !x86_load_record_gp(sel, 8U, 0U)) return false;
            } else if (sel->frame->has_sret &&
                sel->frame->sret_save_offset != 0U &&
                minic_x86_64_core_memory_result_record(
                    sel->core->type_context, id, &bytes)) {
                if (sel->frame->sret_save_offset > INT32_MAX ||
                    !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                        x86_addr(MINIC_X86_REG_RBP,
                            -(int64_t)sel->frame->sret_save_offset),
                        x86_reg(MINIC_X86_REG_RDI, 64U)) ||
                    !x86_record_move_bytes(sel, MINIC_X86_REG_R11, 0U,
                        MINIC_X86_REG_RDI, 0U, bytes, true, false, false) ||
                    !x86_2(sel, MINIC_X86_MI_MOV64_AR,
                        x86_addr(MINIC_X86_REG_RBP,
                            -(int64_t)sel->frame->sret_save_offset),
                        x86_reg(MINIC_X86_REG_RAX, 64U))) return false;
            } else return false;
        } else if (type->kind == MINIC_CORE_TYPE_FLOAT) {
            bool fp_double;
            if (term->return_value >= sel->core->value_count) return false;
            if (x86_x87_shape(sel, id)) {
                if (minic_core_function_calling_convention(sel->core) !=
                        MINIC_CALLING_CONVENTION_DEFAULT ||
                    !x86_x87_load(sel, term->return_value)) return false;
            } else if (!x86_float_shape(sel, id, &fp_double) ||
                !x86_float_load_xmm(sel, term->return_value,
                    0U, MINIC_X86_REG_RAX, fp_double)) return false;
        } else if (x86_i128_type(sel, id, NULL)) {
            if (term->return_value >= sel->core->value_count ||
                !x86_i128_load(sel, term->return_value,
                    MINIC_X86_REG_RAX, MINIC_X86_REG_RDX)) return false;
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
    const char *phase = "entry";
    if (machine == NULL || target == NULL || core == NULL || frame == NULL ||
        target->architecture != MINIC_TARGET_ARCH_X86_64 ||
        core->name == NULL || core->type_context == NULL ||
        core->block_count == 0U || core->blocks == NULL ||
        core->entry_block >= core->block_count ||
        frame->value_count != core->value_count ||
        frame->object_count != core->object_count ||
        (core->value_count != 0U && frame->value_offsets == NULL) ||
        frame->frame_size > INT32_MAX) return false;
    count = core->block_count;
    if (count > SIZE_MAX / sizeof(*sel.blocks)) return false;
    sel.core = core;
    sel.frame = frame;
    sel.target = target;
    sel.blocks = malloc(count * sizeof(*sel.blocks));
    sel.false_blocks = malloc(count * sizeof(*sel.false_blocks));
    sel.reachable = calloc(count, sizeof(*sel.reachable));
    if (core->value_count > SIZE_MAX / sizeof(*sel.value_uses))
        goto cleanup;
    sel.value_uses = calloc(core->value_count != 0U ?
        core->value_count : 1U, sizeof(*sel.value_uses));
    if (core->instruction_count > SIZE_MAX /
            sizeof(*sel.instruction_aux)) goto cleanup;
    sel.instruction_aux = calloc(core->instruction_count != 0U ?
        core->instruction_count : 1U, sizeof(*sel.instruction_aux));
    if (sel.blocks == NULL || sel.false_blocks == NULL ||
        sel.reachable == NULL || sel.value_uses == NULL ||
        sel.instruction_aux == NULL ||
        !minic_core_function_value_use_counts(core, sel.value_uses,
            core->value_count) ||
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
    phase = "prologue";
    if (!minic_machine_builder_initialize(&sel.builder, machine) ||
        !minic_x86_64_machine_select_prologue(&sel.builder,
            &sel.frame_view, &prologue)) goto cleanup;
    phase = "block-layout";
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
            if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM &&
                core->inline_asms != NULL &&
                ins->value.inline_asm_id < core->inline_asm_count &&
                core->inline_asms[ins->value.inline_asm_id].is_goto &&
                (minic_x86_64_linux_jump_label_form(
                    core->inline_asms[ins->value.inline_asm_id].template_text,
                    core->inline_asms[ins->value.inline_asm_id].template_length) ||
                 minic_x86_64_linux_cpu_has_goto_form(
                    core->inline_asms[ins->value.inline_asm_id].template_text,
                    core->inline_asms[ins->value.inline_asm_id].template_length))) {
                if (!minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->first) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->second)) goto cleanup;
            } else if (ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR) {
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
            } else if (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
                ins->value.call.extended_float_operation ==
                    MINIC_CORE_EXT_FLOAT_FROM_UNSIGNED64) {
                if (!minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->first) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->second)) goto cleanup;
            } else if (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
                ins->value.call.extended_float_operation ==
                    MINIC_CORE_EXT_FLOAT_TO_UNSIGNED64) {
                if (!minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->first) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->second) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->third)) goto cleanup;
            } else if ((ins->kind ==
                    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
                    ins->kind ==
                    MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) &&
                ins->value.binary.left < core->value_count &&
                x86_i128_type(&sel,
                    core->values[ins->value.binary.left].core_type_id,
                    NULL)) {
                if (!minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->first) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->second) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &aux->third)) goto cleanup;
            } else if (ins->kind ==
                    MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE ||
                    ins->kind ==
                    MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER) {
                MinicCoreFloatingShape shape = {0};
                if (!minic_core_floating_shape(core, ins, &shape))
                    goto cleanup;
                if (shape.integer_bytes == 8U &&
                    !shape.integer_signed) {
                    if (!minic_machine_builder_create_block(&sel.builder,
                            NULL, false, true, &aux->first) ||
                        !minic_machine_builder_create_block(&sel.builder,
                            NULL, false, true, &aux->second)) goto cleanup;
                    if (ins->kind ==
                            MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER &&
                        !minic_machine_builder_create_block(&sel.builder,
                            NULL, false, true, &aux->third)) goto cleanup;
                }
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
    phase = "prologue-continuation";
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
        /* An indirect Core landing pad is a hardware IBT entry, not merely
         * a symbolic label. The old direct path emitted ENDBR for each such
         * block; production MachineIR must do the same explicitly. */
        if (block->indirect_entry &&
            (minic_x86_64_target_codegen_options(target) &
             MINIC_X86_CODEGEN_IBT_ENDBR) != 0U &&
            !x86_0(&sel, MINIC_X86_MI_ENDBR64)) goto cleanup;
        phase = "core-instructions";
        for (j = 0U; j < block->instruction_count; ++j) {
            MinicCoreInstructionId id;
            const MinicCoreInstruction *ins;
            if (!minic_core_block_instruction_at(core, block, j,
                    &id, &ins)) goto cleanup;
            if (getenv("MINIC_X86_TYPED_SELECT_TRACE") != NULL)
                fprintf(stderr,
                    "x86 typed select: function=%s block=%zu instruction=%zu kind=%u result=%u type=%u\n",
                    core->name, i, (size_t)id, (unsigned int)ins->kind,
                    (unsigned int)ins->result,
                    (unsigned int)ins->core_type_id);
            minic_machine_builder_set_origin(&sel.builder, i, id);
            if (!x86_core_instruction(&sel, ins, id)) {
                if (getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL)
                    fprintf(stderr,
                        "x86 typed select reject: function=%s block=%zu instruction=%zu kind=%u result=%u type=%u\n",
                        core->name, i, (size_t)id, (unsigned int)ins->kind,
                        (unsigned int)ins->result,
                        (unsigned int)ins->core_type_id);
                if (getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL &&
                    ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM &&
                    ins->value.inline_asm_id < core->inline_asm_count) {
                    const MinicCoreInlineAsm *rejected =
                        &core->inline_asms[ins->value.inline_asm_id];
                    fprintf(stderr,
                        "x86 typed reject opaque: template_len=%zu template=%.160s\n",
                        rejected->template_length,
                        rejected->template_text ? rejected->template_text : "");
                }
                if (getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL &&
                    ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM &&
                    ins->value.structured_inline_asm.inline_asm_id < core->inline_asm_count) {
                    const MinicCoreInlineAsm *rejected = &core->inline_asms[
                        ins->value.structured_inline_asm.inline_asm_id];
                    fprintf(stderr,"x86 typed reject structured: form=%u variant=%u operands=%zu template=%.100s\n",
                        rejected->target_machine_form_id,
                        rejected->target_machine_form_variant,
                        ins->value.structured_inline_asm.operand_count,
                        rejected->template_text ? rejected->template_text : "");
                }
                goto cleanup;
            }
        }
        minic_machine_builder_clear_origin(&sel.builder);
        phase = "core-terminator";
        if (!x86_core_terminator(&sel, i)) {
            if (getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL)
                fprintf(stderr,
                    "x86 typed select reject: function=%s stage=core-terminator block=%zu terminator=%u\n",
                    core->name, i, (unsigned int)block->terminator.kind);
            goto cleanup;
        }
    }
    phase = "gnu-asm-preflight";
    ok = minic_machine_gnu_asm_preflight(machine, &sel.policy);
cleanup:
    if (!ok && getenv("MINIC_X86_TYPED_REJECT_TRACE") != NULL)
        fprintf(stderr,
            "x86 typed select reject: function=%s stage=%s\n",
            core->name, phase);
    free(sel.blocks);
    free(sel.false_blocks);
    free(sel.reachable);
    free(sel.value_uses);
    free(sel.instruction_aux);
    if (!ok) minic_machine_function_destroy(machine);
    return ok;
}
