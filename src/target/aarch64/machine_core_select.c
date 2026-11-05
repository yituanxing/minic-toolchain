#include "target/aarch64/machine_core_select.h"
#include "target/aarch64/machine_frame_select.h"
#include "target/aarch64/machine_opcode.h"
#include "target/aarch64/machine_source_asm.h"
#include "target/aarch64/machine_lse_source_select.h"
#include "target/aarch64/asm_contract_internal.h"
#include "target/aarch64/abi.h"
#include "target/aarch64/neon_emit.h"
#include "target/common/core_block_walk.h"
#include "target/common/core_machine_asm.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/core_extended_float_runtime.h"
#include "target/common/core_abi_value_shape.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/elf_module_symbol_info.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_aggregate.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Value slots are independently allocated by the ABI frame planner. The
 * selector fixes only the physical scratch set, not any virtual register or
 * C type. The frame selector protects live x9..x12 from its x16/x17 scratch. */
enum { A64_R0 = 0U, A64_T0 = 9U, A64_T1 = 10U,
       A64_T2 = 11U, A64_T3 = 12U };

typedef struct A64VariadicBlocks {
    size_t register_path;
    size_t stack_path;
    size_t join;
    size_t nonzero_path;
} A64VariadicBlocks;

typedef struct A64CoreSelection {
    MinicMachineBuilder builder;
    MinicMachineGNUAsmPolicy policy;
    const MinicDirectLP64BlockContext *ctx;
    size_t *blocks;
    size_t *false_blocks;
    size_t *true_blocks;
    bool *reachable;
    size_t *goto_emit;
    size_t *goto_continue;
    A64VariadicBlocks *variadic_blocks;
    MinicA64FrameView view;
} A64CoreSelection;

static bool a64_normalize(A64CoreSelection *, size_t, bool, unsigned int);
static bool a64_symbol_materialize(A64CoreSelection *, const char *,
    MinicElfModuleSymbolKind);
static bool a64_branch(A64CoreSelection *, size_t);
static bool a64_memory_register(A64CoreSelection *, uint32_t, bool,
    unsigned int, unsigned int, unsigned int, size_t, bool, bool);
static bool a64_variadic(A64CoreSelection *, const MinicCoreInstruction *,
    MinicCoreInstructionId);
static bool a64_wide_shift(A64CoreSelection *, const MinicCoreInstruction *,
    MinicCoreInstructionId);

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

static MinicMachineOperand a64_block_address(size_t id) {
    MinicMachineOperand op = {0};
    op.kind = MINIC_MACHINE_OPERAND_BLOCK_ADDRESS;
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

static bool a64_widen_mul(A64CoreSelection *sel, bool sign,
    unsigned int dst, unsigned int left, unsigned int right) {
    const MinicMachineOperand ops[] = {
        a64_gp(dst, 64U, MINIC_MACHINE_REGISTER_WRITE),
        a64_gp(left, 32U, MINIC_MACHINE_REGISTER_READ),
        a64_gp(right, 32U, MINIC_MACHINE_REGISTER_READ)
    };
    return a64_emit(sel, sign ? MINIC_A64_MACHINE_SMULL :
        MINIC_A64_MACHINE_UMULL, ops, 3U);
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

static bool a64_madd(A64CoreSelection *sel, unsigned int dst,
    unsigned int multiplicand, unsigned int multiplier,
    unsigned int accumulator) {
    const MinicMachineOperand ops[] = {
        a64_gp(dst, 64U, MINIC_MACHINE_REGISTER_WRITE),
        a64_gp(multiplicand, 64U, MINIC_MACHINE_REGISTER_READ),
        a64_gp(multiplier, 64U, MINIC_MACHINE_REGISTER_READ),
        a64_gp(accumulator, 64U, MINIC_MACHINE_REGISTER_READ)
    };
    return a64_emit(sel, MINIC_A64_MACHINE_MADD, ops, 4U);
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

/* Existing AAPCS64 named i128/binary128 parameter locations are already
 * selected by Common.  Keep their complete 16-byte payload in the planned
 * wide SSA slot; no implicit narrowing to a scalar GP/FP register. */
static bool a64_wide_parameter(A64CoreSelection *sel,
    const MinicCoreInstruction *ins,
    const MinicLP64ArgumentLocation *location) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const bool integer = a64_wide_integer(fn, ins->core_type_id);
    const bool floating = a64_wide_float(fn, ins->core_type_id);
    size_t offset, limb;
    if ((!integer && !floating) ||
        location->kind != MINIC_LP64_ARGUMENT_WIDE ||
        !a64_wide_core_slot(sel, ins->result, &offset)) return false;
    if (integer) {
        if ((location->gp_registers == 2U) ==
            (location->stack_slots == 2U) ||
            location->fp_registers != 0U ||
            (location->gp_registers == 2U && location->stack_slots != 0U) ||
            (location->stack_slots == 2U && location->gp_registers != 0U) ||
            (location->gp_registers == 2U &&
             (location->gp_first > 6U || (location->gp_first & 1U) != 0U)) ||
            (location->stack_slots == 2U &&
             location->stack_first > SIZE_MAX / 8U - 1U)) return false;
        for (limb = 0U; limb < 2U; ++limb) {
            const unsigned int reg = location->gp_registers == 2U ?
                (unsigned int)(location->gp_first + limb) : A64_T0;
            if (location->stack_slots == 2U &&
                !minic_a64_machine_emit_frame_load(&sel->builder,
                    &sel->view,
                    (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING,
                        (location->stack_first + limb) * 8U},
                    (MinicA64FrameRegister){MINIC_A64_REG_X, A64_T0},
                    (MinicA64ScratchPolicy){0U, 0U})) return false;
            if (!a64_store_wide_half(sel, offset + limb * 8U, reg))
                return false;
        }
        return true;
    }
    if ((location->fp_registers == 1U) ==
        (location->stack_slots == 2U) ||
        location->gp_registers != 0U ||
        (location->fp_registers == 1U && location->stack_slots != 0U) ||
        (location->stack_slots == 2U && location->fp_registers != 0U) ||
        (location->fp_registers == 1U && location->fp_first >= 8U) ||
        (location->stack_slots == 2U &&
         location->stack_first > SIZE_MAX / 8U - 1U)) return false;
    {
        const unsigned int reg = location->fp_registers == 1U ?
            (unsigned int)location->fp_first : 9U;
        if (location->stack_slots == 2U &&
            !minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
                (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING,
                    location->stack_first * 8U},
                (MinicA64FrameRegister){MINIC_A64_REG_Q, reg},
                (MinicA64ScratchPolicy){0U, 0U})) return false;
        return minic_a64_machine_emit_frame_store(&sel->builder, &sel->view,
            minic_a64_owned_slot(offset),
            (MinicA64FrameRegister){MINIC_A64_REG_Q, reg},
            (MinicA64ScratchPolicy){0U, 0U});
    }
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
            sel->ctx->incoming_plan, &location)) return false;
    if (a64_wide_integer(fn, ins->core_type_id) ||
        a64_wide_float(fn, ins->core_type_id))
        return a64_wide_parameter(sel, ins, location);
    if (!a64_scalar_abi(fn, ins->core_type_id, &shape) ||
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

/* Calls consume sealed Common argument locations.  These routines perform
 * only the native AAPCS64 transport previously owned by call_emit.c. */
static bool a64_call_wide_argument(A64CoreSelection *sel,
    MinicCoreValueId value, const MinicLP64ArgumentLocation *location,
    const MinicA64FrameView *live) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreTypeId type_id = fn->values[value].core_type_id;
    const bool integer = a64_wide_integer(fn, type_id);
    const bool floating = a64_wide_float(fn, type_id);
    size_t at, limb;
    if ((!integer && !floating) ||
        location->kind != MINIC_LP64_ARGUMENT_WIDE ||
        !a64_wide_core_slot(sel, value, &at)) return false;
    if (integer) {
        if ((location->gp_registers == 2U) ==
                (location->stack_slots == 2U) ||
            location->fp_registers != 0U ||
            (location->gp_registers == 2U &&
             (location->gp_first > 6U ||
              (location->gp_first & 1U) != 0U ||
              location->stack_slots != 0U)) ||
            (location->stack_slots == 2U &&
             (location->gp_registers != 0U ||
              location->stack_first > SIZE_MAX / 8U - 1U))) return false;
        for (limb = 0U; limb < 2U; ++limb) {
            const unsigned int reg = location->gp_registers == 2U ?
                (unsigned int)(location->gp_first + limb) : A64_T0;
            if (!minic_a64_machine_emit_frame_load(&sel->builder, live,
                    minic_a64_owned_slot(at + limb * 8U),
                    (MinicA64FrameRegister){MINIC_A64_REG_X, reg},
                    (MinicA64ScratchPolicy){0U, 0U})) return false;
            if (location->stack_slots == 2U &&
                !minic_a64_machine_emit_frame_store(&sel->builder, live,
                    (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING,
                        (location->stack_first + limb) * 8U},
                    (MinicA64FrameRegister){MINIC_A64_REG_X, reg},
                    (MinicA64ScratchPolicy){0U, 0U})) return false;
        }
        return true;
    }
    if ((location->fp_registers == 1U) ==
            (location->stack_slots == 2U) ||
        location->gp_registers != 0U ||
        (location->fp_registers == 1U &&
         (location->fp_first >= 8U || location->stack_slots != 0U)) ||
        (location->stack_slots == 2U &&
         (location->fp_registers != 0U ||
          location->stack_first > SIZE_MAX / 8U - 1U))) return false;
    {
        const unsigned int reg = location->fp_registers == 1U ?
            (unsigned int)location->fp_first : 9U;
        if (!minic_a64_machine_emit_frame_load(&sel->builder, live,
                minic_a64_owned_slot(at),
                (MinicA64FrameRegister){MINIC_A64_REG_Q, reg},
                (MinicA64ScratchPolicy){0U, 0U})) return false;
        return location->fp_registers == 1U ||
            minic_a64_machine_emit_frame_store(&sel->builder, live,
                (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING,
                    location->stack_first * 8U},
                (MinicA64FrameRegister){MINIC_A64_REG_Q, reg},
                (MinicA64ScratchPolicy){0U, 0U});
    }
}

/* Assemble one exactly bounded aggregate eightbyte from the source object.
 * x9 is byte scratch, x10 address, x11 accumulated GP word.  The destination
 * x0..x7/stack is written only after the entire chunk has been assembled. */
static bool a64_pack_record_chunk(A64CoreSelection *sel,
    const MinicA64FrameView *live, size_t offset, size_t payload,
    unsigned int destination, size_t stack_offset, bool in_register) {
    size_t byte;
    if (payload == 0U || payload > 8U ||
        (in_register && destination >= 8U) ||
        !minic_a64_machine_emit_frame_address(&sel->builder, live,
            minic_a64_owned_slot(offset), A64_T1,
            (MinicA64ScratchPolicy){0U, 0U}) ||
        !a64_load_constant(sel, A64_T2, 0U)) return false;
    for (byte = 0U; byte < payload; ++byte) {
        if (!a64_memory_register(sel, MINIC_A64_MACHINE_LDRB, false,
                A64_T0, 32U, A64_T1, byte, false, false) ||
            (byte != 0U &&
             !a64_rri(sel, MINIC_A64_MACHINE_LSL_IMM, 64U,
                 A64_T0, A64_T0, (int64_t)(byte * 8U))) ||
            !a64_rrr(sel, MINIC_A64_MACHINE_ORR, 64U,
                A64_T2, A64_T2, A64_T0)) return false;
    }
    if (in_register)
        return a64_rr(sel, MINIC_A64_MACHINE_MOV, 64U,
            destination, A64_T2);
    return minic_a64_machine_emit_frame_store(&sel->builder, live,
        (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING, stack_offset},
        (MinicA64FrameRegister){MINIC_A64_REG_X, A64_T2},
        (MinicA64ScratchPolicy){0U, 0U});
}

/* Move an ABI GP chunk into private object storage, byte-precisely.  This
 * is the reverse of a64_pack_record_chunk, reused for call results. */
static bool a64_unpack_record_chunk(A64CoreSelection *sel,
    size_t offset, size_t payload, unsigned int source) {
    size_t byte;
    if (payload == 0U || payload > 8U || source >= 8U ||
        !minic_a64_machine_emit_frame_address(&sel->builder, &sel->view,
            minic_a64_owned_slot(offset), A64_T1,
            (MinicA64ScratchPolicy){0U, 0U}) ||
        !a64_rr(sel, MINIC_A64_MACHINE_MOV, 64U, A64_T0, source))
        return false;
    for (byte = 0U; byte < payload; ++byte)
        if (!a64_memory_register(sel, MINIC_A64_MACHINE_STRB, false,
                A64_T0, 32U, A64_T1, byte, true, false) ||
            (byte + 1U < payload &&
             !a64_rri(sel, MINIC_A64_MACHINE_LSR_IMM,
                 64U, A64_T0, A64_T0, 8))) return false;
    return true;
}

static bool a64_call_record_argument(A64CoreSelection *sel,
    MinicCoreObjectId id, const MinicLP64ArgumentLocation *location,
    const MinicA64FrameView *live) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreTypeId type = id < fn->object_count && fn->objects != NULL ?
        fn->objects[id].core_type_id : MINIC_CORE_TYPE_INVALID;
    size_t at, bytes = 0U, chunks = 0U, chunk;
    if (id >= fn->object_count || fn->objects == NULL ||
        fn->type_context == NULL) return false;
    if (location->kind == MINIC_LP64_ARGUMENT_EMPTY_RECORD)
        return location->size_bytes == 0U && location->chunks == 0U &&
            location->gp_registers == 0U &&
            location->fp_registers == 0U &&
            location->stack_slots == 0U &&
            minic_multi_empty_record_core(fn->type_context, type);
    if (!minic_lp64_frame_plan_object(sel->ctx->frame_plan, id, &at))
        return false;
    if (location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
        if (!minic_aarch64_indirect_record_core(fn->type_context,
                type, &bytes) || location->size_bytes != bytes ||
            location->fp_registers != 0U ||
            ((location->gp_registers == 1U) ==
             (location->stack_slots == 1U)) ||
            (location->gp_registers == 1U &&
             (location->gp_first >= 8U || location->stack_slots != 0U)) ||
            (location->stack_slots == 1U &&
             (location->gp_registers != 0U ||
              location->stack_first > SIZE_MAX / 8U))) return false;
        if (!minic_a64_machine_emit_frame_address(&sel->builder, live,
                minic_a64_owned_slot(at), A64_T0,
                (MinicA64ScratchPolicy){0U, 0U})) return false;
        if (location->gp_registers == 1U)
            return a64_rr(sel, MINIC_A64_MACHINE_MOV, 64U,
                (unsigned int)location->gp_first, A64_T0);
        return minic_a64_machine_emit_frame_store(&sel->builder, live,
            (MinicA64FrameRef){MINIC_A64_FRAME_OUTGOING,
                location->stack_first * 8U},
            (MinicA64FrameRegister){MINIC_A64_REG_X, A64_T0},
            (MinicA64ScratchPolicy){0U, 0U});
    }
    if (location->kind != MINIC_LP64_ARGUMENT_RECORD ||
        !minic_multi_gp_record_core(fn->type_context, type,
            &bytes, &chunks) ||
        location->size_bytes != bytes || location->chunks != chunks ||
        location->fp_registers != 0U ||
        !((location->gp_registers == chunks &&
           location->stack_slots == 0U) ||
          (location->gp_registers == 0U &&
           location->stack_slots == chunks)) ||
        (location->gp_registers != 0U &&
         (location->gp_first >= 8U ||
          chunks > 8U - location->gp_first)) ||
        (location->stack_slots != 0U &&
         location->stack_first > SIZE_MAX / 8U - chunks)) return false;
    for (chunk = 0U; chunk < chunks; ++chunk) {
        const size_t payload = bytes - chunk * 8U >= 8U ?
            8U : bytes - chunk * 8U;
        if (!a64_pack_record_chunk(sel, live, at + chunk * 8U, payload,
                location->gp_registers != 0U ?
                    (unsigned int)(location->gp_first + chunk) : 0U,
                location->stack_slots != 0U ?
                    (location->stack_first + chunk) * 8U : 0U,
                location->gp_registers != 0U)) return false;
    }
    return true;
}

static bool a64_call_record_result(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, MinicCoreObjectId id) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreTypeId type = ins->core_type_id;
    size_t at, bytes, chunks, fp_count, fp_width, i;
    if (fn->type_context == NULL || id >= fn->object_count ||
        fn->objects == NULL ||
        minic_core_type_unqualified(fn->type_context, type) !=
        minic_core_type_unqualified(fn->type_context,
            fn->objects[id].core_type_id)) return false;
    if (minic_multi_empty_record_core(fn->type_context, type)) return true;
    if (!minic_lp64_frame_plan_object(sel->ctx->frame_plan, id, &at))
        return false;
    if (minic_aarch64_indirect_record_core(fn->type_context,
            type, &bytes)) return bytes > 16U;
    if (minic_multi_fp_return_record_core(fn->type_context,
            type, &fp_count, &fp_width)) {
        for (i = 0U; i < fp_count; ++i)
            if (!minic_a64_machine_emit_frame_store(&sel->builder,
                    &sel->view,
                    minic_a64_owned_slot(at + i * fp_width),
                    (MinicA64FrameRegister){fp_width == 4U ?
                        MINIC_A64_REG_S : MINIC_A64_REG_D,
                        (unsigned int)i},
                    (MinicA64ScratchPolicy){0U, 0U})) return false;
        return true;
    }
    if (!minic_multi_gp_record_core(fn->type_context, type,
            &bytes, &chunks)) return false;
    for (i = 0U; i < chunks; ++i) {
        const size_t payload = bytes - i * 8U >= 8U ?
            8U : bytes - i * 8U;
        if (!a64_unpack_record_chunk(sel, at + i * 8U,
                payload, (unsigned int)i)) return false;
    }
    return true;
}

static bool a64_call(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    const MinicCoreExtendedFloatOperation extended = indirect ?
        MINIC_CORE_EXT_FLOAT_NONE : ins->value.call.extended_float_operation;
    const size_t begin = indirect ? ins->value.indirect_call.argument_begin :
        ins->value.call.argument_begin;
    const size_t count = indirect ? ins->value.indirect_call.argument_count :
        ins->value.call.argument_count;
    const MinicCoreObjectId result_object = indirect ?
        ins->value.indirect_call.result_object : ins->value.call.result_object;
    const MinicLP64PreparedCall *planned;
    MinicCoreAbiValueShape result;
    MinicA64FrameView live = sel->view;
    size_t call_id, i;
    if (fn->instructions == NULL || ins < fn->instructions ||
        ins >= fn->instructions + fn->instruction_count ||
        sel->ctx->outgoing_calls == NULL ||
        !minic_core_abi_value_shape(fn, ins->core_type_id, &result) ||
        (result.kind != MINIC_CORE_TYPE_RECORD &&
         !a64_wide_integer(fn, ins->core_type_id) &&
         !a64_wide_float(fn, ins->core_type_id) &&
         !a64_scalar_abi(fn, ins->core_type_id, &result)) ||
        begin > fn->call_argument_count ||
        count > fn->call_argument_count - begin) return false;
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
        if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            if (!a64_call_record_argument(sel, arg->value.object_id,
                    location, &live)) return false;
            continue;
        }
        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            arg->value.value_id >= fn->value_count) return false;
        if (a64_wide_integer(fn,
                fn->values[arg->value.value_id].core_type_id) ||
            a64_wide_float(fn,
                fn->values[arg->value.value_id].core_type_id)) {
            if (!a64_call_wide_argument(sel, arg->value.value_id,
                    location, &live)) return false;
            continue;
        }
        if (
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
    if (result.kind == MINIC_CORE_TYPE_RECORD) {
        size_t bytes;
        /* AAPCS64 x8 is independent of x0..x7.  Set it AFTER explicit
         * parameters, before the branch-and-link; nested callees write
         * directly to the caller's planned result object. */
        if (minic_aarch64_indirect_record_core(fn->type_context,
                ins->core_type_id, &bytes)) {
            size_t at;
            if (bytes <= 16U || result_object >= fn->object_count ||
                !minic_lp64_frame_plan_object(sel->ctx->frame_plan,
                    result_object, &at) ||
                !minic_a64_machine_emit_frame_address(&sel->builder,
                    &live, minic_a64_owned_slot(at), 8U,
                    (MinicA64ScratchPolicy){0U, 0U})) return false;
        }
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
            (name = extended == MINIC_CORE_EXT_FLOAT_NONE ?
                fn->callees[ins->value.call.callee_id].name :
                minic_core_extended_float_runtime_symbol(extended)) == NULL)
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
    /* GNU TFmode helpers return ordered integer comparison statuses, not
     * the Core 0/1 predicate. Keep the typed Core operation separate from
     * an ordinary user-defined symbol named __eqtf2/__lttf2/__letf2. */
    if (extended >= MINIC_CORE_EXT_FLOAT_EQUAL &&
        extended <= MINIC_CORE_EXT_FLOAT_LESS_EQUAL &&
        (!a64_cmp_zero(sel, 32U, 0U) ||
         !a64_cset(sel, 0U,
             extended == MINIC_CORE_EXT_FLOAT_EQUAL ? 0 :
             extended == MINIC_CORE_EXT_FLOAT_LESS ? 11 : 13)))
        return false;
    if (result.kind == MINIC_CORE_TYPE_VOID) return true;
    if (result.kind == MINIC_CORE_TYPE_RECORD)
        return a64_call_record_result(sel, ins, result_object);
    if (a64_wide_integer(fn, ins->core_type_id)) {
        size_t at;
        return a64_wide_core_slot(sel, ins->result, &at) &&
            a64_store_wide_half(sel, at, 0U) &&
            a64_store_wide_half(sel, at + 8U, 1U);
    }
    if (a64_wide_float(fn, ins->core_type_id)) {
        size_t at;
        return a64_wide_core_slot(sel, ins->result, &at) &&
            minic_a64_machine_emit_frame_store(&sel->builder, &sel->view,
                minic_a64_owned_slot(at),
                (MinicA64FrameRegister){MINIC_A64_REG_Q, 0U},
                (MinicA64ScratchPolicy){0U, 0U});
    }
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

/* Dynamic alloca is a Core memory-lifetime effect, never a source-ASM
 * instruction.  The overflow trap and normal continuation are real auxiliary
 * MachineCFG blocks.  Frame accesses remain anchored to x19, the immutable
 * base established by the AAPCS64 machine prologue. */
static bool a64_dynamic_stack_alloc(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    size_t overflow, continuation;
    const size_t source_block = sel->builder.current_block;
    const MinicMachineOperand high[] = {
        a64_immediate(2), a64_block(0U)
    };
    MinicMachineOperand branch[2] = {high[0], high[1]};
    if (!minic_multi_direct_dynamic_stack_alloc_shape(sel->ctx->fn, ins,
            sel->ctx->machine_frame->has_dynamic_stack) ||
        !a64_load_core(sel, ins->value.operand, A64_T0, 64U) ||
        !a64_rri(sel, MINIC_A64_MACHINE_ADDS_IMM, 64U,
            A64_T1, A64_T0, 15) ||
        !minic_machine_builder_create_block(&sel->builder, NULL,
            false, true, &continuation) ||
        !minic_machine_builder_create_block(&sel->builder, NULL,
            false, true, &overflow) ||
        !minic_machine_builder_position_at(&sel->builder, source_block))
        return false;
    branch[1] = a64_block(overflow);
    if (!a64_emit(sel, MINIC_A64_MACHINE_B_COND, branch, 2U) ||
        !minic_machine_builder_add_successor(&sel->builder, overflow) ||
        !minic_machine_builder_add_successor(&sel->builder, continuation) ||
        !minic_machine_builder_set_fallthrough(&sel->builder, continuation) ||
        !minic_machine_builder_position_at(&sel->builder, overflow) ||
        !a64_emit(sel, MINIC_A64_MACHINE_BRK, NULL, 0U) ||
        !minic_machine_builder_position_at(&sel->builder, continuation) ||
        !a64_rri(sel, MINIC_A64_MACHINE_AND_ALIGN_IMM, 64U,
            A64_T1, A64_T1, -16) ||
        !a64_rrr(sel, MINIC_A64_MACHINE_SUB, 64U,
            MINIC_A64_MACHINE_SP, MINIC_A64_MACHINE_SP, A64_T1) ||
        !a64_rri(sel, MINIC_A64_MACHINE_ADD_IMM, 64U,
            A64_T0, MINIC_A64_MACHINE_SP, 0)) return false;
    return a64_store_core(sel, ins->result, A64_T0, 64U);
}

/* One whole-family integer cast, including i128 and C _Bool, delegates
 * source semantic classification to the shared Core conversion plan.  Its
 * only target decisions are register width, exact sign-extension and which
 * 64-bit halves are moved between already-allocated frame slots. */
static bool a64_integer_conversion(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    MinicDirectIntegerCastPlan cast;
    if (!minic_multi_direct_integer_cast_plan(sel->ctx->module,
            sel->ctx->target, sel->ctx->fn, ins,
            sel->ctx->frame_plan, &cast)) return false;
    if (cast.source_wide) {
        if (!a64_load_wide_half(sel, cast.source_wide_offset, A64_T0) ||
            !a64_load_wide_half(sel,
                cast.source_wide_offset + 8U, A64_T1)) return false;
    } else {
        const unsigned int bits = cast.source_width == 8U ? 64U : 32U;
        if (!a64_load_core(sel, cast.operand, A64_T0, bits) ||
            (cast.source_width < 4U &&
             !a64_normalize(sel, cast.source_width,
                 cast.source_signed, A64_T0))) return false;
        if (cast.target_wide) {
            if (cast.source_width != 8U && cast.source_signed &&
                !a64_extend_word(sel, A64_T0, A64_T0, true)) return false;
            if (cast.source_signed) {
                if (!a64_rri(sel, MINIC_A64_MACHINE_ASR_IMM, 64U,
                        A64_T1, A64_T0, 63)) return false;
            } else if (!a64_load_constant(sel, A64_T1, 0U)) return false;
        }
    }
    if (cast.target_wide)
        return a64_store_wide_half(sel, cast.target_wide_offset, A64_T0) &&
            a64_store_wide_half(sel, cast.target_wide_offset + 8U, A64_T1);
    if (cast.target_boolean) {
        if (cast.source_wide &&
            !a64_rrr(sel, MINIC_A64_MACHINE_ORR, 64U,
                A64_T0, A64_T0, A64_T1)) return false;
        if (!a64_cmp_zero(sel,
                cast.source_wide || cast.source_width == 8U ? 64U : 32U,
                A64_T0) || !a64_cset(sel, A64_T0, 1)) return false;
    } else if (cast.target_width == 8U && !cast.source_wide &&
        cast.source_width < 8U && cast.source_signed) {
        if (!a64_extend_word(sel, A64_T0, A64_T0, true)) return false;
    } else if (cast.target_width < 8U &&
        !a64_normalize(sel, cast.target_width,
            cast.target_signed, A64_T0)) return false;
    return a64_store_core(sel, ins->result, A64_T0,
        cast.target_width == 8U ? 64U : 32U);
}


/* Linux static-key/capability asm-goto stays an exact source-owned section
 * record, with a typed immediate, an actual C-label edge, and fallthrough.
 * Place it in its own terminal Machine block so the original Core block can
 * continue through an adjacent physical successor after the branch island. */
static bool a64_linux_source_asm_goto(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, MinicCoreInstructionId ins_id,
    const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreInlineAsmImmediate *immediate;
    const bool jump = minic_aarch64_linux_jump_label_asm_goto_core(source);
    const bool cap_unlikely = aarch64_linux_cap_goto_text(
        source->template_text, source->template_length, true, false);
    const bool cap_likely = aarch64_linux_cap_goto_text(
        source->template_text, source->template_length, true, true);
    const bool cap_baked_unlikely = aarch64_linux_cap_goto_text(
        source->template_text, source->template_length, false, false);
    const bool cap_baked_likely = aarch64_linux_cap_goto_text(
        source->template_text, source->template_length, false, true);
    const bool cap_baked = cap_baked_unlikely || cap_baked_likely;
    const bool cap_deferred = cap_baked &&
        source->deferred_immediate_symbol != NULL;
    const bool cap = cap_unlikely || cap_likely || cap_baked;
    const char *label_name = (cap_likely || cap_baked_likely) ?
        "l_no" : "l_yes";
    MinicMachineOperand bindings[2] = {{0}};
    size_t emit, after, target, binding_count;
    if (ins->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        !source->is_goto || !source->is_volatile || !(jump || cap) ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_NONE ||
        source->target_machine_form_id != 0U ||
        source->target_machine_form_variant != 0U ||
        source->has_memory_clobber || source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        source->symbol_reference_count != 0U ||
        (!cap_baked && source->deferred_immediate_symbol != NULL) ||
        source->resolved_immediate_count != (cap_baked ? 0U : 1U) ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->template_text == NULL ||
        ins_id >= fn->instruction_count ||
        source->goto_target >= fn->block_count ||
        !sel->reachable[source->goto_target] ||
        sel->blocks[source->goto_target] == SIZE_MAX ||
        (cap_baked ? source->goto_label_names[0] != NULL :
            (source->goto_label_names[0] == NULL ||
             strcmp(source->goto_label_names[0], label_name) != 0)))
        return false;
    emit = sel->goto_emit[ins_id];
    after = sel->goto_continue[ins_id];
    if (emit == SIZE_MAX || after != emit + 1U) return false;
    target = sel->blocks[source->goto_target];
    immediate = cap_baked ? NULL : &source->resolved_immediates[0];
    if (cap_baked) {
        /* The generic one-label Core seam either bakes a proven decimal
         * feature or retains the exact %0 plus its deferred-symbol witness.
         * The latter is discovery-only and intentionally cannot link. */
        if (cap_deferred !=
            (strstr(source->template_text, "%0") != NULL)) return false;
        if (cap_deferred) {
            bindings[0] = a64_symbol(source->deferred_immediate_symbol,
                MINIC_A64_RELOC_SOURCE_IMMEDIATE);
            bindings[0].asm_constraint = "i";
            bindings[1] = a64_block(target);
            bindings[1].asm_name = (char *)label_name;
            binding_count = 2U;
        } else {
            bindings[0] = a64_block(target);
            bindings[0].asm_name = (char *)label_name;
            binding_count = 1U;
        }
    } else if (cap) {
        if (immediate->kind != MINIC_CORE_INLINE_ASM_IMMEDIATE_INTEGER ||
            immediate->integer < 0 || immediate->integer >= (1 << 15) ||
            immediate->symbol != NULL || immediate->byte_addend != 0 ||
            immediate->asm_name == NULL ||
            strcmp(immediate->asm_name, "cpucap") != 0)
            return false;
        bindings[0] = a64_immediate(immediate->integer);
        bindings[0].asm_name = immediate->asm_name;
    } else {
        if (!minic_core_inline_asm_relocation_valid(immediate) ||
            (immediate->byte_addend != 0 &&
             immediate->byte_addend != 1))
            return false;
        bindings[0] = a64_symbol(immediate->symbol,
            MINIC_A64_RELOC_SOURCE_IMMEDIATE);
        bindings[0].value.symbol.addend = immediate->byte_addend;
        bindings[0].asm_name = immediate->asm_name;
    }
    if (!cap_baked) {
        bindings[0].asm_constraint = "i";
        bindings[1] = a64_block(target);
        bindings[1].asm_name = source->goto_label_names[0];
        binding_count = 2U;
    }
    if (!a64_branch(sel, emit) ||
        !minic_machine_builder_position_at(&sel->builder, emit) ||
        !minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, MINIC_MACHINE_EFFECT_BRANCH, bindings, binding_count,
            NULL, 0U, true, true) ||
        !minic_machine_builder_add_successor(&sel->builder, target) ||
        !minic_machine_builder_add_successor(&sel->builder, after) ||
        !minic_machine_builder_set_fallthrough(&sel->builder, after) ||
        !minic_machine_builder_position_at(&sel->builder, after))
        return false;
    return true;
}


/* The original no-operand PAN/TCO alternative is a complete, fixed Linux
 * section+instruction record. Source qualification carries the implicit
 * privileged memory effect; retain it without turning arbitrary .inst text
 * into an opaque machine emission fallback. */
static bool a64_linux_pstate_alternative_zero(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const Aarch64LinuxPstateAlternative form =
        aarch64_linux_pstate_alternative(
            source->template_text, source->template_length);
    if (!form.cap || !source->is_volatile || source->is_goto ||
        source->target_machine_form_id !=
            AARCH64_MACHINE_ASM_PSTATE_ALTERNATIVE ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != 0U ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID)) return false;
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source, 0U, NULL, 0U, NULL, 0U, false, false);
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

static bool a64_ordered_source_memory(A64CoreSelection *sel, uint32_t opcode,
    unsigned int reg, unsigned int bits, unsigned int base, bool store,
    const MinicCoreInlineAsm *source) {
    MinicMachineBlock *block;
    if (!a64_memory_register(sel, opcode, false, reg, bits, base, 0U,
            store, true)) return false;
    block = &sel->builder.function->blocks[sel->builder.current_block];
    if (block->instruction_count == 0U) return false;
    block->instructions[block->instruction_count - 1U].effects |=
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    return minic_machine_builder_seal_selected_source_asm(&sel->builder,
        source->target_machine_form_id,
        source->target_machine_form_variant, source->source_asm_unique_id);
}

/* Original Linux smp_load_acquire/smp_store_release are sealed two-role GNU
 * forms. Keep C operand admission upstream; recheck the native Core pointee
 * width and emit actual acquire/release opcodes with typed address operands.
 * A mere compiler barrier would lose the architectural ordering guarantee. */
static bool a64_linux_acquire_release(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    static const uint32_t acquire[] = {
        MINIC_A64_MACHINE_LDARB, MINIC_A64_MACHINE_LDARH,
        MINIC_A64_MACHINE_LDAR_W, MINIC_A64_MACHINE_LDAR_X
    };
    static const uint32_t release[] = {
        MINIC_A64_MACHINE_STLRB, MINIC_A64_MACHINE_STLRH,
        MINIC_A64_MACHINE_STLR_W, MINIC_A64_MACHINE_STLR_X
    };
    static const uint32_t stores[] = {
        MINIC_A64_MACHINE_STRB, MINIC_A64_MACHINE_STRH,
        MINIC_A64_MACHINE_STR_W, MINIC_A64_MACHINE_STR_X
    };
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    const bool is_load = source->target_machine_form_id ==
        AARCH64_MACHINE_ASM_LDAR;
    const size_t width = source->target_machine_form_variant;
    const size_t index = width == 1U ? 0U : width == 2U ? 1U :
        width == 4U ? 2U : width == 8U ? 3U : SIZE_MAX;
    const unsigned int bits = width == 8U ? 64U : 32U;
    if (index == SIZE_MAX || source->template_text == NULL ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        (is_load ? aarch64_load_acquire_width(source->template_text,
            source->template_length) :
            aarch64_stlr_width(source->template_text,
                source->template_length)) != width ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ops[0].operand_index != 0U || ops[1].operand_index != 1U ||
        ops[0].value >= fn->value_count ||
        ops[1].value >= fn->value_count ||
        ops[0].has_fixed_register_binding ||
        ops[1].has_fixed_register_binding ||
        ops[0].early_clobber || ops[1].early_clobber ||
        ops[0].physical_early_clobber ||
        ops[1].physical_early_clobber ||
        ops[0].physical_read_write ||
        ops[1].physical_read_write ||
        ops[0].has_tied_output || ops[1].has_tied_output ||
        source->template_length == 0U)
        return false;
    if (is_load) {
        if (ops[0].kind !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
            ops[1].kind !=
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT ||
            !aarch64_stlr_scalar_core(fn, ops[0].value, true, width) ||
            !aarch64_stlr_memory_carrier_core(fn, ops[1].value, width) ||
            !a64_load_core(sel, ops[1].value, A64_T1, 64U) ||
            !a64_ordered_source_memory(sel, acquire[index],
                A64_T0, bits, A64_T1, false, source) ||
            !a64_load_core(sel, ops[0].value, A64_T2, 64U) ||
            !a64_memory_register(sel, stores[index], false,
                A64_T0, bits, A64_T2, 0U, true, true)) return false;
    } else if (ops[0].kind !=
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT ||
        ops[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        !aarch64_stlr_memory_carrier_core(fn, ops[0].value, width) ||
        !aarch64_stlr_scalar_core(fn, ops[1].value, false, width) ||
        !a64_load_core(sel, ops[0].value, A64_T1, 64U) ||
        !a64_load_core(sel, ops[1].value, A64_T0, 64U) ||
        !a64_ordered_source_memory(sel, release[index],
            A64_T0, bits, A64_T1, true, source)) return false;
    return true;
}

/* Linux's sealed 32-bit rbit macro is a normal physical register transform;
 * the output itself is an address-backed Core object, not a SSA result. */
static bool a64_linux_bit_reverse(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    static const char text[] = "rbit %w0, %w1";
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    if (source->target_machine_form_id != AARCH64_MACHINE_ASM_BIT_REVERSE ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->template_text == NULL ||
        source->template_length != sizeof(text) - 1U ||
        memcmp(source->template_text, text, sizeof(text) - 1U) != 0 ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        ops[0].operand_index != 0U || ops[1].operand_index != 1U ||
        ops[0].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT ||
        ops[1].kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        ops[0].has_fixed_register_binding ||
        ops[1].has_fixed_register_binding ||
        ops[0].early_clobber || ops[1].early_clobber ||
        ops[0].physical_early_clobber ||
        ops[1].physical_early_clobber ||
        ops[0].physical_read_write ||
        ops[1].physical_read_write ||
        ops[0].has_tied_output || ops[1].has_tied_output ||
        !aarch64_asm_core_integer(fn, ops[0].value, true, 4U, -1) ||
        !aarch64_asm_core_integer(fn, ops[1].value, false, 4U, -1) ||
        !a64_load_core(sel, ops[1].value, A64_T0, 32U) ||
        !a64_rr(sel, MINIC_A64_MACHINE_RBIT, 32U, A64_T0, A64_T0))
        return false;
    if (!a64_load_core(sel, ops[0].value, A64_T1, 64U) ||
        !a64_memory_register(sel, MINIC_A64_MACHINE_STR_W, false,
            A64_T0, 32U, A64_T1, 0U, true, true)) return false;
    return minic_machine_builder_seal_selected_source_asm(&sel->builder,
        source->target_machine_form_id, source->target_machine_form_variant,
        source->source_asm_unique_id);
}

/* Privileged Linux TLBI stays genuine source ASM, with the original `.arch`
 * directive and GNU `%x0` operand. MachineIR binds the one rZ operand to
 * a verified GP register; it does not reimplement assembler directives. */
static bool a64_linux_tlbi(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const unsigned int form = aarch64_linux_tlbi_form(
        source->template_text, source->template_length);
    const size_t count = form != 0U &&
        form <= AARCH64_LINUX_TLBI_ADDR_FORM_COUNT ? 1U : 0U;
    MinicMachineOperand binding = a64_gp(A64_T0, 64U,
        MINIC_MACHINE_REGISTER_READ);
    if (form == 0U ||
        source->target_machine_form_id != AARCH64_MACHINE_ASM_TLBI ||
        source->target_machine_form_variant != form ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber || source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ins->value.structured_inline_asm.operand_count != count ||
        sel->ctx->machine_frame->has_dynamic_stack) return false;
    if (count != 0U) {
        const MinicCoreStructuredInlineAsmOperand *operand =
            &ins->value.structured_inline_asm.operands[0];
        if (operand->operand_index != 0U ||
            operand->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
            operand->has_fixed_register_binding || operand->early_clobber ||
            operand->physical_early_clobber ||
            operand->physical_read_write || operand->has_tied_output ||
            !minic_core_function_value_is_kind(fn,
                operand->value, MINIC_CORE_TYPE_INTEGER) ||
            !a64_load_core(sel, operand->value, A64_T0, 64U)) return false;
        binding.asm_constraint = "rZ";
    }
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source, 0U, count != 0U ? &binding : NULL, count,
        NULL, 0U, false, false);
}

/* The source-qualified AT, cache-maintenance and PRFM family has exactly
 * one consumed 64-bit value. Keep genuine GNU source text; the target seals
 * its original grammar and effects and assigns only the physical x9 input.
 * The `%a0` PRFM modifier belongs to GNU AArch64 address syntax, not Core. */
static bool a64_linux_one_gp_source(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *operand =
        ins->value.structured_inline_asm.operands;
    const uint32_t form = source->target_machine_form_id;
    const bool cache = form == AARCH64_MACHINE_ASM_CACHE_OP;
    const bool prefetch = form == AARCH64_MACHINE_ASM_PREFETCH;
    const char *cache_op = cache ? aarch64_linux_cache_op_form(
        source->template_text, source->template_length) : NULL;
    const bool efi = cache &&
        aarch64_linux_efi_unclobbered_cache_form(cache_op);
    MinicMachineOperand binding = a64_gp(A64_T0, 64U,
        MINIC_MACHINE_REGISTER_READ);
    if (source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        source->is_goto || source->register_clobber_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        sel->ctx->machine_frame->has_dynamic_stack || operand == NULL ||
        operand->operand_index != 0U ||
        operand->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
        operand->early_clobber || operand->physical_early_clobber ||
        operand->physical_read_write || operand->has_fixed_register_binding ||
        operand->has_tied_output || operand->value >= fn->value_count)
        return false;
    if (form == AARCH64_MACHINE_ASM_AT_S1E1R) {
        if (!source->is_volatile || source->has_memory_clobber ||
            source->implicit_memory_effect ||
            !aarch64_linux_at_s1e1r_template(source->template_text,
                source->template_length) ||
            !aarch64_asm_core_integer(fn, operand->value, false, 8U, 0))
            return false;
    } else if (prefetch) {
        if (!source->is_volatile || source->has_memory_clobber ||
            source->implicit_memory_effect ||
            aarch64_linux_prefetch_template(source->template_text,
                source->template_length) == NULL ||
            !minic_core_function_value_is_kind(fn, operand->value,
                MINIC_CORE_TYPE_POINTER)) return false;
    } else if (cache) {
        if (cache_op == NULL || !source->implicit_memory_effect ||
            !((source->is_volatile && source->has_memory_clobber) ||
              (!source->has_memory_clobber && efi)) ||
            !(aarch64_asm_core_pointer(fn, operand->value, false) ||
              aarch64_asm_core_integer(fn, operand->value, false, 8U, 0)))
            return false;
    } else return false;
    if (!a64_load_core(sel, operand->value, A64_T0, 64U)) return false;
    binding.asm_constraint = prefetch ? "p" : "r";
    return minic_machine_builder_emit_core_source_asm(&sel->builder,
        source, cache ? MINIC_MACHINE_EFFECT_MEMORY_READ |
                        MINIC_MACHINE_EFFECT_MEMORY_WRITE : 0U,
        &binding, 1U, NULL, 0U, false, false);
}

/* Original Linux arm64 io.h raw MMIO is two typed scalar operands, even
 * when the GNU form is a write through an r pointer rather than a Q lvalue.
 * The read alternative must retain both 661/663 instruction bodies and its
 * .altinstructions fixups. The source qualifier and the MachineIR verifier
 * independently match the exact same target-owned width catalog. */
static bool a64_linux_raw_io_source(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    static const uint32_t stores[4] = {
        MINIC_A64_MACHINE_STRB, MINIC_A64_MACHINE_STRH,
        MINIC_A64_MACHINE_STR_W, MINIC_A64_MACHINE_STR_X
    };
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    const Aarch64RawIoRead *read = aarch64_raw_io_read(
        source->template_text, source->template_length);
    const Aarch64RawIoStore *store = aarch64_raw_io_store(
        source->template_text, source->template_length);
    const bool is_read = source->target_machine_form_id ==
        AARCH64_MACHINE_ASM_RAW_IO_READ;
    const size_t width = is_read ? (read ? read->width : 0U) :
        (store ? store->width : 0U);
    const unsigned int bits = width == 8U ? 64U : 32U;
    const size_t index = width == 1U ? 0U : width == 2U ? 1U :
        width == 4U ? 2U : 3U;
    MinicMachineOperand bindings[2];
    size_t i;
    if (width == 0U ||
        (is_read ? store != NULL : read != NULL) ||
        source->target_machine_form_id != (is_read ?
            AARCH64_MACHINE_ASM_RAW_IO_READ :
            AARCH64_MACHINE_ASM_RAW_IO_STORE) ||
        source->target_machine_form_variant != width ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        !source->is_volatile || source->is_goto ||
        source->explicit_memory_clobber ||
        source->has_memory_clobber != !is_read ||
        source->implicit_memory_effect == is_read ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ops == NULL || sel->ctx->machine_frame->has_dynamic_stack ||
        !aarch64_asm_core_integer(fn, ops[0].value, is_read,
            width, -1) ||
        !aarch64_asm_core_pointer(fn, ops[1].value, false)) return false;
    for (i = 0U; i < 2U; ++i) {
        const MinicCoreStructuredInlineAsmOperand *op = &ops[i];
        if (op->operand_index != i || op->value >= fn->value_count ||
            op->has_fixed_register_binding || op->early_clobber ||
            op->physical_early_clobber || op->physical_read_write ||
            op->has_tied_output || op->kind != (is_read && i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
        bindings[i] = a64_gp(A64_T0 + (unsigned int)i,
            i == 0U ? bits : 64U, is_read && i == 0U ?
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
        bindings[i].asm_constraint = i == 0U ?
            (is_read ? "=r" : "rZ") : "r";
    }
    if ((!is_read && !a64_load_core(sel, ops[0].value, A64_T0, bits)) ||
        !a64_load_core(sel, ops[1].value, A64_T1, 64U) ||
        !minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, is_read ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings, 2U, NULL, 0U, false, false)) return false;
    if (!is_read) return true;
    return a64_load_core(sel, ops[0].value, A64_T2, 64U) &&
        a64_memory_register(sel, stores[index], false,
            A64_T0, bits, A64_T2, 0U, true, true);
}

/* The source admission catalog owns these three complete Linux ASM forms;
 * the target owns only physical registers, memory effects and GNU emission. */
static bool a64_linux_extra_source(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    const uint32_t form = source->target_machine_form_id;
    const bool cmpwait = form == AARCH64_MACHINE_ASM_CMPWAIT;
    const bool dep = form == AARCH64_MACHINE_ASM_IO_DEPENDENCY;
    const bool zeropad = form == AARCH64_MACHINE_ASM_ZEROPAD;
    const size_t width = cmpwait ? aarch64_linux_cmpwait_form(
        source->template_text, source->template_length) : 0U;
    const size_t count = dep ? 2U : 3U;
    MinicMachineOperand bindings[3] = {{0}};
    size_t i;
    if (!(cmpwait || dep || zeropad) ||
        (cmpwait && (width != 1U && width != 2U &&
                     width != 4U && width != 8U)) ||
        (dep && !aarch64_linux_io_dependency_form(
            source->template_text, source->template_length)) ||
        (zeropad && !aarch64_linux_zeropad_form(
            source->template_text, source->template_length)) ||
        source->target_machine_form_variant != (cmpwait ? width : 0U) ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->is_volatile != (cmpwait || dep) || source->is_goto ||
        source->has_memory_clobber != dep ||
        source->implicit_memory_effect != zeropad ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        ins->value.structured_inline_asm.operand_count != count ||
        ops == NULL || sel->ctx->machine_frame->has_dynamic_stack ||
        !aarch64_asm_core_integer(fn,ops[0].value,true,8U,-1) ||
        (cmpwait &&
         (!aarch64_asm_core_integer(fn,ops[1].value,true,width,-1) ||
          !aarch64_asm_core_integer(fn,ops[2].value,false,8U,-1))) ||
        (dep && !aarch64_asm_core_integer(fn,ops[1].value,false,8U,-1)) ||
        (zeropad &&
         (!aarch64_asm_core_pointer(fn,ops[1].value,false) ||
          !aarch64_asm_core_integer(fn,ops[2].value,true,8U,-1))))
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *op = &ops[i];
        const MinicCoreStructuredInlineAsmOperandKind kind =
            i == 0U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            (cmpwait && i == 1U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
             zeropad && i == 2U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT);
        if (op->operand_index != i || op->value >= fn->value_count ||
            op->kind != kind || op->has_fixed_register_binding ||
            op->has_tied_output) return false;
        bindings[i] = a64_gp(A64_T0 + (unsigned int)i, 64U,
            i == 0U ? MINIC_MACHINE_REGISTER_WRITE :
                MINIC_MACHINE_REGISTER_READ);
        bindings[i].asm_constraint = i == 0U ?
            (dep ? "=r" : "=&r") :
            (cmpwait && i == 1U ? "+Q" :
             zeropad && i == 2U ? "Q" : "r");
    }
    if ((cmpwait && (!a64_load_core(sel,ops[1].value,A64_T1,64U) ||
                     !a64_load_core(sel,ops[2].value,A64_T2,64U))) ||
        (dep && !a64_load_core(sel,ops[1].value,A64_T1,64U)) ||
        (zeropad && (!a64_load_core(sel,ops[1].value,A64_T1,64U) ||
                     !a64_load_core(sel,ops[2].value,A64_T2,64U))) ||
        !minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, cmpwait ?
                MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE :
                zeropad ? MINIC_MACHINE_EFFECT_MEMORY_READ : 0U,
            bindings, count, NULL, 0U, false, false))
        return false;
    return a64_load_core(sel,ops[0].value,A64_T3,64U) &&
        a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
            A64_T0,64U,A64_T3,0U,true,false);
}

/* The original Linux __get_user/__put_user contract has three independent
 * GNU roles, with an exception-table fixup that names the errno/data GPRs.
 * Resolve those roles to x9/x10/x11 in typed MachineIR; only the C lvalue
 * width determines the later writeback. In particular byte and halfword
 * outputs must never be widened to the full scratch-register width. */
static bool a64_linux_uaccess_source(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    static const uint32_t stores[4] = {
        MINIC_A64_MACHINE_STRB, MINIC_A64_MACHINE_STRH,
        MINIC_A64_MACHINE_STR_W, MINIC_A64_MACHINE_STR_X
    };
    Aarch64UaccessForm form;
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    MinicMachineOperand bindings[3];
    MinicMachineOperand writeback[2];
    unsigned int bits;
    size_t index;
    if (source->target_machine_form_id != AARCH64_MACHINE_ASM_UACCESS ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->template_text == NULL ||
        !aarch64_linux_uaccess_form(source->template_text,
            source->template_length, &form) ||
        source->target_machine_form_variant !=
            (((unsigned int)form.is_load << 16) |
             ((unsigned int)form.width << 8) | form.exception_type) ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber || !source->implicit_memory_effect ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ops == NULL || sel->ctx->machine_frame->has_dynamic_stack ||
        (form.width != 1U && form.width != 2U &&
         form.width != 4U && form.width != 8U) ||
        !aarch64_asm_core_integer(fn, ops[0].value, true, 4U, 1) ||
        !(aarch64_asm_core_integer(fn, ops[1].value, form.is_load,
                form.width, -1) ||
          (form.width == 8U && aarch64_asm_core_pointer(
                fn, ops[1].value, form.is_load))) ||
        !aarch64_asm_core_pointer(fn, ops[2].value, false)) return false;
    bits = form.width == 8U ? 64U : 32U;
    for (index = 0U; index < 3U; ++index) {
        const MinicCoreStructuredInlineAsmOperand *op = &ops[index];
        if (op->operand_index != index || op->value >= fn->value_count ||
            op->has_fixed_register_binding || op->early_clobber ||
            op->physical_early_clobber ||
            op->physical_read_write != (index == 0U) ||
            op->has_tied_output ||
            op->kind != (index == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                index == 1U && form.is_load ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT)) return false;
        bindings[index] = a64_gp(A64_T0 + (unsigned int)index,
            index == 0U || (index == 1U && form.width != 8U) ? 32U : 64U,
            index == 0U ? MINIC_MACHINE_REGISTER_READ |
                MINIC_MACHINE_REGISTER_WRITE :
                index == 1U && form.is_load ? MINIC_MACHINE_REGISTER_WRITE :
                MINIC_MACHINE_REGISTER_READ);
        bindings[index].asm_constraint = index == 0U ? "+r" :
            index == 1U ? (form.is_load ? "=r" : "rZ") : "r";
    }
    if (!a64_load_core(sel, ops[0].value, A64_T3, 64U) ||
        !a64_memory_register(sel, MINIC_A64_MACHINE_LDR_W, false,
            A64_T0, 32U, A64_T3, 0U, false, false) ||
        !a64_load_core(sel, ops[2].value, A64_T2, 64U) ||
        (!form.is_load &&
         !a64_load_core(sel, ops[1].value, A64_T1, bits)) ||
        !minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, form.is_load ? MINIC_MACHINE_EFFECT_MEMORY_READ :
                                   MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings, 3U, NULL, 0U, false, false)) return false;
    index = form.width == 1U ? 0U : form.width == 2U ? 1U :
        form.width == 4U ? 2U : 3U;
    if (form.is_load) {
        if (!a64_load_core(sel, ops[1].value, A64_T3, 64U))
            return false;
        writeback[0] = a64_gp(A64_T1, bits, MINIC_MACHINE_REGISTER_READ);
        writeback[1] = a64_address(A64_T3);
        writeback[1].value.address.target_mode =
            MINIC_A64_ADDRESS_BASE_ONLY;
        if (!a64_emit(sel, stores[index], writeback, 2U))
            return false;
        sel->builder.function->blocks[sel->builder.current_block]
            .instructions[sel->builder.function->blocks[
                sel->builder.current_block].instruction_count - 1U]
            .effects |= MINIC_MACHINE_EFFECT_VOLATILE;
    }
    return a64_load_core(sel, ops[0].value, A64_T3, 64U) &&
        a64_memory_register(sel, MINIC_A64_MACHINE_STR_W, false,
            A64_T0, 32U, A64_T3, 0U, true, true);
}

/* Original Linux read_sysreg()/write_sysreg() and the encoded MSR_S/MRS_S
 * macro wrappers are one audited family: a sealed finite ISA system-register
 * grammar, one physical x9 operand, and a typed Core lvalue store for reads.
 * GNU assembles the original named register or macro, never an invented
 * string-valued Machine opcode. */
static bool a64_linux_sysreg_source(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *operand =
        ins->value.structured_inline_asm.operands;
    const uint32_t form = source->target_machine_form_id;
    const bool read = form == AARCH64_MACHINE_ASM_MRS ||
        form == AARCH64_MACHINE_ASM_ENCODED_SYSREG_READ;
    const uint32_t variant = form == AARCH64_MACHINE_ASM_ENCODED_SYSREG_READ ?
        aarch64_linux_encoded_sysreg_form(source->template_text,
            source->template_length) :
        form == AARCH64_MACHINE_ASM_ENCODED_SYSREG_WRITE ?
        aarch64_linux_encoded_sysreg_write_form(source->template_text,
            source->template_length) : 0U;
    MinicMachineOperand binding = a64_gp(A64_T0, 64U,
        read ? MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
    if ((form != AARCH64_MACHINE_ASM_MRS &&
         form != AARCH64_MACHINE_ASM_MSR &&
         form != AARCH64_MACHINE_ASM_ENCODED_SYSREG_READ &&
         form != AARCH64_MACHINE_ASM_ENCODED_SYSREG_WRITE) ||
        (form == AARCH64_MACHINE_ASM_MRS &&
         aarch64_linux_mrs_register(source->template_text,
             source->template_length) == NULL) ||
        (form == AARCH64_MACHINE_ASM_MSR &&
         aarch64_linux_msr_register(source->template_text,
             source->template_length) == NULL) ||
        ((form == AARCH64_MACHINE_ASM_ENCODED_SYSREG_READ ||
          form == AARCH64_MACHINE_ASM_ENCODED_SYSREG_WRITE) &&
         variant == 0U) ||
        source->target_machine_form_variant != variant ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        source->has_memory_clobber || source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        ins->value.structured_inline_asm.operand_count != 1U ||
        sel->ctx->machine_frame->has_dynamic_stack || operand == NULL ||
        operand->operand_index != 0U ||
        operand->kind != (read ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
        operand->has_fixed_register_binding || operand->early_clobber ||
        operand->physical_early_clobber || operand->physical_read_write ||
        operand->has_tied_output || operand->value >= fn->value_count ||
        !aarch64_asm_core_integer(fn, operand->value, read, 8U, 0))
        return false;
    binding.asm_constraint = read ? "=r" :
        form == AARCH64_MACHINE_ASM_MSR &&
        strcmp(aarch64_linux_msr_register(source->template_text,
            source->template_length), "s3_0_c15_c0_4") == 0 ? "r" : "rZ";
    if (!read && !a64_load_core(sel, operand->value, A64_T0, 64U))
        return false;
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder,
            source, 0U, &binding, 1U, NULL, 0U, false, false)) return false;
    if (!read) return true;
    return a64_load_core(sel, operand->value, A64_T1, 64U) &&
        a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
            A64_T0, 64U, A64_T1, 0U, true, false);
}

/* Kernel CASP128 keeps the original GNU source template, including
 * .arch_extension, GNU named operands and the observable x0/x1 fixed pair.
 * The memory carrier and seven scalar operands are materialized BEFORE the
 * sealed MachineIR ASM; only the two read/write lvalues are written back. */
static bool a64_linux_casp128_source(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    static const char *const names[] = {
        "old1", "old2", "v", "new1", "new2", "ptr", "oldval1", "oldval2"
    };
    static const char *const constraints[] = {
        "+&r", "+&r", "+Q", "r", "r", "r", "r", "r"
    };
    static const unsigned int registers[] = {0U, 1U, 10U, 2U, 3U, 4U, 11U, 12U};
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    const unsigned int form = aarch64_linux_lse_casp128_form(
        source->template_text, source->template_length);
    MinicMachineOperand machine[8];
    size_t i;
    if (form == 0U ||
        source->target_machine_form_id != AARCH64_MACHINE_ASM_LSE_CASP128 ||
        source->target_machine_form_variant != form ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ins->value.structured_inline_asm.operand_count != 8U ||
        ops == NULL || sel->ctx->machine_frame->has_dynamic_stack)
        return false;
    for (i = 0U; i < 8U; ++i) {
        const bool fixed = i < 2U || (i >= 3U && i <= 5U);
        const MinicCoreFixedRegisterBinding *binding;
        if (ops[i].operand_index != i || ops[i].value >= fn->value_count ||
            ops[i].has_fixed_register_binding != fixed ||
            ops[i].early_clobber || ops[i].has_tied_output ||
            ops[i].kind != (i == 2U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                i < 2U ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            !aarch64_asm_core_integer(fn, ops[i].value, i <= 2U,
                i == 2U ? 16U : 8U, -1)) return false;
        if (fixed) {
            if (ops[i].fixed_register_binding_id >=
                fn->fixed_register_binding_count) return false;
            binding = &fn->fixed_register_bindings[
                ops[i].fixed_register_binding_id];
            if (!binding->is_local || binding->register_name == NULL ||
                binding->register_name_length != 2U ||
                (binding->register_name[0] != 'x' &&
                 binding->register_name[0] != 'r') ||
                binding->register_name[1] !=
                    (char)('0' + (i < 2U ? i : i - 1U))) return false;
        }
        machine[i] = a64_gp(registers[i], 64U,
            i < 2U ? MINIC_MACHINE_REGISTER_READ |
                MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
        machine[i].asm_constraint = (char *)constraints[i];
        machine[i].asm_name = (char *)names[i];
    }
    if (!a64_load_core(sel, ops[2].value, 10U, 64U) ||
        !a64_load_core(sel, ops[0].value, 11U, 64U) ||
        !a64_memory_register(sel, MINIC_A64_MACHINE_LDR_X, false,
            0U, 64U, 11U, 0U, false, false) ||
        !a64_load_core(sel, ops[1].value, 11U, 64U) ||
        !a64_memory_register(sel, MINIC_A64_MACHINE_LDR_X, false,
            1U, 64U, 11U, 0U, false, false) ||
        !a64_load_core(sel, ops[3].value, 2U, 64U) ||
        !a64_load_core(sel, ops[4].value, 3U, 64U) ||
        !a64_load_core(sel, ops[5].value, 4U, 64U) ||
        !a64_load_core(sel, ops[6].value, 11U, 64U) ||
        !a64_load_core(sel, ops[7].value, 12U, 64U) ||
        !minic_machine_builder_emit_core_source_asm(&sel->builder, source,
            MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            machine, 8U, NULL, 0U, false, false) ||
        !a64_load_core(sel, ops[0].value, 11U, 64U) ||
        !a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
            0U, 64U, 11U, 0U, true, false) ||
        !a64_load_core(sel, ops[1].value, 11U, 64U) ||
        !a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
            1U, 64U, 11U, 0U, true, false)) return false;
    return true;
}

/* One selected physical path for the complete scalar LSE family:
 * st{clr,set,eor,add}, their fetched counterparts and CAS{b,h,w,x}.
 * The front end has already sealed GNU input/output constraints; x10 is
 * the true +Q address while x9 is the hardware old-value result. */
static bool a64_linux_scalar_lse_source(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    Aarch64LseForm form = {0};
    MinicMachineOperand machine[3];
    static const uint32_t store[4] = {
        MINIC_A64_MACHINE_STRB, MINIC_A64_MACHINE_STRH,
        MINIC_A64_MACHINE_STR_W, MINIC_A64_MACHINE_STR_X
    };
    static const uint32_t load[4] = {
        MINIC_A64_MACHINE_LDRB, MINIC_A64_MACHINE_LDRH,
        MINIC_A64_MACHINE_LDR_W, MINIC_A64_MACHINE_LDR_X
    };
    size_t index, count, i;
    unsigned int bits;
    if (source->target_machine_form_id != AARCH64_MACHINE_ASM_LSE ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        source->template_text == NULL ||
        !(aarch64_lse_form(source->template_text, source->template_length,
            false, &form) ||
          aarch64_lse_form(source->template_text, source->template_length,
            true, &form)) ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber || source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ops == NULL || sel->ctx->machine_frame->has_dynamic_stack)
        return false;
    if (source->target_machine_form_variant !=
            (((uint32_t)form.kind << 16) | ((uint32_t)form.width << 8) |
             form.mode)) return false;
    count = form.kind == AARCH64_LSE_STORE ? 2U : 3U;
    index = form.width == 1U ? 0U : form.width == 2U ? 1U :
        form.width == 4U ? 2U : 3U;
    if (ins->value.structured_inline_asm.operand_count != count ||
        (form.width != 1U && form.width != 2U &&
         form.width != 4U && form.width != 8U) || form.mode > 3U)
        return false;
    bits = form.width == 8U ? 64U : 32U;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind kind = i == 0U ?
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
            (i == 1U && form.kind == AARCH64_LSE_FETCH) ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT :
            (i == 1U && form.kind == AARCH64_LSE_CAS) ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (ops[i].operand_index != i || ops[i].kind != kind ||
            ops[i].value >= fn->value_count || ops[i].early_clobber ||
            ops[i].has_fixed_register_binding || ops[i].has_tied_output ||
            !aarch64_asm_core_integer(fn, ops[i].value,
                i != count - 1U, form.width, -1)) return false;
    }
    machine[0] = a64_gp(10U, 64U, MINIC_MACHINE_REGISTER_READ);
    machine[0].asm_constraint = "+Q";
    machine[0].asm_name = "v";
    if (!a64_load_core(sel, ops[0].value, 10U, 64U)) return false;
    if (form.kind == AARCH64_LSE_STORE) {
        machine[1] = a64_gp(11U, 64U, MINIC_MACHINE_REGISTER_READ);
        machine[1].asm_constraint = "r";
        machine[1].asm_name = "i";
        if (!a64_load_core(sel, ops[1].value, 11U, bits)) return false;
    } else {
        machine[1] = a64_gp(9U, 64U, form.kind == AARCH64_LSE_CAS ?
            MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE :
            MINIC_MACHINE_REGISTER_WRITE);
        machine[1].asm_constraint = form.kind == AARCH64_LSE_CAS ? "+r" : "=r";
        machine[1].asm_name = "old";
        machine[2] = a64_gp(11U, 64U, MINIC_MACHINE_REGISTER_READ);
        machine[2].asm_constraint = form.kind == AARCH64_LSE_CAS ? "rZ" : "r";
        machine[2].asm_name = form.kind == AARCH64_LSE_CAS ? "new" : "i";
        if (form.kind == AARCH64_LSE_CAS) {
            if (!a64_load_core(sel, ops[1].value, 12U, 64U) ||
                !a64_memory_register(sel, load[index], false,
                    9U, bits, 12U, 0U, false, false)) return false;
        }
        if (!a64_load_core(sel, ops[2].value, 11U, bits)) return false;
    }
    if (!minic_machine_builder_emit_core_source_asm(&sel->builder, source,
        MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        machine, count, NULL, 0U, false, false)) return false;
    if (form.kind == AARCH64_LSE_STORE) return true;
    return a64_load_core(sel, ops[1].value, 12U, 64U) &&
        a64_memory_register(sel, store[index], false,
            9U, bits, 12U, 0U, true, false);
}

/* Signed decrement-unless-negative is an indivisible GNU CASAL retry loop,
 * not a single pseudo-instruction. Keep the original 1b/2f local labels,
 * physical condition-code clobber and distinct +Q/ret/tmp operands. */
static bool a64_linux_lse_dec_positive_source(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, const MinicCoreInlineAsm *source) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreStructuredInlineAsmOperand *ops =
        ins->value.structured_inline_asm.operands;
    MinicMachineRegisterEffect flags = {
        {0U, 4U, MINIC_MACHINE_REGISTER_FLAGS},
        MINIC_MACHINE_REGISTER_CLOBBER
    };
    MinicMachineOperand machine[3];
    size_t i;
    if (source->target_machine_form_id !=
            AARCH64_MACHINE_ASM_LSE_DEC_POSITIVE ||
        source->target_machine_form_variant != 0U ||
        !aarch64_linux_lse_dec_positive_form(
            source->template_text, source->template_length) ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->register_clobber_count != 1U ||
        source->register_clobbers == NULL ||
        source->register_clobbers[0].name == NULL ||
        source->register_clobbers[0].name_length != 2U ||
        memcmp(source->register_clobbers[0].name, "cc", 2U) != 0 ||
        source->resolved_immediate_count != 0U ||
        source->symbol_reference_count != 0U ||
        source->deferred_immediate_symbol != NULL ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        ops == NULL || sel->ctx->machine_frame->has_dynamic_stack)
        return false;
    for (i = 0U; i < 3U; ++i)
        if (ops[i].operand_index != i || ops[i].value >= fn->value_count ||
            ops[i].has_fixed_register_binding || ops[i].early_clobber ||
            ops[i].has_tied_output ||
            ops[i].kind != (i == 0U ?
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE :
                i == 1U ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE :
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT) ||
            !(i == 0U ? aarch64_asm_core_pointer(fn, ops[i].value, true) :
                aarch64_asm_core_integer(fn, ops[i].value, true, 8U, -1)))
            return false;
    machine[0] = a64_gp(9U, 64U,
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE);
    machine[0].asm_name = "ret";
    machine[0].asm_constraint = "+&r";
    machine[1] = a64_gp(10U, 64U, MINIC_MACHINE_REGISTER_READ);
    machine[1].asm_name = "v";
    machine[1].asm_constraint = "+Q";
    machine[2] = a64_gp(12U, 64U, MINIC_MACHINE_REGISTER_WRITE);
    machine[2].asm_name = "tmp";
    machine[2].asm_constraint = "=&r";
    if (!a64_load_core(sel, ops[1].value, 10U, 64U) ||
        !a64_load_core(sel, ops[0].value, 11U, 64U) ||
        !a64_memory_register(sel, MINIC_A64_MACHINE_LDR_X, false,
            9U, 64U, 11U, 0U, false, false) ||
        !minic_machine_builder_emit_core_source_asm(&sel->builder, source,
            MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            machine, 3U, &flags, 1U, false, false) ||
        !a64_load_core(sel, ops[0].value, 11U, 64U) ||
        !a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
            9U, 64U, 11U, 0U, true, false) ||
        !a64_load_core(sel, ops[2].value, 11U, 64U) ||
        !a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
            12U, 64U, 11U, 0U, true, false)) return false;
    return true;
}

/* Physical source-ASM seals require two independent proofs. machine_opcode.c
 * checks the exact hardware opcode, size and effects; this target callback
 * checks the corresponding sealed Core address/scalar roles and native type
 * layout. The selected instruction cannot invent an ASM role by copying the
 * form ID alone. */
bool minic_aarch64_machine_prove_selected_source_asm(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *source, const MinicMachineFunction *machine,
    const MinicMachineInstruction *selected, void *context) {
    const MinicCoreStructuredInlineAsmOperand *ops;
    size_t i;
    size_t width;
    const bool acquire = source != NULL &&
        source->target_machine_form_id == AARCH64_MACHINE_ASM_LDAR;
    const bool release = source != NULL &&
        source->target_machine_form_id == AARCH64_MACHINE_ASM_STLR;
    const bool bit_reverse = source != NULL &&
        source->target_machine_form_id == AARCH64_MACHINE_ASM_BIT_REVERSE;
    (void)context;
    if (fn == NULL || ins == NULL || source == NULL || machine == NULL ||
        machine->target == NULL ||
        machine->target->architecture != MINIC_TARGET_ARCH_AARCH64 ||
        selected == NULL || !selected->has_selected_source_asm_seal ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        !minic_core_function_type_is_kind(fn, ins->core_type_id,
            MINIC_CORE_TYPE_VOID) ||
        !source->is_volatile || source->is_goto ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        ins->value.structured_inline_asm.operand_count != 2U ||
        (!acquire && !release && !bit_reverse)) return false;
    ops = ins->value.structured_inline_asm.operands;
    for (i = 0U; i < 2U; ++i)
        if (ops[i].operand_index != i || ops[i].value >= fn->value_count ||
            ops[i].has_fixed_register_binding || ops[i].early_clobber ||
            ops[i].physical_early_clobber || ops[i].physical_read_write ||
            ops[i].has_tied_output) return false;
    if (bit_reverse) {
        static const char template_text[] = "rbit %w0, %w1";
        return source->target_machine_form_variant == 0U &&
            source->template_text != NULL &&
            source->template_length == sizeof(template_text) - 1U &&
            memcmp(source->template_text, template_text,
                sizeof(template_text) - 1U) == 0 &&
            !source->has_memory_clobber &&
            ops[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
            ops[1].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT &&
            aarch64_asm_core_integer(fn, ops[0].value, true, 4U, -1) &&
            aarch64_asm_core_integer(fn, ops[1].value, false, 4U, -1);
    }
    width = source->target_machine_form_variant;
    if (!source->has_memory_clobber ||
        (width != 1U && width != 2U && width != 4U && width != 8U) ||
        (acquire ? aarch64_load_acquire_width(source->template_text,
            source->template_length) :
            aarch64_stlr_width(source->template_text,
                source->template_length)) != width)
        return false;
    if (acquire)
        return ops[0].kind ==
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
            ops[1].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT &&
            aarch64_stlr_scalar_core(fn, ops[0].value, true, width) &&
            aarch64_stlr_memory_carrier_core(fn, ops[1].value, width);
    return ops[0].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT &&
        ops[1].kind == MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT &&
        aarch64_stlr_memory_carrier_core(fn, ops[0].value, width) &&
        aarch64_stlr_scalar_core(fn, ops[1].value, false, width);
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
        const size_t source_block = sel->builder.current_block;
        MinicMachineOperand branch[2];
        if (!a64_load_constant(sel, A64_T3, (uint64_t)bytes) ||
            !minic_machine_builder_create_block(&sel->builder, NULL,
                false, true, &loop) ||
            !minic_machine_builder_create_block(&sel->builder, NULL,
                false, true, &done) ||
            !minic_machine_builder_position_at(&sel->builder, source_block) ||
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

/* Transfer an already-planned AAPCS64 record parameter into its owned
 * addressable Core object.  The source backend's byte-exact ingress logic
 * is retained, but the locations and object bounds remain Common-owned and
 * all output uses the existing typed Machine/frame emitters.  A partial
 * final eightbyte must not overwrite a neighbouring frame object. */
static bool a64_parameter_object(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicLP64ArgumentLocation *location;
    size_t offset, chunk, size;
    if (sel->ctx->incoming_plan == NULL ||
        !minic_multi_direct_parameter_object_location(sel->ctx->fn, ins,
            sel->ctx->incoming_plan, sel->ctx->frame_plan,
            &location, &offset)) return false;
    size = location->size_bytes;
    if (location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
        if (size == 0U || size > 2048U ||
            ((location->gp_registers == 1U) ==
             (location->stack_slots == 1U)) ||
            (location->gp_registers == 1U && location->stack_slots != 0U) ||
            (location->stack_slots == 1U && location->gp_registers != 0U) ||
            location->fp_registers != 0U ||
            (location->gp_registers != 0U && location->gp_first >= 8U) ||
            (location->stack_slots != 0U &&
             location->stack_first > SIZE_MAX / 8U)) return false;
        /* x9: caller's record address; x10: our owned record address. */
        if (location->gp_registers != 0U) {
            if (!a64_rr(sel, MINIC_A64_MACHINE_MOV, 64U,
                    A64_T0, (unsigned int)location->gp_first)) return false;
        } else if (!minic_a64_machine_emit_frame_load(&sel->builder,
                &sel->view, (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING,
                    location->stack_first * 8U},
                (MinicA64FrameRegister){MINIC_A64_REG_X, A64_T0},
                (MinicA64ScratchPolicy){0U, 0U})) return false;
        return minic_a64_machine_emit_frame_address(&sel->builder,
                &sel->view, minic_a64_owned_slot(offset), A64_T1,
                (MinicA64ScratchPolicy){0U, 0U}) &&
            a64_copy_bytes(sel, size, false, false);
    }
    if (location->kind != MINIC_LP64_ARGUMENT_RECORD ||
        size == 0U || size > 16U ||
        location->fp_registers != 0U ||
        location->chunks != (size + 7U) / 8U ||
        location->gp_registers > location->chunks ||
        location->stack_slots !=
            location->chunks - location->gp_registers ||
        (location->gp_registers != 0U &&
         (location->gp_first >= 8U ||
          location->gp_registers > 8U - location->gp_first)) ||
        (location->stack_slots != 0U &&
         location->stack_first > SIZE_MAX / 8U -
             (location->stack_slots - 1U))) return false;
    for (chunk = 0U; chunk < location->chunks; ++chunk) {
        const bool in_register = chunk < location->gp_registers;
        const size_t payload = size - chunk * 8U >= 8U ?
            8U : size - chunk * 8U;
        size_t byte;
        /* x10 is the exact destination byte address; x9 carries one
         * little-endian AAPCS64 argument eightbyte. */
        if (offset > SIZE_MAX - chunk * 8U ||
            !minic_a64_machine_emit_frame_address(&sel->builder,
                &sel->view, minic_a64_owned_slot(offset + chunk * 8U),
                A64_T1, (MinicA64ScratchPolicy){0U, 0U})) return false;
        if (in_register) {
            if (!a64_rr(sel, MINIC_A64_MACHINE_MOV, 64U,
                    A64_T0,
                    (unsigned int)(location->gp_first + chunk))) return false;
        } else if (!minic_a64_machine_emit_frame_load(&sel->builder,
                &sel->view, (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING,
                    (location->stack_first + chunk -
                     location->gp_registers) * 8U},
                (MinicA64FrameRegister){MINIC_A64_REG_X, A64_T0},
                (MinicA64ScratchPolicy){0U, 0U})) return false;
        for (byte = 0U; byte < payload; ++byte) {
            if (!a64_memory_register(sel, MINIC_A64_MACHINE_STRB,
                    false, A64_T0, 32U, A64_T1, byte, true, false) ||
                (byte + 1U < payload &&
                 !a64_rri(sel, MINIC_A64_MACHINE_LSR_IMM,
                     64U, A64_T0, A64_T0, 8))) return false;
        }
    }
    return true;
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

/* Preserve the old AAPCS64 checked-builtin arithmetic exactly, but keep
 * arithmetic, flags and the result-pointer store as independently verified
 * physical instructions. No output text or second Core arithmetic planner. */
static bool a64_checked_overflow(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreValueId lhs = ins->value.integer_overflow.left;
    const MinicCoreValueId rhs = ins->value.integer_overflow.right;
    const MinicCoreValueId address = ins->value.integer_overflow.result_address;
    const int op = ins->value.integer_overflow.operator_kind;
    size_t width;
    bool sign;
    uint32_t store_op;
    if (minic_core_integer_overflow_shape(fn, ins, &width, &sign)) {
        const unsigned int bits = width == 8U ? 64U : 32U;
        if ((width != 4U && width != 8U) ||
            !a64_load_core(sel, lhs, A64_T0, bits) ||
            !a64_load_core(sel, rhs, A64_T1, bits) ||
            !a64_load_core(sel, address, A64_T2, 64U)) return false;
        if (op == MINIC_CORE_INTEGER_OVERFLOW_ADD ||
            op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
            const bool sub = op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT;
            if (!a64_rrr(sel, sub ? MINIC_A64_MACHINE_SUBS :
                    MINIC_A64_MACHINE_ADDS, bits, A64_T0, A64_T0, A64_T1) ||
                !a64_cset(sel, A64_T1, sign ? 6 : sub ? 3 : 2)) return false;
        } else if (op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
            if (bits == 64U) {
                if (!a64_rrr(sel, sign ? MINIC_A64_MACHINE_SMULH :
                        MINIC_A64_MACHINE_UMULH, 64U,
                        A64_T3, A64_T0, A64_T1) ||
                    !a64_rrr(sel, MINIC_A64_MACHINE_MUL, 64U,
                        A64_T0, A64_T0, A64_T1)) return false;
                if (sign) {
                    if (!a64_rri(sel, MINIC_A64_MACHINE_ASR_IMM, 64U,
                            A64_T1, A64_T0, 63) ||
                        !a64_compare(sel, 64U, A64_T3, A64_T1)) return false;
                } else if (!a64_cmp_zero(sel, 64U, A64_T3)) return false;
            } else {
                if (!a64_widen_mul(sel, sign,
                        A64_T3, A64_T0, A64_T1)) return false;
                if (sign) {
                    if (!a64_extend_word(sel, A64_T1, A64_T3, true) ||
                        !a64_compare(sel, 64U, A64_T3, A64_T1)) return false;
                } else if (!a64_rri(sel, MINIC_A64_MACHINE_LSR_IMM,
                    64U, A64_T1, A64_T3, 32) ||
                    !a64_cmp_zero(sel, 64U, A64_T1)) return false;
                if (!a64_rr(sel, MINIC_A64_MACHINE_MOV, 32U,
                        A64_T0, A64_T3)) return false;
            }
            if (!a64_cset(sel, A64_T1, 1)) return false;
        } else return false;
        store_op = bits == 64U ? MINIC_A64_MACHINE_STR_X :
            MINIC_A64_MACHINE_STR_W;
        return a64_memory_register(sel, store_op, false, A64_T0,
                   bits, A64_T2, 0U, true, false) &&
            a64_store_core(sel, ins->result, A64_T1, 32U);
    }
    {
        MinicCoreIntegerOverflowNarrowShape narrow;
        if (minic_core_integer_overflow_narrow_result_shape(fn, ins,
                &narrow)) {
            const uint32_t math = op == MINIC_CORE_INTEGER_OVERFLOW_ADD ?
                MINIC_A64_MACHINE_ADD :
                op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT ?
                    MINIC_A64_MACHINE_SUB : MINIC_A64_MACHINE_MUL;
            const uint32_t narrow_store = narrow.result_width == 1U ?
                MINIC_A64_MACHINE_STRB : MINIC_A64_MACHINE_STRH;
            if ((op != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
                 op != MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
                 op != MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
                !a64_load_core(sel, lhs, A64_T0, 32U) ||
                !a64_load_core(sel, rhs, A64_T1, 32U) ||
                !a64_load_core(sel, address, A64_T2, 64U) ||
                !a64_normalize(sel, narrow.left_width,
                    narrow.left_signed, A64_T0) ||
                !a64_normalize(sel, narrow.right_width,
                    narrow.right_signed, A64_T1) ||
                !a64_extend_word(sel, A64_T0, A64_T0,
                    narrow.left_signed) ||
                !a64_extend_word(sel, A64_T1, A64_T1,
                    narrow.right_signed) ||
                !a64_rrr(sel, math, 64U, A64_T3, A64_T0, A64_T1) ||
                !a64_rr(sel, MINIC_A64_MACHINE_MOV, 32U,
                    A64_T0, A64_T3) ||
                !a64_normalize(sel, narrow.result_width,
                    narrow.result_signed, A64_T0) ||
                !a64_extend_word(sel, 13U, A64_T0,
                    narrow.result_signed) ||
                !a64_compare(sel, 64U, A64_T3, 13U) ||
                !a64_cset(sel, A64_T1, 1)) return false;
            return a64_memory_register(sel, narrow_store, false,
                       A64_T0, 32U, A64_T2, 0U, true, false) &&
                a64_store_core(sel, ins->result, A64_T1, 32U);
        }
    }
    {
        bool signed_left, signed_right, signed_result;
        if (minic_core_integer_overflow_mixed_multiply_i32_shape(fn,
                ins, &signed_left, &signed_right, &signed_result)) {
            if (!a64_load_core(sel, lhs, A64_T0, 32U) ||
                !a64_load_core(sel, rhs, A64_T1, 32U) ||
                !a64_load_core(sel, address, A64_T2, 64U) ||
                !a64_extend_word(sel, A64_T0, A64_T0, signed_left) ||
                !a64_extend_word(sel, A64_T1, A64_T1, signed_right) ||
                !a64_rrr(sel, MINIC_A64_MACHINE_MUL, 64U,
                    A64_T3, A64_T0, A64_T1) ||
                !a64_rr(sel, MINIC_A64_MACHINE_MOV, 32U,
                    A64_T0, A64_T3) ||
                !a64_extend_word(sel, 13U, A64_T0, signed_result) ||
                !a64_compare(sel, 64U, A64_T3, 13U) ||
                !a64_cset(sel, A64_T1, 1)) return false;
            return a64_memory_register(sel, MINIC_A64_MACHINE_STR_W,
                       false, A64_T0, 32U, A64_T2, 0U, true, false) &&
                a64_store_core(sel, ins->result, A64_T1, 32U);
        }
    }
    {
        bool signed_left;
        if (!minic_core_integer_overflow_mixed_add_i32_shape(fn,
                ins, &signed_left) ||
            !a64_load_core(sel, lhs, A64_T0, 64U) ||
            !a64_load_core(sel, rhs, A64_T1, 64U) ||
            !a64_load_core(sel, address, A64_T2, 64U) ||
            !a64_extend_word(sel, signed_left ? A64_T0 : A64_T1,
                signed_left ? A64_T0 : A64_T1, true) ||
            !a64_rri(sel, MINIC_A64_MACHINE_ASR_IMM, 64U,
                A64_T3, signed_left ? A64_T0 : A64_T1, 63) ||
            !a64_rrr(sel, MINIC_A64_MACHINE_ADDS, 64U,
                A64_T0, A64_T0, A64_T1) ||
            !a64_rrr(sel, MINIC_A64_MACHINE_ADC, 64U,
                A64_T3, A64_T3, MINIC_A64_MACHINE_ZR) ||
            !a64_extend_word(sel, 13U, A64_T0, true) ||
            !a64_compare(sel, 64U, A64_T0, 13U) ||
            !a64_cset(sel, 14U, 1) ||
            !a64_rri(sel, MINIC_A64_MACHINE_ASR_IMM, 64U,
                13U, 13U, 63) ||
            !a64_compare(sel, 64U, A64_T3, 13U) ||
            !a64_cset(sel, 15U, 1) ||
            !a64_rrr(sel, MINIC_A64_MACHINE_ORR, 32U,
                A64_T1, 14U, 15U)) return false;
        return a64_memory_register(sel, MINIC_A64_MACHINE_STR_W,
                   false, A64_T0, 32U, A64_T2, 0U, true, false) &&
            a64_store_core(sel, ins->result, A64_T1, 32U);
    }
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
    bool compare, shift;
    const MinicCoreValueId left = ins->value.binary.left;
    const MinicCoreValueId right = ins->value.binary.right;
    const bool pointer_less = ins->kind == MINIC_CORE_INSTRUCTION_POINTER_LESS;
    if (left >= fn->value_count || right >= fn->value_count) return false;
    if (a64_wide_integer(fn, fn->values[left].core_type_id)) {
        const MinicCoreType *type = minic_core_type_get(fn->type_context,
            minic_core_type_unqualified(fn->type_context,
                fn->values[left].core_type_id));
        const bool comparison = ins->kind ==
                MINIC_CORE_INSTRUCTION_INTEGER_LESS ||
            ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        const bool sign = type->value.integer.is_signed;
        size_t lhs, rhs, result = 0U;
        /* The shared scalar helper intentionally caps at 8 bytes. AAPCS64
         * i128 is one 16-byte typed value transported through two x regs. */
        if (fn->values[left].core_type_id !=
                fn->values[right].core_type_id ||
            (!comparison && ins->core_type_id !=
                fn->values[left].core_type_id) ||
            !a64_wide_integer(fn, fn->values[right].core_type_id) ||
            !a64_wide_core_slot(sel, left, &lhs) ||
            !a64_wide_core_slot(sel, right, &rhs) ||
            (!comparison && !a64_wide_core_slot(sel, ins->result, &result)) ||
            !a64_load_wide_half(sel, lhs, A64_T0) ||
            !a64_load_wide_half(sel, lhs + 8U, A64_T1) ||
            !a64_load_wide_half(sel, rhs, A64_T2) ||
            !a64_load_wide_half(sel, rhs + 8U, 13U)) return false;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
            if (!a64_rrr(sel, ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD ?
                    MINIC_A64_MACHINE_ADDS : MINIC_A64_MACHINE_SUBS,
                    64U, A64_T0, A64_T0, A64_T2) ||
                !a64_rrr(sel, ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD ?
                    MINIC_A64_MACHINE_ADC : MINIC_A64_MACHINE_SBC,
                    64U, A64_T1, A64_T1, 13U)) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
            if (!a64_rrr(sel, MINIC_A64_MACHINE_UMULH,
                    64U, 14U, A64_T0, A64_T2) ||
                !a64_madd(sel, 14U, A64_T0, 13U, 14U) ||
                !a64_madd(sel, A64_T1, A64_T1, A64_T2, 14U) ||
                !a64_rrr(sel, MINIC_A64_MACHINE_MUL,
                    64U, A64_T0, A64_T0, A64_T2)) return false;
            break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR: {
            const uint32_t op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND ?
                MINIC_A64_MACHINE_AND :
                ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR ?
                MINIC_A64_MACHINE_ORR : MINIC_A64_MACHINE_EOR;
            if (!a64_rrr(sel, op, 64U, A64_T0, A64_T0, A64_T2) ||
                !a64_rrr(sel, op, 64U, A64_T1, A64_T1, 13U)) return false;
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
            if (!a64_compare(sel, 64U, A64_T1, 13U) ||
                !a64_cset(sel, 14U, sign ? 11 : 3) ||
                !a64_cset(sel, 15U, 0) ||
                !a64_compare(sel, 64U, A64_T0, A64_T2) ||
                !a64_cset(sel, A64_T0, 3) ||
                !a64_rrr(sel, MINIC_A64_MACHINE_AND,
                    32U, A64_T0, A64_T0, 15U) ||
                !a64_rrr(sel, MINIC_A64_MACHINE_ORR,
                    32U, A64_T0, A64_T0, 14U)) return false;
            break;
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
            if (!a64_rrr(sel, MINIC_A64_MACHINE_EOR,
                    64U, A64_T0, A64_T0, A64_T2) ||
                !a64_rrr(sel, MINIC_A64_MACHINE_EOR,
                    64U, A64_T1, A64_T1, 13U) ||
                !a64_rrr(sel, MINIC_A64_MACHINE_ORR,
                    64U, A64_T0, A64_T0, A64_T1) ||
                !a64_cmp_zero(sel, 64U, A64_T0) ||
                !a64_cset(sel, A64_T0, 0)) return false;
            break;
        default: return false;
        }
        return comparison ? a64_store_core(sel, ins->result, A64_T0, 32U) :
            a64_store_wide_half(sel, result, A64_T0) &&
            a64_store_wide_half(sel, result + 8U, A64_T1);
    }
    compare = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS ||
        pointer_less || ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
    shift = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
    if (pointer_less) {
        if (!minic_multi_direct_pointer_less_shape(fn, ins)) return false;
    } else if (compare) {
        if (!minic_multi_direct_integer_compare_shape(fn, ins,
            &lhs_width, &rhs_width, &lhs_signed, &rhs_signed)) {
            /* Core scalar equality also admits two pointer values. It is
             * not an integer comparison, but AAPCS64 still compares their
             * full 64-bit addresses with one native CMP. */
            if (ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_EQUAL ||
                !aarch64_asm_core_pointer(fn, left, false) ||
                !aarch64_asm_core_pointer(fn, right, false) ||
                !aarch64_asm_core_integer(fn, ins->result, false, 4U, 1))
                return false;
            lhs_width = rhs_width = 8U;
            lhs_signed = rhs_signed = false;
        }
    } else if (shift) {
        if (!minic_multi_direct_integer_shift_shape(fn, ins,
            &lhs_width, &rhs_width, &lhs_signed, &rhs_signed)) return false;
    } else if (!minic_multi_direct_integer_binary_shape(fn, ins,
        &lhs_width, &rhs_width, &lhs_signed, &rhs_signed)) return false;
    if (!a64_value_width(fn, fn->values[left].core_type_id, &lhs_width,
            &lhs_signed, &pointer) ||
        !a64_value_width(fn, fn->values[right].core_type_id, &rhs_width,
            &rhs_signed, &pointer) || (!shift && lhs_width != rhs_width) ||
        !a64_value_width(fn, ins->core_type_id, &dest_width,
            &dest_signed, &pointer)) return false;
    bits = lhs_width == 8U ? 64U : 32U;
    if (!a64_load_core(sel, left, A64_T0, bits) ||
        (shift && lhs_width < 4U &&
         !a64_normalize(sel, lhs_width, lhs_signed, A64_T0)) ||
        !a64_load_core(sel, right, A64_T1,
            rhs_width == 8U ? 64U : 32U)) return false;
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

/* Linux AAPCS64 va_list is an owned 32-byte cursor:
 * {stack, gr_top, vr_top, gr_offs, vr_offs}. The function frame planner
 * already reserves its cursors and prologue GP/SIMD register save banks.
 * This selector only lowers the existing Core cursor effects into verified
 * physical operations; none of the old FILE* emitter runs on this path. */
static bool a64_variadic(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, MinicCoreInstructionId ins_id) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicMachineLP64FrameContract *frame = sel->ctx->machine_frame;
    const MinicA64ScratchPolicy scratch = {0U, 0U};
    if (ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS) {
        size_t seen = 0U, i, cursor;
        const size_t count = frame->variadic_count;
        size_t gp, fp;
        if (count == 0U || ins_id >= fn->instruction_count ||
            sel->ctx->incoming_plan == NULL ||
            sel->ctx->incoming_plan->stack_slots > SIZE_MAX / 8U ||
            ins->result >= fn->value_count ||
            !minic_core_function_type_is_kind(fn,
                ins->core_type_id, MINIC_CORE_TYPE_POINTER) ||
            frame->variadic_save_base > SIZE_MAX - 224U) return false;
        for (i = 0U; i < ins_id; ++i)
            if (fn->instructions[i].kind ==
                    MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS)
                ++seen;
        if (seen >= count || (count - seen > SIZE_MAX / 32U) ||
            (seen != 0U && frame->frame_size < (count - seen) * 32U))
            return false;
        cursor = seen == 0U ? frame->variadic_save_base :
            frame->frame_size - (count - seen) * 32U;
        if (!minic_a64_machine_emit_frame_address(&sel->builder,
                &sel->view, minic_a64_owned_slot(cursor), A64_T0,
                scratch)) return false;
        if (!ins->value.variadic_address.copy_destination) {
            if (!minic_a64_machine_emit_frame_address(&sel->builder,
                    &sel->view, (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING,
                        sel->ctx->incoming_plan->stack_slots * 8U},
                    A64_T1, scratch) ||
                !a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
                    A64_T1, 64U, A64_T0, 0U, true, false) ||
                !minic_a64_machine_emit_frame_address(&sel->builder,
                    &sel->view, minic_a64_owned_slot(
                        frame->variadic_save_base + 96U), A64_T1, scratch) ||
                !a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
                    A64_T1, 64U, A64_T0, 8U, true, false) ||
                !minic_a64_machine_emit_frame_address(&sel->builder,
                    &sel->view, minic_a64_owned_slot(
                        frame->variadic_save_base + 224U), A64_T1, scratch) ||
                !a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
                    A64_T1, 64U, A64_T0, 16U, true, false)) return false;
            gp = frame->variadic_gp_count < 8U ?
                frame->variadic_gp_count : 8U;
            fp = frame->variadic_fp_count < 8U ?
                frame->variadic_fp_count : 8U;
            if (!a64_load_constant_width(sel, A64_T1,
                    (uint32_t)(-((int32_t)((8U - gp) * 8U))), 32U) ||
                !a64_memory_register(sel, MINIC_A64_MACHINE_STR_W, false,
                    A64_T1, 32U, A64_T0, 24U, true, false) ||
                !a64_load_constant_width(sel, A64_T1,
                    (uint32_t)(-((int32_t)((8U - fp) * 16U))), 32U) ||
                !a64_memory_register(sel, MINIC_A64_MACHINE_STR_W, false,
                    A64_T1, 32U, A64_T0, 28U, true, false)) return false;
        }
        return a64_store_core(sel, ins->result, A64_T0, 64U);
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_COPY) {
        const MinicCoreValueId dest = ins->value.variadic_copy.destination;
        const MinicCoreValueId src = ins->value.variadic_copy.source;
        size_t i;
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            !minic_core_function_type_is_kind(fn,
                ins->core_type_id, MINIC_CORE_TYPE_VOID) ||
            dest >= fn->value_count || src >= fn->value_count ||
            !minic_core_function_type_is_kind(fn,
                fn->values[dest].core_type_id, MINIC_CORE_TYPE_POINTER) ||
            fn->values[dest].core_type_id !=
                fn->values[src].core_type_id ||
            !a64_load_core(sel, dest, A64_T0, 64U) ||
            !a64_load_core(sel, src, A64_T1, 64U)) return false;
        /* Load the entire cursor before writing any byte, matching the
         * original pair-load sequence even for partially aliased cursors. */
        for (i = 0U; i < 4U; ++i)
            if (!a64_memory_register(sel, MINIC_A64_MACHINE_LDR_X, false,
                    (unsigned int)(11U + i), 64U, A64_T1,
                    i * 8U, false, false)) return false;
        for (i = 0U; i < 4U; ++i)
            if (!a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
                    (unsigned int)(11U + i), 64U, A64_T0,
                    i * 8U, true, false)) return false;
        return true;
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR) {
        const MinicCoreValueId pointer = ins->value.operand;
        A64VariadicBlocks *aux;
        MinicMachineOperand branch[2];
        size_t width = 8U;
        bool sign = false, ptr = false;
        unsigned int float_bits;
        bool fp;
        if (ins_id >= fn->instruction_count ||
            ins->result >= fn->value_count ||
            pointer >= fn->value_count ||
            !minic_core_function_type_is_kind(fn,
                fn->values[pointer].core_type_id, MINIC_CORE_TYPE_POINTER) ||
            sel->variadic_blocks == NULL) return false;
        aux = &sel->variadic_blocks[ins_id];
        fp = a64_float_type(fn, ins->core_type_id, &float_bits) &&
            float_bits == 64U;
        if (!fp && !a64_value_width(fn, ins->core_type_id,
                &width, &sign, &ptr)) return false;
        if (aux->register_path == SIZE_MAX || aux->stack_path == SIZE_MAX ||
            aux->join == SIZE_MAX ||
            !a64_load_core(sel, pointer, A64_T0, 64U) ||
            !a64_memory_register(sel, MINIC_A64_MACHINE_LDRSW, false,
                A64_T1, 64U, A64_T0, fp ? 28U : 24U, false, false) ||
            !a64_cmp_zero(sel, 32U, A64_T1)) return false;
        branch[0] = a64_immediate(10); /* GE (gr_offs >= 0). */
        branch[1] = a64_block(aux->stack_path);
        if (!a64_emit(sel, MINIC_A64_MACHINE_B_COND, branch, 2U) ||
            !minic_machine_builder_add_successor(&sel->builder,
                aux->stack_path) ||
            !minic_machine_builder_add_successor(&sel->builder,
                aux->register_path) ||
            !minic_machine_builder_set_fallthrough(&sel->builder,
                aux->register_path) ||
            !minic_machine_builder_position_at(&sel->builder,
                aux->register_path) ||
            !a64_memory_register(sel, MINIC_A64_MACHINE_LDR_X, false,
                A64_T2, 64U, A64_T0, fp ? 16U : 8U, false, false) ||
            !a64_rrr(sel, MINIC_A64_MACHINE_ADD, 64U,
                A64_T2, A64_T2, A64_T1) ||
            !a64_memory_register(sel, MINIC_A64_MACHINE_LDR_X, false,
                13U, 64U, A64_T2, 0U, false, false) ||
            !a64_rri(sel, MINIC_A64_MACHINE_ADD_IMM, 32U,
                A64_T1, A64_T1, fp ? 16 : 8) ||
            !a64_memory_register(sel, MINIC_A64_MACHINE_STR_W, false,
                A64_T1, 32U, A64_T0, fp ? 28U : 24U, true, false) ||
            !a64_branch(sel, aux->join) ||
            !minic_machine_builder_position_at(&sel->builder,
                aux->stack_path) ||
            !a64_memory_register(sel, MINIC_A64_MACHINE_LDR_X, false,
                A64_T2, 64U, A64_T0, 0U, false, false) ||
            !a64_memory_register(sel, MINIC_A64_MACHINE_LDR_X, false,
                13U, 64U, A64_T2, 0U, false, false) ||
            !a64_rri(sel, MINIC_A64_MACHINE_ADD_IMM, 64U,
                A64_T2, A64_T2, 8) ||
            !a64_memory_register(sel, MINIC_A64_MACHINE_STR_X, false,
                A64_T2, 64U, A64_T0, 0U, true, false) ||
            !a64_branch(sel, aux->join) ||
            !minic_machine_builder_position_at(&sel->builder, aux->join) ||
            !a64_rr(sel, MINIC_A64_MACHINE_MOV, 64U, A64_T0, 13U) ||
            (!fp && !ptr && width != 8U &&
             !a64_normalize(sel, width, sign, A64_T0))) return false;
        return a64_store_core(sel, ins->result, A64_T0, 64U);
    }
    return false;
}

/* Complete i128 left/right shifts. The four reserved Machine blocks keep the
 * two conditional fallthroughs physically adjacent to their source blocks,
 * while all Core value transport remains in the planned 16-byte slot. */
static bool a64_wide_shift(A64CoreSelection *sel,
    const MinicCoreInstruction *ins, MinicCoreInstructionId ins_id) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreValueId left = ins->value.binary.left;
    const MinicCoreValueId right = ins->value.binary.right;
    const bool shift_left = ins->kind ==
        MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
    const MinicCoreType *type;
    A64VariadicBlocks *aux;
    size_t from, to, rhs_width;
    bool rhs_signed, rhs_pointer;
    MinicMachineOperand branch[2];
    if (ins_id >= fn->instruction_count ||
        left >= fn->value_count || right >= fn->value_count ||
        fn->values[left].core_type_id != ins->core_type_id ||
        !a64_wide_integer(fn, ins->core_type_id) ||
        !a64_value_width(fn, fn->values[right].core_type_id,
            &rhs_width, &rhs_signed, &rhs_pointer) || rhs_pointer ||
        !a64_wide_core_slot(sel, left, &from) ||
        !a64_wide_core_slot(sel, ins->result, &to)) return false;
    type = minic_core_type_get(fn->type_context,
        minic_core_type_unqualified(fn->type_context, ins->core_type_id));
    aux = &sel->variadic_blocks[ins_id];
    if (aux->nonzero_path == SIZE_MAX || aux->register_path == SIZE_MAX ||
        aux->stack_path == SIZE_MAX || aux->join == SIZE_MAX ||
        !a64_load_core(sel, right, A64_T1,
            rhs_width == 8U ? 64U : 32U) ||
        !a64_load_wide_half(sel, from, A64_T0) ||
        !a64_load_wide_half(sel, from + 8U, A64_T2)) return false;
    branch[0] = a64_gp(A64_T1, 64U, MINIC_MACHINE_REGISTER_READ);
    branch[1] = a64_block(aux->join);
    if (!a64_emit(sel, MINIC_A64_MACHINE_CBZ, branch, 2U) ||
        !minic_machine_builder_add_successor(&sel->builder, aux->join) ||
        !minic_machine_builder_add_successor(&sel->builder,
            aux->nonzero_path) ||
        !minic_machine_builder_set_fallthrough(&sel->builder,
            aux->nonzero_path) ||
        !minic_machine_builder_position_at(&sel->builder,
            aux->nonzero_path) ||
        !a64_load_constant_width(sel, 14U, 64U, 64U) ||
        !a64_compare(sel, 64U, A64_T1, 14U)) return false;
    branch[0] = a64_immediate(2); /* unsigned >= 64 */
    branch[1] = a64_block(aux->stack_path);
    if (!a64_emit(sel, MINIC_A64_MACHINE_B_COND, branch, 2U) ||
        !minic_machine_builder_add_successor(&sel->builder,
            aux->stack_path) ||
        !minic_machine_builder_add_successor(&sel->builder,
            aux->register_path) ||
        !minic_machine_builder_set_fallthrough(&sel->builder,
            aux->register_path) ||
        !minic_machine_builder_position_at(&sel->builder,
            aux->register_path) ||
        !a64_rr(sel, MINIC_A64_MACHINE_NEG, 64U, 13U, A64_T1))
        return false;
    if (shift_left) {
        if (!a64_rrr(sel, MINIC_A64_MACHINE_LSR, 64U,
                13U, A64_T0, 13U) ||
            !a64_rrr(sel, MINIC_A64_MACHINE_LSL, 64U,
                A64_T2, A64_T2, A64_T1) ||
            !a64_rrr(sel, MINIC_A64_MACHINE_ORR, 64U,
                A64_T2, A64_T2, 13U) ||
            !a64_rrr(sel, MINIC_A64_MACHINE_LSL, 64U,
                A64_T0, A64_T0, A64_T1)) return false;
    } else if (!a64_rrr(sel, MINIC_A64_MACHINE_LSL, 64U,
            13U, A64_T2, 13U) ||
        !a64_rrr(sel, MINIC_A64_MACHINE_LSR, 64U,
            A64_T0, A64_T0, A64_T1) ||
        !a64_rrr(sel, MINIC_A64_MACHINE_ORR, 64U,
            A64_T0, A64_T0, 13U) ||
        !a64_rrr(sel, type->value.integer.is_signed ?
                MINIC_A64_MACHINE_ASR : MINIC_A64_MACHINE_LSR,
            64U, A64_T2, A64_T2, A64_T1)) return false;
    if (!a64_branch(sel, aux->join) ||
        !minic_machine_builder_position_at(&sel->builder,
            aux->stack_path) ||
        !a64_rri(sel, MINIC_A64_MACHINE_SUB_IMM, 64U,
            A64_T1, A64_T1, 64)) return false;
    if (shift_left) {
        if (!a64_rrr(sel, MINIC_A64_MACHINE_LSL, 64U,
                A64_T2, A64_T0, A64_T1) ||
            !a64_load_constant(sel, A64_T0, 0U)) return false;
    } else if (!a64_rrr(sel, type->value.integer.is_signed ?
                MINIC_A64_MACHINE_ASR : MINIC_A64_MACHINE_LSR,
            64U, A64_T0, A64_T2, A64_T1) ||
        (type->value.integer.is_signed ?
            !a64_rri(sel, MINIC_A64_MACHINE_ASR_IMM,
                64U, A64_T2, A64_T2, 63) :
            !a64_load_constant(sel, A64_T2, 0U))) return false;
    return a64_branch(sel, aux->join) &&
        minic_machine_builder_position_at(&sel->builder, aux->join) &&
        a64_store_wide_half(sel, to, A64_T0) &&
        a64_store_wide_half(sel, to + 8U, A64_T2);
}

static bool a64_unary(A64CoreSelection *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    size_t width;
    size_t from, to;
    bool sign, is_pointer;
    uint32_t opcode;
    unsigned int bits;
    unsigned int scan_bits;
    /* AAPCS64 transports a single i128 Core operand as two 64-bit halves.
     * The common scalar unary decoder correctly admits only <=64 bits, so
     * select the full physical carry chain before scalar legalization. */
    if ((ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ||
         ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT) &&
        ins->value.operand < fn->value_count &&
        a64_wide_integer(fn,
            fn->values[ins->value.operand].core_type_id)) {
        if (ins->core_type_id !=
                fn->values[ins->value.operand].core_type_id ||
            !a64_wide_core_slot(sel, ins->value.operand, &from) ||
            !a64_wide_core_slot(sel, ins->result, &to) ||
            !a64_load_wide_half(sel, from, A64_T0) ||
            !a64_load_wide_half(sel, from + 8U, A64_T1)) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE) {
            /* 0 - (hi:lo): low NEGS writes NZCV, high NGC consumes carry. */
            if (!a64_rr(sel, MINIC_A64_MACHINE_NEGS, 64U,
                    A64_T0, A64_T0) ||
                !a64_rr(sel, MINIC_A64_MACHINE_NGC, 64U,
                    A64_T1, A64_T1)) return false;
        } else if (!a64_rr(sel, MINIC_A64_MACHINE_MVN, 64U,
                A64_T0, A64_T0) ||
            !a64_rr(sel, MINIC_A64_MACHINE_MVN, 64U,
                A64_T1, A64_T1)) return false;
        return a64_store_wide_half(sel, to, A64_T0) &&
            a64_store_wide_half(sel, to + 8U, A64_T1);
    }
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
    const MinicCoreInstruction *ins, MinicCoreInstructionId ins_id) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicLP64FramePlan *frame = sel->ctx->frame_plan;
    size_t width;
    bool sign, ptr;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER: {
        MinicDirectMachineInstructionShape shape = {0};
        return minic_multi_direct_machine_instruction_shape(fn, ins,
                   false, &shape) &&
            shape.family == MINIC_DIRECT_MACHINE_COMPILER_BARRIER &&
            minic_machine_builder_emit_compiler_barrier(&sel->builder);
    }
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        return a64_variadic(sel, ins, ins_id);
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return a64_parameter(sel, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return a64_parameter_object(sel, ins);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return a64_scalar_bitcast(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        return a64_call_frame_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
        return a64_fixed_sp_read(sel, ins);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        return a64_dynamic_stack_alloc(sel, ins);
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {
        const MinicCoreInlineAsmId id = ins->value.inline_asm_id;
        if (id >= fn->inline_asm_count || fn->inline_asms == NULL)
            return false;
        if (fn->inline_asms[id].is_goto)
            return a64_linux_source_asm_goto(sel, ins, ins_id,
                &fn->inline_asms[id]);
        return a64_opaque_hint(sel, ins);
    }
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM: {
        const MinicCoreInlineAsm *source;
        MinicMachineSourceAsmInput input = {0};
        MinicMachineInstruction candidate = {0};
        if (ins->value.structured_inline_asm.inline_asm_id >=
            fn->inline_asm_count) return false;
        source = &fn->inline_asms[
            ins->value.structured_inline_asm.inline_asm_id];
        if (source->target_machine_form_id ==
                AARCH64_MACHINE_ASM_PSTATE_ALTERNATIVE)
            return a64_linux_pstate_alternative_zero(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_LDAR ||
            source->target_machine_form_id == AARCH64_MACHINE_ASM_STLR)
            return a64_linux_acquire_release(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_BIT_REVERSE)
            return a64_linux_bit_reverse(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_TLBI)
            return a64_linux_tlbi(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_AT_S1E1R ||
            source->target_machine_form_id == AARCH64_MACHINE_ASM_CACHE_OP ||
            source->target_machine_form_id == AARCH64_MACHINE_ASM_PREFETCH)
            return a64_linux_one_gp_source(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_MRS ||
            source->target_machine_form_id == AARCH64_MACHINE_ASM_MSR ||
            source->target_machine_form_id == AARCH64_MACHINE_ASM_ENCODED_SYSREG_READ ||
            source->target_machine_form_id == AARCH64_MACHINE_ASM_ENCODED_SYSREG_WRITE)
            return a64_linux_sysreg_source(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_CMPWAIT ||
            source->target_machine_form_id == AARCH64_MACHINE_ASM_IO_DEPENDENCY ||
            source->target_machine_form_id == AARCH64_MACHINE_ASM_ZEROPAD)
            return a64_linux_extra_source(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_UACCESS)
            return a64_linux_uaccess_source(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_RAW_IO_READ ||
            source->target_machine_form_id == AARCH64_MACHINE_ASM_RAW_IO_STORE)
            return a64_linux_raw_io_source(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_LSE_CASP128)
            return a64_linux_casp128_source(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_LSE)
            return a64_linux_scalar_lse_source(sel, ins, source);
        if (source->target_machine_form_id == AARCH64_MACHINE_ASM_LSE_DEC_POSITIVE)
            return a64_linux_lse_dec_positive_source(sel, ins, source);
        if (ins->result != MINIC_CORE_VALUE_INVALID ||
            ins->value.structured_inline_asm.operand_count != 0U ||
            !minic_core_function_type_is_kind(fn, ins->core_type_id,
                MINIC_CORE_TYPE_VOID)) return false;
        /* A valid GNU spelling is not a machine-admission capability.
         * ID0 belongs only to proved generic structured ASM; target-owned
         * physical forms must carry their exact nonzero Machine form ID.
         * In particular, NONE cannot forge a zero-role `dmb oshst`. */
        if ((source->operand_plan_owner !=
                 MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED &&
             source->operand_plan_owner !=
                 MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED) ||
            (source->operand_plan_owner ==
                 MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED &&
             source->target_machine_form_id != 0U) ||
            (source->operand_plan_owner ==
                 MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED &&
             source->target_machine_form_id == 0U)) return false;
        if (!source->is_volatile || source->is_goto ||
            source->register_clobber_count != 0U ||
            source->resolved_immediate_count != 0U ||
            source->symbol_reference_count != 0U ||
            source->deferred_immediate_symbol != NULL ||
            source->target_machine_form_variant != 0U ||
            (source->target_machine_form_id != 0U &&
             source->target_machine_form_id != AARCH64_MACHINE_ASM_BARRIER) ||
            source->template_text == NULL) return false;
        /* The empty GNU memory-clobber has no hardware opcode. It is still
         * a real ordered compiler effect with this exact Core origin. */
        if (source->template_length == 0U)
            return minic_machine_builder_emit_compiler_barrier(&sel->builder);
        input.source_bytes = source->template_text;
        input.source_byte_length = source->template_length;
        input.target_machine_form_id = source->target_machine_form_id;
        input.effects = (source->has_memory_clobber ?
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER : 0U) |
            MINIC_MACHINE_EFFECT_VOLATILE;
        input.has_unique_id = source->source_asm_unique_id != 0U;
        input.unique_id = source->source_asm_unique_id;
        candidate.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
        candidate.effects = input.effects | MINIC_MACHINE_EFFECT_SOURCE_ASM;
        candidate.source_asm_template = source->template_text;
        candidate.source_asm_length = source->template_length;
        candidate.source_asm_machine_form_id = source->target_machine_form_id;
        return minic_aarch64_machine_source_asm_validate(sel->ctx->target,
            &candidate, NULL) &&
            minic_machine_builder_emit_source_asm_typed(&sel->builder, &input);
    }
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
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        return a64_checked_overflow(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
        return a64_integer_conversion(sel, ins);
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
        if ((ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
             ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) &&
            ins->value.binary.left < fn->value_count &&
            a64_wide_integer(fn, fn->values[
                ins->value.binary.left].core_type_id))
            return a64_wide_shift(sel, ins, ins_id);
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
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS: {
        MinicCoreBlockId core_block;
        const MinicMachineOperand adrp_ops[] = {
            a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_WRITE),
            a64_block_address(0U)
        };
        MinicMachineOperand add_ops[] = {
            a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_WRITE),
            a64_gp(A64_T0, 64U, MINIC_MACHINE_REGISTER_READ),
            a64_block_address(0U)
        };
        MinicMachineOperand adrp[2];
        if (!minic_multi_direct_block_address_shape(fn, ins, &core_block) ||
            core_block >= fn->block_count || !sel->reachable[core_block] ||
            sel->blocks[core_block] == SIZE_MAX) return false;
        memcpy(adrp, adrp_ops, sizeof(adrp));
        adrp[1] = a64_block_address(sel->blocks[core_block]);
        add_ops[2] = a64_block_address(sel->blocks[core_block]);
        return a64_emit(sel, MINIC_A64_MACHINE_ADRP_BLOCK, adrp, 2U) &&
            a64_emit(sel, MINIC_A64_MACHINE_ADD_LO12_BLOCK, add_ops, 3U) &&
            a64_store_core(sel, ins->result, A64_T0, 64U);
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
        /* The shared scalar shape deliberately leaves byte width unset for
         * pointers: it supplies only their Core kind. AAPCS64 LP64 always
         * transfers those values as a full X register, including volatile
         * pointer lvalues, not a fabricated 32-bit integer access. */
        if (ptr) width = 8U;
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

/* AAPCS64 record results are ABI objects, not scalar SSA values. Their
 * physical return classification is already shared with the direct LP64
 * planner; only the actual x0/x1, v0..v3, or caller-owned x8 transport is
 * selected here. The indirect result pointer was saved by the MIR prologue
 * before calls could clobber x8. */
static bool a64_return_record(A64CoreSelection *sel,
    const MinicCoreTerminator *term, MinicCoreTypeId type) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreObjectId object = term->return_object;
    size_t at, bytes, chunks, count, width, i;
    if (object >= fn->object_count || fn->objects == NULL ||
        !minic_lp64_core_equal_unqualified(fn->type_context,
            type, fn->objects[object].core_type_id)) return false;
    if (minic_multi_empty_record_core(fn->type_context, type)) return true;
    if (!minic_lp64_frame_plan_object(sel->ctx->frame_plan, object, &at))
        return false;
    if (minic_multi_fp_return_record_core(fn->type_context,
            type, &count, &width)) {
        if (count > 4U || (width != 4U && width != 8U)) return false;
        for (i = 0U; i < count; ++i)
            if (!minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
                    minic_a64_owned_slot(at + i * width),
                    (MinicA64FrameRegister){width == 4U ?
                        MINIC_A64_REG_S : MINIC_A64_REG_D, (unsigned int)i},
                    (MinicA64ScratchPolicy){0U, 0U})) return false;
        return true;
    }
    if (sel->ctx->machine_frame->has_indirect_return) {
        if (!minic_aarch64_indirect_record_core(fn->type_context,
                type, &bytes) || bytes <= 16U ||
            !minic_a64_machine_emit_frame_address(&sel->builder, &sel->view,
                minic_a64_owned_slot(at), A64_T0,
                (MinicA64ScratchPolicy){0U, 0U}) ||
            !minic_a64_machine_emit_frame_load(&sel->builder, &sel->view,
                minic_a64_owned_slot(
                    sel->ctx->machine_frame->indirect_return_offset),
                (MinicA64FrameRegister){MINIC_A64_REG_X, A64_T1},
                (MinicA64ScratchPolicy){0U, 0U})) return false;
        return a64_copy_bytes(sel, bytes, false, false);
    }
    if (!minic_multi_gp_record_core(fn->type_context,
            type, &bytes, &chunks) || chunks == 0U || chunks > 2U)
        return false;
    for (i = 0U; i < chunks; ++i) {
        const size_t remaining = bytes - i * 8U;
        if (!a64_pack_record_chunk(sel, &sel->view, at + i * 8U,
                remaining >= 8U ? 8U : remaining,
                (unsigned int)i, 0U, true)) return false;
    }
    return true;
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
        const MinicCoreType *return_type = minic_core_type_get(
            fn->type_context,
            minic_core_type_unqualified(fn->type_context, ret_type));
        if (return_type != NULL &&
            return_type->kind == MINIC_CORE_TYPE_RECORD)
            return a64_return_record(sel, term, ret_type) &&
                minic_a64_machine_emit_epilogue(&sel->builder,
                    sel->ctx->machine_frame);
        if (a64_wide_integer(fn, ret_type) ||
            a64_wide_float(fn, ret_type)) {
            size_t at;
            if (!a64_wide_core_slot(sel, term->return_value, &at))
                return false;
            if (a64_wide_integer(fn, ret_type)) {
                if (!a64_load_wide_half(sel, at, 0U) ||
                    !a64_load_wide_half(sel, at + 8U, 1U)) return false;
            } else if (!minic_a64_machine_emit_frame_load(&sel->builder,
                    &sel->view, minic_a64_owned_slot(at),
                    (MinicA64FrameRegister){MINIC_A64_REG_Q, 0U},
                    (MinicA64ScratchPolicy){0U, 0U})) return false;
            return minic_a64_machine_emit_epilogue(&sel->builder,
                sel->ctx->machine_frame);
        }
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
    if (fn->instruction_count > SIZE_MAX / sizeof(size_t))
        goto cleanup;
    sel.goto_emit = malloc((fn->instruction_count != 0U ?
        fn->instruction_count : 1U) * sizeof(*sel.goto_emit));
    sel.goto_continue = malloc((fn->instruction_count != 0U ?
        fn->instruction_count : 1U) * sizeof(*sel.goto_continue));
    if (fn->instruction_count > SIZE_MAX / sizeof(*sel.variadic_blocks))
        goto cleanup;
    if (fn->instruction_count != 0U)
        sel.variadic_blocks = malloc(fn->instruction_count *
            sizeof(*sel.variadic_blocks));
    if (sel.blocks == NULL || sel.false_blocks == NULL ||
        sel.true_blocks == NULL || sel.reachable == NULL ||
        sel.goto_emit == NULL || sel.goto_continue == NULL ||
        (fn->instruction_count != 0U && sel.variadic_blocks == NULL) ||
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
    for (i = 0U; i < fn->instruction_count; ++i)
        sel.goto_emit[i] = sel.goto_continue[i] = SIZE_MAX;
    for (i = 0U; i < fn->instruction_count; ++i)
        sel.variadic_blocks[i] = (A64VariadicBlocks){SIZE_MAX,
            SIZE_MAX, SIZE_MAX, SIZE_MAX};
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
        { size_t j;
          for (j = 0U; j < block->instruction_count; ++j) {
              MinicCoreInstructionId ins_id;
              const MinicCoreInstruction *ins;
              A64VariadicBlocks *aux;
              if (!minic_core_block_instruction_at(fn, block, j,
                      &ins_id, &ins) || ins_id >= fn->instruction_count)
                  goto cleanup;
              if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM) {
                  const MinicCoreInlineAsmId source_id =
                      ins->value.inline_asm_id;
                  if (fn->inline_asms == NULL ||
                      source_id >= fn->inline_asm_count) goto cleanup;
                  if (fn->inline_asms[source_id].is_goto &&
                      (!minic_machine_builder_create_block(&sel.builder,
                           NULL, false, true, &sel.goto_emit[ins_id]) ||
                       !minic_machine_builder_create_block(&sel.builder,
                           NULL, false, true,
                           &sel.goto_continue[ins_id]))) goto cleanup;
              }
              if (ins->kind !=
                      MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR &&
                  !((ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
                     ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) &&
                    ins->value.binary.left < fn->value_count &&
                    a64_wide_integer(fn, fn->values[
                        ins->value.binary.left].core_type_id)))
                  continue;
              aux = &sel.variadic_blocks[ins_id];
              if (ins->kind !=
                      MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR &&
                  !minic_machine_builder_create_block(&sel.builder,
                      NULL, false, true, &aux->nonzero_path))
                  goto cleanup;
              if (!minic_machine_builder_create_block(&sel.builder,
                      NULL, false, true, &aux->register_path) ||
                  !minic_machine_builder_create_block(&sel.builder,
                      NULL, false, true, &aux->stack_path) ||
                  !minic_machine_builder_create_block(&sel.builder,
                      NULL, false, true, &aux->join)) goto cleanup;
          }
        }
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
            if (!a64_core_instruction(&sel, ins, ins_id)) {
                if (getenv("MINIC_M1_TRACE") != NULL)
                    (void)fprintf(stderr,
                        "MINIC_A64_MIR_REJECT function=%s block=%zu instruction=%zu kind=%d phase=core_selection\n",
                        ctx->name, i, (size_t)ins_id, (int)ins->kind);
                goto cleanup;
            }
        }
        minic_machine_builder_clear_origin(&sel.builder);
        if (!a64_terminate(&sel, i)) {
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                    "MINIC_A64_MIR_REJECT function=%s block=%zu phase=terminator kind=%d\n",
                    ctx->name, i, (int)block->terminator.kind);
            goto cleanup;
        }
    }
    ok = a64_derive_generated_features(&sel) &&
         minic_machine_gnu_asm_preflight(machine, &sel.policy);
cleanup:
    free(sel.blocks);
    free(sel.false_blocks);
    free(sel.true_blocks);
    free(sel.reachable);
    free(sel.goto_emit);
    free(sel.goto_continue);
    free(sel.variadic_blocks);
    if (!ok) minic_machine_function_destroy(machine);
    return ok;
}
