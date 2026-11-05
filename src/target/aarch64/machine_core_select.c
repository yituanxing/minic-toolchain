#include "target/aarch64/machine_core_select.h"
#include "target/aarch64/machine_frame_select.h"
#include "target/aarch64/machine_opcode.h"
#include "target/aarch64/machine_source_asm.h"
#include "target/aarch64/neon_emit.h"
#include "target/common/core_block_walk.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/core_abi_value_shape.h"
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

static bool a64_normalize(A64CoreSelection *, size_t, bool, unsigned int);
static bool a64_symbol_materialize(A64CoreSelection *, const char *,
    MinicElfModuleSymbolKind);
static bool a64_branch(A64CoreSelection *, size_t);

static MinicMachineOperand a64_gp(unsigned int reg, unsigned int bits,
    unsigned int access) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){reg, (uint16_t)bits,
        MINIC_MACHINE_REGISTER_GENERAL};
    op.value.physical.access = access;
    return op;
}

static MinicMachineOperand a64_fp(unsigned int reg, unsigned int bits,
    unsigned int access) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){reg, (uint16_t)bits,
        MINIC_MACHINE_REGISTER_FLOAT};
    op.value.physical.access = access;
    return op;
}

static MinicMachineOperand a64_vector(unsigned int reg,
    unsigned int access) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_REGISTER;
    op.value.physical.reg = (MinicMachineRegister){reg, 128U,
        MINIC_MACHINE_REGISTER_VECTOR};
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

static bool a64_load_constant_width(A64CoreSelection *sel, unsigned int reg,
    uint64_t value, unsigned int bits) {
    unsigned int limb;
    if (bits != 32U && bits != 64U) return false;
    for (limb = 0U; limb < bits / 16U; ++limb) {
        const unsigned int shift = limb * 16U;
        const MinicMachineOperand ops[] = {
            a64_gp(reg, bits, limb == 0U ? MINIC_MACHINE_REGISTER_WRITE :
                (MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE)),
            a64_immediate((int64_t)((value >> shift) & UINT64_C(65535))),
            a64_immediate((int64_t)shift)
        };
        if ((limb == 0U || ((value >> shift) & UINT64_C(65535)) != 0U) &&
            !a64_emit(sel, limb == 0U ? MINIC_A64_MACHINE_MOVZ :
                MINIC_A64_MACHINE_MOVK, ops, 3U)) return false;
    }
    return true;
}

static bool a64_load_constant(A64CoreSelection *sel, unsigned int reg,
    uint64_t value) {
    return a64_load_constant_width(sel, reg, value, 64U);
}

static bool a64_load_wide_half(A64CoreSelection *sel,
    size_t offset, unsigned int reg) {
    return minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
        minic_a64_owned_slot(offset),
        (MinicA64FrameRegister){MINIC_A64_REG_X, reg},
        (MinicA64ScratchPolicy){0U, 0U});
}

static bool a64_store_wide_half(A64CoreSelection *sel,
    size_t offset, unsigned int reg) {
    return minic_a64_machine_emit_frame_store(&sel->builder, &sel->view,
        minic_a64_owned_slot(offset),
        (MinicA64FrameRegister){MINIC_A64_REG_X, reg},
        (MinicA64ScratchPolicy){0U, 0U});
}

static bool a64_wide_core_slot(A64CoreSelection *sel,
    MinicCoreValueId value, size_t *offset) {
    return minic_lp64_frame_plan_wide_slot(sel->ctx->frame_plan,
        value, offset) && *offset <= MINIC_LARGE_FRAME_MAX - 16U;
}

static bool a64_wide_float(const MinicCoreFunction *fn,
    MinicCoreTypeId id) {
    const MinicCoreType *type = fn->type_context != NULL ?
        minic_core_type_get(fn->type_context,
            minic_core_type_unqualified(fn->type_context, id)) : NULL;
    return type != NULL && type->has_layout && type->size == 16U &&
        type->kind == MINIC_CORE_TYPE_FLOAT &&
        type->value.floating.format == MINIC_CORE_FLOAT_BINARY128;
}

static bool a64_wide_integer(const MinicCoreFunction *fn,
    MinicCoreTypeId id) {
    const MinicCoreType *type = fn->type_context != NULL ?
        minic_core_type_get(fn->type_context,
            minic_core_type_unqualified(fn->type_context, id)) : NULL;
    return type != NULL && type->has_layout && type->size == 16U &&
        type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->value.integer.bit_width == 128U;
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

static bool a64_load_fp(A64CoreSelection *sel, MinicCoreValueId value,
    unsigned int reg, unsigned int bits) {
    size_t offset;
    return (bits == 32U || bits == 64U) &&
        minic_lp64_frame_plan_value_slot(sel->ctx->frame_plan, value, &offset) &&
        minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
            minic_a64_owned_slot(offset),
            (MinicA64FrameRegister){bits == 32U ? MINIC_A64_REG_S :
                MINIC_A64_REG_D, reg}, (MinicA64ScratchPolicy){0U, 0U});
}

static bool a64_store_fp(A64CoreSelection *sel, MinicCoreValueId value,
    unsigned int reg, unsigned int bits) {
    size_t offset;
    return (bits == 32U || bits == 64U) &&
        minic_lp64_frame_plan_value_slot(sel->ctx->frame_plan, value, &offset) &&
        minic_a64_machine_emit_frame_store(&sel->builder, &sel->view,
            minic_a64_owned_slot(offset),
            (MinicA64FrameRegister){bits == 32U ? MINIC_A64_REG_S :
                MINIC_A64_REG_D, reg}, (MinicA64ScratchPolicy){0U, 0U});
}

static bool a64_fp_rr(A64CoreSelection *sel, uint32_t opcode,
    unsigned int dst_bits, unsigned int src_bits,
    unsigned int dst, unsigned int src) {
    const MinicMachineOperand ops[] = {
        a64_fp(dst, dst_bits, MINIC_MACHINE_REGISTER_WRITE),
        a64_fp(src, src_bits, MINIC_MACHINE_REGISTER_READ)
    };
    return a64_emit(sel, opcode, ops, 2U);
}

static bool a64_fp_rrr(A64CoreSelection *sel, uint32_t opcode,
    unsigned int bits, unsigned int dst, unsigned int left, unsigned int right) {
    const MinicMachineOperand ops[] = {
        a64_fp(dst, bits, MINIC_MACHINE_REGISTER_WRITE),
        a64_fp(left, bits, MINIC_MACHINE_REGISTER_READ),
        a64_fp(right, bits, MINIC_MACHINE_REGISTER_READ)
    };
    return a64_emit(sel, opcode, ops, 3U);
}

static bool a64_gp_fp(A64CoreSelection *sel, uint32_t opcode,
    bool gp_destination, unsigned int gp_bits,
    unsigned int fp_bits, unsigned int gp_reg, unsigned int fp_reg) {
    const MinicMachineOperand ops[] = {
        gp_destination ? a64_gp(gp_reg, gp_bits, MINIC_MACHINE_REGISTER_WRITE)
            : a64_fp(fp_reg, fp_bits, MINIC_MACHINE_REGISTER_WRITE),
        gp_destination ? a64_fp(fp_reg, fp_bits, MINIC_MACHINE_REGISTER_READ)
            : a64_gp(gp_reg, gp_bits, MINIC_MACHINE_REGISTER_READ)
    };
    return a64_emit(sel, opcode, ops, 2U);
}

static bool a64_float_type(const MinicCoreFunction *fn,
    MinicCoreTypeId id, unsigned int *bits) {
    MinicCoreAbiValueShape shape;
    if (bits == NULL || !minic_core_abi_value_shape(fn, id, &shape) ||
        shape.kind != MINIC_CORE_TYPE_FLOAT ||
        (shape.float_format != MINIC_CORE_FLOAT_BINARY32 &&
         shape.float_format != MINIC_CORE_FLOAT_BINARY64)) return false;
    *bits = (unsigned int)(shape.bytes * 8U);
    return true;
}

/* The existing Core numeric-shape visitor owns width and conversion
 * admission. These target operations only select physical AArch64 F registers
 * and the already planned SSA frame locations; no frontend C type escapes. */
static bool a64_floating(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    unsigned int bits, source_bits;
    uint32_t opcode;
    const MinicCoreValueId left = ins->value.binary.left;
    const MinicCoreValueId right = ins->value.binary.right;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (a64_wide_float(fn, ins->core_type_id)) {
            size_t offset;
            if (!a64_wide_core_slot(sel, ins->result, &offset) ||
                !a64_load_constant(sel, A64_T0,
                    ins->value.floating128_bits.low) ||
                !a64_store_wide_half(sel, offset, A64_T0) ||
                !a64_load_constant(sel, A64_T0,
                    ins->value.floating128_bits.high)) return false;
            return a64_store_wide_half(sel, offset + 8U, A64_T0);
        }
        if (!a64_float_type(fn, ins->core_type_id, &bits) ||
            !a64_load_constant_width(sel, A64_T0,
                ins->value.floating_bits, bits)) return false;
        return a64_store_core(sel, ins->result, A64_T0, bits);
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        if (!minic_multi_direct_floating_shape(fn, ins, NULL, NULL) ||
            left >= fn->value_count || right >= fn->value_count ||
            !a64_float_type(fn, fn->values[left].core_type_id, &bits) ||
            !a64_float_type(fn, fn->values[right].core_type_id,
                &source_bits) || bits != source_bits ||
            !a64_load_fp(sel, left, A64_T0, bits) ||
            !a64_load_fp(sel, right, A64_T1, bits)) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL ||
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ||
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL) {
            const MinicMachineOperand ops[] = {
                a64_fp(A64_T0, bits, MINIC_MACHINE_REGISTER_READ),
                a64_fp(A64_T1, bits, MINIC_MACHINE_REGISTER_READ)
            };
            const int64_t condition =
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL ? 0 :
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ? 3 : 9;
            return a64_emit(sel, MINIC_A64_MACHINE_FCMP, ops, 2U) &&
                a64_cset(sel, A64_T0, condition) &&
                a64_store_core(sel, ins->result, A64_T0, 32U);
        }
        opcode = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD ||
            ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD
                ? MINIC_A64_MACHINE_FADD :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT ||
            ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                ? MINIC_A64_MACHINE_FSUB :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY ||
            ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                ? MINIC_A64_MACHINE_FMUL : MINIC_A64_MACHINE_FDIV;
        return a64_fp_rrr(sel, opcode, bits, A64_T0, A64_T0, A64_T1) &&
            a64_store_fp(sel, ins->result, A64_T0, bits);
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
        const MinicCoreValueId source = ins->value.operand;
        if (source >= fn->value_count ||
            !minic_multi_direct_floating_shape(fn, ins, NULL, NULL) ||
            !a64_float_type(fn, fn->values[source].core_type_id,
                &source_bits) ||
            !a64_float_type(fn, ins->core_type_id, &bits) ||
            !a64_load_fp(sel, source, A64_T0, source_bits) ||
            !a64_fp_rr(sel, ins->kind ==
                MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE ? MINIC_A64_MACHINE_FNEG :
                MINIC_A64_MACHINE_FCVT, bits, source_bits,
                A64_T0, A64_T0)) return false;
        return a64_store_fp(sel, ins->result, A64_T0, bits);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
        size_t width;
        bool sign;
        const MinicCoreValueId source = ins->value.operand;
        if (source >= fn->value_count ||
            !minic_multi_direct_floating_shape(fn, ins, &width, &sign) ||
            !a64_load_core(sel, source, A64_T0,
                width == 8U ? 64U : 32U) ||
            (width < 4U && !a64_normalize(sel, width, sign, A64_T0)) ||
            !a64_gp_fp(sel, sign ? MINIC_A64_MACHINE_SCVTF :
                MINIC_A64_MACHINE_UCVTF, false,
                width == 8U ? 64U : 32U, 64U, A64_T0, A64_T0))
            return false;
        return a64_store_fp(sel, ins->result, A64_T0, 64U);
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
        size_t width;
        bool sign;
        const MinicCoreValueId source = ins->value.operand;
        if (source >= fn->value_count ||
            !minic_multi_direct_floating_shape(fn, ins, &width, &sign) ||
            !a64_load_fp(sel, source, A64_T0, 64U) ||
            !a64_gp_fp(sel, sign ? MINIC_A64_MACHINE_FCVTZS :
                MINIC_A64_MACHINE_FCVTZU, true,
                width == 8U ? 64U : 32U, 64U, A64_T0, A64_T0) ||
            !a64_normalize(sel, width, sign, A64_T0)) return false;
        return a64_store_core(sel, ins->result, A64_T0,
            width == 8U ? 64U : 32U);
    }
    default: return false;
    }
}

static bool a64_scalar_abi(const MinicCoreFunction *fn,
    MinicCoreTypeId type_id, MinicCoreAbiValueShape *shape) {
    return minic_core_abi_value_shape(fn, type_id, shape) &&
        (shape->kind == MINIC_CORE_TYPE_VOID ||
         (shape->kind == MINIC_CORE_TYPE_POINTER && shape->bytes == 8U) ||
         (shape->kind == MINIC_CORE_TYPE_INTEGER && shape->bytes <= 8U) ||
         (shape->kind == MINIC_CORE_TYPE_FLOAT &&
          (shape->float_format == MINIC_CORE_FLOAT_BINARY32 ||
           shape->float_format == MINIC_CORE_FLOAT_BINARY64)));
}

static MinicA64FrameRegister a64_abi_register(
    const MinicCoreAbiValueShape *shape, unsigned int reg) {
    MinicA64FrameRegister result = {0};
    result.number = reg;
    if (shape->kind == MINIC_CORE_TYPE_FLOAT)
        result.kind = shape->bytes == 4U ? MINIC_A64_REG_S : MINIC_A64_REG_D;
    else result.kind = shape->bytes == 8U ? MINIC_A64_REG_X : MINIC_A64_REG_W;
    return result;
}

static bool a64_core_value_transfer(A64CoreSelection *sel,
    MinicCoreValueId id, const MinicA64FrameView *view,
    MinicA64FrameRegister reg, bool store) {
    size_t at;
    if (sel->ctx->frame_plan == NULL ||
        !minic_lp64_frame_plan_value_slot(sel->ctx->frame_plan, id, &at))
        return false;
    return store ? minic_a64_machine_emit_frame_store(&sel->builder, view,
            minic_a64_owned_slot(at), reg,
            (MinicA64ScratchPolicy){0U, 0U}) :
        minic_a64_machine_emit_frame_load(&sel->builder, view,
            minic_a64_owned_slot(at), reg,
            (MinicA64ScratchPolicy){0U, 0U});
}

/* AAPCS64's immutable Common-planned locations are the authority.  This
 * selector handles actual physical moves; it does not recalculate AAPCS64
 * register/stack assignment or reopen a frontend C function signature. */
static bool a64_parameter(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicLP64ArgumentLocation *location = NULL;
    MinicCoreAbiValueShape shape;
    MinicA64FrameRegister reg;
    if (sel->ctx->incoming_plan == NULL ||
        !minic_multi_direct_scalar_parameter_location(fn, ins,
            sel->ctx->incoming_plan, &location) ||
        !a64_scalar_abi(fn, ins->core_type_id, &shape) ||
        shape.kind == MINIC_CORE_TYPE_VOID) return false;
    if (shape.kind == MINIC_CORE_TYPE_FLOAT) {
        if ((location->fp_registers != 0U) ==
            (location->stack_slots != 0U)) return false;
        reg = a64_abi_register(&shape,
            (unsigned int)(location->fp_registers != 0U ?
                location->fp_first : 9U));
    } else {
        if ((location->gp_registers != 0U) ==
            (location->stack_slots != 0U)) return false;
        reg = a64_abi_register(&shape,
            (unsigned int)(location->gp_registers != 0U ?
                location->gp_first : 9U));
    }
    if (location->stack_slots != 0U &&
        (location->stack_first > SIZE_MAX / 8U ||
         !minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
            (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING,
                location->stack_first * 8U}, reg,
            (MinicA64ScratchPolicy){0U, 0U}))) return false;
    return a64_core_value_transfer(sel, ins->result, &sel->view, reg, true);
}

static bool a64_call(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    const size_t begin = indirect ? ins->value.indirect_call.argument_begin :
        ins->value.call.argument_begin;
    const size_t count = indirect ? ins->value.indirect_call.argument_count :
        ins->value.call.argument_count;
    const MinicLP64PreparedCall *planned;
    MinicCoreAbiValueShape result;
    MinicA64FrameView live = sel->view;
    size_t call_id, i;
    if (fn->instructions == NULL || ins < fn->instructions ||
        ins >= fn->instructions + fn->instruction_count ||
        sel->ctx->outgoing_calls == NULL ||
        !a64_scalar_abi(fn, ins->core_type_id, &result) ||
        begin > fn->call_argument_count ||
        count > fn->call_argument_count - begin ||
        (!indirect && ins->value.call.extended_float_operation !=
            MINIC_CORE_EXT_FLOAT_NONE)) return false;
    call_id = (size_t)(ins - fn->instructions);
    planned = minic_lp64_function_call_plan(sel->ctx->outgoing_calls, call_id);
    if (planned == NULL || planned->argument_count != count ||
        planned->stack_bytes > 4095U ||
        (planned->stack_bytes % 16U) != 0U) return false;
    live.outgoing_sp_bytes = planned->stack_bytes;
    if (planned->stack_bytes != 0U &&
        !a64_rri(sel, MINIC_A64_MACHINE_SUB_IMM, 64U,
            MINIC_A64_MACHINE_SP, MINIC_A64_MACHINE_SP,
            (int64_t)planned->stack_bytes)) return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreCallArgument *arg = &fn->call_arguments[begin + i];
        const MinicLP64ArgumentLocation *location = &planned->arguments[i];
        MinicCoreAbiValueShape shape;
        MinicA64FrameRegister reg;
        bool in_register;
        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            arg->value.value_id >= fn->value_count ||
            !a64_scalar_abi(fn,
                fn->values[arg->value.value_id].core_type_id, &shape) ||
            shape.kind == MINIC_CORE_TYPE_VOID) return false;
        in_register = shape.kind == MINIC_CORE_TYPE_FLOAT ?
            location->fp_registers != 0U : location->gp_registers != 0U;
        if (in_register) {
            const size_t index = shape.kind == MINIC_CORE_TYPE_FLOAT ?
                location->fp_first : location->gp_first;
            if (index >= 8U) return false;
            reg = a64_abi_register(&shape, (unsigned int)index);
        } else {
            if (location->stack_slots == 0U ||
                location->stack_first > SIZE_MAX / 8U) return false;
            reg = a64_abi_register(&shape, 9U);
        }
        if (!a64_core_value_transfer(sel, arg->value.value_id,
                &live, reg, false)) return false;
        if (!in_register &&
            !minic_a64_machine_emit_frame_store(&sel->builder, &live,
                (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING,
                    location->stack_first * 8U}, reg,
                (MinicA64ScratchPolicy){0U, 0U})) return false;
    }
    if (indirect) {
        const MinicMachineOperand op[] = {
            a64_gp(16U, 64U, MINIC_MACHINE_REGISTER_READ)
        };
        if (!a64_core_value_transfer(sel, ins->value.indirect_call.callee,
                &live, (MinicA64FrameRegister){MINIC_A64_REG_X, 16U},
                false) || !a64_emit(sel, MINIC_A64_MACHINE_BLR, op, 1U))
            return false;
    } else {
        const char *name;
        const MinicElfModuleSymbolFacts facts =
            ins->value.call.callee_id < fn->callee_count ?
                minic_multi_elf_module_symbol_facts(sel->ctx->module,
                    fn->callees[ins->value.call.callee_id].name,
                    MINIC_ELF_MODULE_SYMBOL_FUNCTION) :
                (MinicElfModuleSymbolFacts){0};
        if (ins->value.call.callee_id >= fn->callee_count ||
            (name = fn->callees[ins->value.call.callee_id].name) == NULL)
            return false;
        if (facts.undefined_weak) {
            const MinicMachineOperand op[] = {
                a64_gp(16U, 64U, MINIC_MACHINE_REGISTER_READ)
            };
            if (!a64_symbol_materialize(sel, name,
                    MINIC_ELF_MODULE_SYMBOL_FUNCTION) ||
                !a64_rr(sel, MINIC_A64_MACHINE_MOV, 64U, 16U, A64_T0) ||
                !a64_emit(sel, MINIC_A64_MACHINE_BLR, op, 1U)) return false;
        } else {
            const MinicMachineOperand op[] = {
                a64_symbol(name, MINIC_A64_RELOC_DIRECT)
            };
            if (!a64_emit(sel, MINIC_A64_MACHINE_BL, op, 1U)) return false;
        }
    }
    if (planned->stack_bytes != 0U &&
        !a64_rri(sel, MINIC_A64_MACHINE_ADD_IMM, 64U,
            MINIC_A64_MACHINE_SP, MINIC_A64_MACHINE_SP,
            (int64_t)planned->stack_bytes)) return false;
    if (result.kind == MINIC_CORE_TYPE_VOID) return true;
    if (result.kind == MINIC_CORE_TYPE_FLOAT)
        return a64_core_value_transfer(sel, ins->result, &sel->view,
            a64_abi_register(&result, 0U), true);
    if (result.kind == MINIC_CORE_TYPE_INTEGER && result.bytes < 4U &&
        !a64_normalize(sel, result.bytes, result.integer_signed, 0U))
        return false;
    return a64_core_value_transfer(sel, ins->result, &sel->view,
        a64_abi_register(&result, 0U), true);
}

/* The Core bitcast's semantic shape is shared; AAPCS64 owns only its native
 * pointer/integer width conversion, with no C representation in MachineIR. */
static bool a64_scalar_bitcast(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreValueId source = ins->value.operand;
    const MinicCoreFunction *fn = sel->ctx->fn;
    bool pointer_to_integer, sign, source_pointer;
    size_t width;
    if (source >= fn->value_count ||
        !minic_multi_direct_scalar_bitcast_shape(fn, ins,
            &pointer_to_integer, &width, &sign, &source_pointer))
        return false;
    if (pointer_to_integer) {
        if (!a64_load_core(sel, source, A64_T0, 64U)) return false;
        if (width == 1U &&
            (!a64_cmp_zero(sel, 64U, A64_T0) ||
             !a64_cset(sel, A64_T0, 1U))) return false;
        if (width == 4U &&
            !a64_extend_word(sel, A64_T0, A64_T0, sign)) return false;
        return a64_store_core(sel, ins->result, A64_T0,
            width == 8U ? 64U : 32U);
    }
    if (!a64_load_core(sel, source, A64_T0,
            source_pointer || width == 8U ? 64U : 32U)) return false;
    if (!source_pointer && width != 8U &&
        (!a64_normalize(sel, width, sign, A64_T0) ||
         !a64_extend_word(sel, A64_T0, A64_T0, sign))) return false;
    return a64_store_core(sel, ins->result, A64_T0, 64U);
}

/* AAPCS64's canonical x29 frame chain and saved LR are independent of the
 * source language; layout is owned by the already prepared MachineFrame. */
static bool a64_call_frame_address(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicMachineLP64FrameContract *frame = sel->ctx->machine_frame;
    const MinicCoreFunction *fn = sel->ctx->fn;
    if (minic_multi_direct_call_frame_level0_shape(fn, ins,
            MINIC_CORE_CALL_FRAME_ADDRESS_FRAME)) {
        return a64_rr(sel, MINIC_A64_MACHINE_MOV, 64U, A64_T0, 29U) &&
            a64_store_core(sel, ins->result, A64_T0, 64U);
    }
    if (ins->value.call_frame_address.kind ==
            MINIC_CORE_CALL_FRAME_ADDRESS_FRAME &&
        ins->value.call_frame_address.level == 1U &&
        minic_target_info_call_frame_address_supported(sel->ctx->target,
            MINIC_CALL_FRAME_ADDRESS_FRAME, 1U)) {
        const MinicMachineOperand ops[] = {
            a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_WRITE),
            a64_address(29U)
        };
        return a64_emit(sel, MINIC_A64_MACHINE_LDR_X, ops, 2U) &&
            a64_store_core(sel, ins->result, A64_T0, 64U);
    }
    if (!minic_multi_direct_call_frame_return_level0_shape(fn, ins))
        return false;
    if (frame->has_calls) {
        if (!minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
                minic_a64_owned_slot(frame->save_offset),
                (MinicA64FrameRegister){MINIC_A64_REG_X, A64_T0},
                (MinicA64ScratchPolicy){0U, 0U})) return false;
    } else if (!a64_rr(sel, MINIC_A64_MACHINE_MOV, 64U, A64_T0, 30U))
        return false;
    return a64_store_core(sel, ins->result, A64_T0, 64U);
}

static bool a64_fixed_sp_read(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFixedRegisterBinding *binding;
    return !sel->ctx->machine_frame->has_dynamic_stack &&
        minic_multi_direct_fixed_register_read_shape(sel->ctx->target,
            sel->ctx->fn, ins, &binding) &&
        binding->register_name_length == 2U &&
        memcmp(binding->register_name, "sp", 2U) == 0 &&
        a64_rri(sel, MINIC_A64_MACHINE_ADD_IMM, 64U,
            A64_T0, MINIC_A64_MACHINE_SP, 0) &&
        a64_store_core(sel, ins->result, A64_T0, 64U);
}

/* Source-owned zero-operand hardware hints are carried through their exact
 * proven Core source bytes. They are NOT arbitrary selected instruction text:
 * the independent target source-ASM grammar validates a fixed hint subset. */
static bool a64_opaque_hint(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreInlineAsm *source;
    MinicMachineInstruction candidate = {0};
    MinicMachineSourceAsmInput input = {0};
    if (ins->value.inline_asm_id >= fn->inline_asm_count) return false;
    source = &fn->inline_asms[ins->value.inline_asm_id];
    if (source->is_goto || source->register_clobber_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        source->target_machine_form_id != 0U ||
        source->target_machine_form_variant != 0U ||
        source->template_text == NULL) return false;
    input.source_bytes = source->template_text;
    input.source_byte_length = source->template_length;
    input.effects = (source->is_volatile ? MINIC_MACHINE_EFFECT_VOLATILE : 0U) |
        (source->has_memory_clobber ?
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER : 0U);
    input.has_unique_id = source->source_asm_unique_id != 0U;
    input.unique_id = source->source_asm_unique_id;
    candidate.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    candidate.source_asm_template = source->template_text;
    candidate.source_asm_length = source->template_length;
    candidate.effects = input.effects | MINIC_MACHINE_EFFECT_SOURCE_ASM;
    return minic_aarch64_machine_source_asm_validate(sel->ctx->target,
        &candidate, NULL) &&
        minic_machine_builder_emit_source_asm_typed(&sel->builder, &input);
}

static bool a64_memory_register(A64CoreSelection *sel,
    uint32_t opcode, bool vector, unsigned int reg, unsigned int bits,
    unsigned int base, size_t displacement, bool store, bool vol) {
    MinicMachineOperand address = a64_address(base);
    MinicMachineOperand operands[2];
    if (displacement > (size_t)INT64_MAX) return false;
    address.value.address.displacement = (int64_t)displacement;
    operands[0] = vector ? a64_vector(reg,
        store ? MINIC_MACHINE_REGISTER_READ : MINIC_MACHINE_REGISTER_WRITE) :
        a64_gp(reg, bits,
            store ? MINIC_MACHINE_REGISTER_READ : MINIC_MACHINE_REGISTER_WRITE);
    operands[1] = address;
    if (!a64_emit(sel, opcode, operands, 2U)) return false;
    if (vol) {
        MinicMachineBlock *block = &sel->builder.function->blocks[
            sel->builder.current_block];
        block->instructions[block->instruction_count - 1U].effects |=
            MINIC_MACHINE_EFFECT_VOLATILE;
    }
    return true;
}

/* One checked wide transport shared by i128, binary128 and NEON register
 * vectors. Frame slots come from Common and ISA-native moves preserve every
 * payload bit; neither frontend types nor line-oriented assembly is needed. */
static bool a64_wide_memory(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicLP64FramePlan *frame = sel->ctx->frame_plan;
    const bool vector = ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_LOAD ||
        ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_STORE ||
        ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR;
    const bool load = ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
        ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_LOAD;
    const bool vol = !vector && (load ? ins->value.load.is_volatile :
        ins->value.store.is_volatile);
    MinicCoreValueId address, payload;
    size_t wide_slot;
    if (ins->kind == MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR) {
        const MinicCoreValueId left = ins->value.binary.left;
        const MinicCoreValueId right = ins->value.binary.right;
        size_t lhs, rhs, result;
        const MinicMachineOperand ops[] = {
            a64_vector(16U, MINIC_MACHINE_REGISTER_WRITE),
            a64_vector(16U, MINIC_MACHINE_REGISTER_READ),
            a64_vector(17U, MINIC_MACHINE_REGISTER_READ)
        };
        return left < fn->value_count && right < fn->value_count &&
            minic_aarch64_neon_vector128_core_type(fn->type_context,
                ins->core_type_id) &&
            a64_wide_core_slot(sel, left, &lhs) &&
            a64_wide_core_slot(sel, right, &rhs) &&
            a64_wide_core_slot(sel, ins->result, &result) &&
            minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
                minic_a64_owned_slot(lhs),
                (MinicA64FrameRegister){MINIC_A64_REG_Q, 16U},
                (MinicA64ScratchPolicy){0U, 0U}) &&
            minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
                minic_a64_owned_slot(rhs),
                (MinicA64FrameRegister){MINIC_A64_REG_Q, 17U},
                (MinicA64ScratchPolicy){0U, 0U}) &&
            a64_emit(sel, MINIC_A64_MACHINE_VECTOR_EOR_16B, ops, 3U) &&
            minic_a64_machine_emit_frame_store(&sel->builder, &sel->view,
                minic_a64_owned_slot(result),
                (MinicA64FrameRegister){MINIC_A64_REG_Q, 16U},
                (MinicA64ScratchPolicy){0U, 0U});
    }
    if (load) {
        if (vector) {
            address = ins->value.load.address;
            if (!minic_aarch64_neon_vector128_core_type(fn->type_context,
                    ins->core_type_id) ||
                !a64_wide_core_slot(sel, ins->result, &wide_slot))
                return false;
        } else if (!minic_multi_direct_wide_scalar_load_location(fn,
                ins, frame, &address, &wide_slot)) return false;
        payload = ins->result;
    } else {
        size_t offset;
        payload = ins->value.store.stored_value;
        address = ins->value.store.address;
        if (payload >= fn->value_count) return false;
        if (vector) {
            if (!minic_aarch64_neon_vector128_core_type(fn->type_context,
                    fn->values[payload].core_type_id) ||
                !a64_wide_core_slot(sel, payload, &wide_slot)) return false;
        } else if (!minic_multi_direct_wide_store_shape(fn, ins,
                frame, &wide_slot)) return false;
        if (!minic_lp64_frame_plan_value_slot(frame, address, &offset))
            return false;
    }
    if (address >= fn->value_count ||
        !a64_load_core(sel, address, A64_T1, 64U)) return false;
    if (vector || a64_wide_float(fn, load ? ins->core_type_id :
            fn->values[payload].core_type_id)) {
        if (!load && !minic_a64_machine_emit_frame_load(&sel->builder,
                &sel->view, minic_a64_owned_slot(wide_slot),
                (MinicA64FrameRegister){MINIC_A64_REG_Q, 16U},
                (MinicA64ScratchPolicy){0U, 0U})) return false;
        if (!a64_memory_register(sel, load ? MINIC_A64_MACHINE_LDR_FP_Q :
                MINIC_A64_MACHINE_STR_FP_Q, true, 16U, 128U,
                A64_T1, 0U, !load, vol)) return false;
        return !load || minic_a64_machine_emit_frame_store(&sel->builder,
            &sel->view, minic_a64_owned_slot(wide_slot),
            (MinicA64FrameRegister){MINIC_A64_REG_Q, 16U},
            (MinicA64ScratchPolicy){0U, 0U});
    }
    if (!a64_wide_integer(fn, load ? ins->core_type_id :
            fn->values[payload].core_type_id)) return false;
    if (!load && (!a64_load_wide_half(sel, wide_slot, A64_T0) ||
        !a64_load_wide_half(sel, wide_slot + 8U, A64_T2))) return false;
    if (!a64_memory_register(sel, load ? MINIC_A64_MACHINE_LDR_X :
            MINIC_A64_MACHINE_STR_X, false, A64_T0, 64U,
            A64_T1, 0U, !load, vol) ||
        !a64_memory_register(sel, load ? MINIC_A64_MACHINE_LDR_X :
            MINIC_A64_MACHINE_STR_X, false, A64_T2, 64U,
            A64_T1, 8U, !load, vol)) return false;
    return !load || (a64_store_wide_half(sel, wide_slot, A64_T0) &&
        a64_store_wide_half(sel, wide_slot + 8U, A64_T2));
}

/* Physical x9=source, x10=destination.  Short composites are exact linear
 * copies; large Linux records get a real MachineCFG loop, not hundreds of
 * thousands of duplicated assembly strings or a hidden libc call. */
static bool a64_copy_one_byte(A64CoreSelection *sel,
    bool source_volatile, bool destination_volatile) {
    return a64_memory_register(sel, MINIC_A64_MACHINE_LDRB,
            false, 11U, 32U, A64_T0, 0U, false, source_volatile) &&
        a64_memory_register(sel, MINIC_A64_MACHINE_STRB,
            false, 11U, 32U, A64_T1, 0U, true, destination_volatile) &&
        a64_rri(sel, MINIC_A64_MACHINE_ADD_IMM,
            64U, A64_T0, A64_T0, 1) &&
        a64_rri(sel, MINIC_A64_MACHINE_ADD_IMM,
            64U, A64_T1, A64_T1, 1);
}

static bool a64_copy_bytes(A64CoreSelection *sel,
    size_t bytes, bool source_volatile, bool destination_volatile) {
    size_t i;
    if (bytes == 0U || bytes > 16U * 1024U * 1024U) return false;
    if (bytes <= 64U) {
        for (i = 0U; i < bytes; ++i)
            if (!a64_copy_one_byte(sel,
                    source_volatile, destination_volatile)) return false;
        return true;
    }
    {
        size_t loop, done;
        MinicMachineOperand branch[2];
        if (!a64_load_constant(sel, A64_T3, (uint64_t)bytes) ||
            !minic_machine_builder_create_block(&sel->builder, NULL,
                false, true, &loop) ||
            !minic_machine_builder_create_block(&sel->builder, NULL,
                false, true, &done) ||
            !a64_branch(sel, loop) ||
            !minic_machine_builder_position_at(&sel->builder, loop) ||
            !a64_copy_one_byte(sel, source_volatile,
                destination_volatile) ||
            !a64_rri(sel, MINIC_A64_MACHINE_SUB_IMM, 64U,
                A64_T3, A64_T3, 1)) return false;
        branch[0] = a64_gp(A64_T3, 64U, MINIC_MACHINE_REGISTER_READ);
        branch[1] = a64_block(loop);
        return a64_emit(sel, MINIC_A64_MACHINE_CBNZ, branch, 2U) &&
            minic_machine_builder_add_successor(&sel->builder, loop) &&
            minic_machine_builder_add_successor(&sel->builder, done) &&
            minic_machine_builder_set_fallthrough(&sel->builder, done) &&
            minic_machine_builder_position_at(&sel->builder, done);
    }
}

static bool a64_record_transfer(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicLP64FramePlan *frame = sel->ctx->frame_plan;
    MinicCoreValueId source, destination;
    size_t bytes, dest_offset;
    bool source_volatile, destination_volatile;
    if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD) {
        if (!minic_multi_direct_record_load_extent(fn, ins, frame,
                &source, &dest_offset, &bytes) ||
            !a64_load_core(sel, source, A64_T0, 64U) ||
            !minic_a64_machine_emit_frame_address(&sel->builder, &sel->view,
                minic_a64_owned_slot(dest_offset), A64_T1,
                (MinicA64ScratchPolicy){0U, 0U})) return false;
        source_volatile = ins->value.record_load.is_volatile;
        destination_volatile = false;
    } else if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_COPY) {
        if (!minic_multi_direct_record_copy_extent(fn, ins, &bytes))
            return false;
        source = ins->value.record_copy.source_address;
        destination = ins->value.record_copy.destination_address;
        if (!a64_load_core(sel, source, A64_T0, 64U) ||
            !a64_load_core(sel, destination, A64_T1, 64U)) return false;
        source_volatile = ins->value.record_copy.source_volatile;
        destination_volatile =
            ins->value.record_copy.destination_volatile;
    } else return false;
    return a64_copy_bytes(sel, bytes, source_volatile,
        destination_volatile);
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
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return a64_parameter(sel, ins);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return a64_scalar_bitcast(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        return a64_call_frame_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
        return a64_fixed_sp_read(sel, ins);
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return a64_opaque_hint(sel, ins);
    case MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_LOAD:
    case MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_STORE:
    case MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR:
        return a64_wide_memory(sel, ins);
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return a64_record_transfer(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL:
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return a64_call(sel, ins);
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return a64_floating(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        return a64_value_width(fn, ins->core_type_id, &width, &sign, &ptr) &&
            a64_load_constant_width(sel, A64_T0,
                (uint64_t)ins->value.integer_value,
                width == 8U ? 64U : 32U) &&
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
    case MINIC_CORE_INSTRUCTION_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE: {
        const MinicCoreValueId stored = ins->kind ==
            MINIC_CORE_INSTRUCTION_STORE ? ins->value.store.stored_value :
            MINIC_CORE_VALUE_INVALID;
        const MinicCoreTypeId value_type = stored < fn->value_count ?
            fn->values[stored].core_type_id : MINIC_CORE_TYPE_INVALID;
        if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
            (a64_wide_integer(fn, ins->core_type_id) ||
             a64_wide_float(fn, ins->core_type_id)))
            return a64_wide_memory(sel, ins);
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            (a64_wide_integer(fn, value_type) ||
             a64_wide_float(fn, value_type)))
            return a64_wide_memory(sel, ins);
        const bool load = ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
            ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
        const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD ||
            ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
        const MinicCoreValueId address = atomic ?
            (load ? ins->value.atomic_load.address :
                ins->value.atomic_store.address) :
            (load ? ins->value.load.address : ins->value.store.address);
        const MinicCoreValueId source = load ? ins->result :
            (atomic ? ins->value.atomic_store.stored_value :
                ins->value.store.stored_value);
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
        vol = !atomic && (load ? ins->value.load.is_volatile :
            ins->value.store.is_volatile);
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
        MinicCoreAbiValueShape shape;
        if (!a64_scalar_abi(fn, ret_type, &shape)) return false;
        if (shape.kind == MINIC_CORE_TYPE_FLOAT) {
            if (term->return_value >= fn->value_count ||
                !a64_load_fp(sel, term->return_value, A64_R0,
                    (unsigned int)(shape.bytes * 8U))) return false;
        } else if (shape.kind != MINIC_CORE_TYPE_VOID) {
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

/* Generated ISA features describe the actual instructions selected, not the
 * hardware instructions accepted inside qualified source inline ASM. Frame
 * lowering may emit SIMD save/restore independently of Core FP arithmetic,
 * so derive these bits from every final target opcode contract. */
static bool a64_derive_generated_features(A64CoreSelection *sel) {
    uint64_t required = MINIC_A64_MACHINE_FEATURE_BASE;
    size_t b, i;
    const MinicMachineFunction *fn = sel->builder.function;
    if (fn == NULL || sel->policy.verify.lookup_opcode == NULL) return false;
    for (b = 0U; b < fn->block_count; ++b) {
        const MinicMachineBlock *block = &fn->blocks[b];
        for (i = 0U; i < block->instruction_count; ++i) {
            const MinicMachineInstruction *ins = &block->instructions[i];
            MinicMachineOpcodeContract contract = {0};
            if (ins->kind != MINIC_MACHINE_INSTRUCTION_SELECTED) continue;
            if (!sel->policy.verify.lookup_opcode(sel->ctx->target,
                    ins->opcode, &contract,
                    sel->policy.verify.context)) return false;
            required |= contract.required_generated_features;
        }
    }
    sel->builder.function->generated_features = required;
    return true;
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
            MINIC_A64_MACHINE_FEATURE_BASE))
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
                sel.blocks[i]) ||
            !minic_machine_builder_mark_core_block(&sel.builder, i))
            goto cleanup;
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
    ok = a64_derive_generated_features(&sel) &&
         minic_machine_gnu_asm_preflight(machine, &sel.policy);
cleanup:
    free(sel.blocks);
    free(sel.false_blocks);
    free(sel.true_blocks);
    free(sel.reachable);
    if (!ok) minic_machine_function_destroy(machine);
    return ok;
}
