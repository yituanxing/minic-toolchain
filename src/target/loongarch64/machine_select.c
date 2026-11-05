#include "target/loongarch64/machine_select.h"
#include "target/loongarch64/machine_opcode.h"
#include "target/loongarch64/machine_source_asm.h"
#include "target/loongarch64/machine_target_state.h"
#include "target/loongarch64/frame_emit.h"
#include "target/common/core_block_walk.h"
#include "target/common/core_numeric_shape.h"
#include "target/common/core_abi_value_shape.h"
#include "target/common/core_extended_float_runtime.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/elf_module_symbol_info.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"
#include "core/symbol_identity.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    LOONG_ZERO = 0U, LOONG_RA = 1U, LOONG_SP = 3U,
    LOONG_A0 = 4U, LOONG_T0 = 12U, LOONG_T1 = 13U,
    LOONG_T2 = 14U, LOONG_T3 = 15U, LOONG_T4 = 16U,
    LOONG_T5 = 17U, LOONG_S0 = 23U
};

typedef struct LoongSelector {
    MinicMachineBuilder builder;
    MinicMachineGNUAsmPolicy policy;
    const MinicDirectLP64BlockContext *ctx;
    size_t *block_ids;
    size_t *aux_true;
    size_t *aux_false;
    /* Each dynamic alloca owns two adjacent machine blocks: overflow trap
     * and valid continuation. This is CFG, never a private text label. */
    size_t *alloc_trap;
    size_t *alloc_continue;
    /* A double-to-unsigned64 conversion has two real ordered conversion
     * paths. Three auxiliary MIR blocks replace the old textual labels. */
    size_t *u64_high;
    size_t *u64_low;
    size_t *u64_done;
    /* A record copy's counted-loop/done blocks are allocated in Core
     * instruction order, before subsequent alloca/FP branches. Runtime
     * appending of these blocks would break physical fallthrough adjacency. */
    size_t *copy_loop;
    size_t *copy_done;
    bool *reachable;
} LoongSelector;

static bool loong_local_frame_address(LoongSelector *,
    unsigned int reg, size_t offset);
static bool loong_branch(LoongSelector *, uint32_t, size_t, bool, bool);
static bool loong_bstrpick_d(MinicMachineBuilder *, unsigned int, unsigned int);

static MinicMachineRegister loong_reg(unsigned int number) {
    return (MinicMachineRegister){number, 64U,
        MINIC_MACHINE_REGISTER_GENERAL};
}

static MinicMachineRegister loong_freg(unsigned int number,
    unsigned int width) {
    return (MinicMachineRegister){number, (uint16_t)width,
        MINIC_MACHINE_REGISTER_FLOAT};
}

static MinicMachineOperand loong_gp(unsigned int number,
    unsigned int access) {
    MinicMachineOperand o = {0};
    o.kind = MINIC_MACHINE_OPERAND_REGISTER;
    o.value.physical.reg = loong_reg(number);
    o.value.physical.access = access;
    return o;
}

static MinicMachineOperand loong_fp(unsigned int number, unsigned int width,
    unsigned int access) {
    MinicMachineOperand o = {0};
    o.kind = MINIC_MACHINE_OPERAND_REGISTER;
    o.value.physical.reg = loong_freg(number, width);
    o.value.physical.access = access;
    return o;
}

static MinicMachineOperand loong_imm(int64_t value) {
    MinicMachineOperand o = {0};
    o.kind = MINIC_MACHINE_OPERAND_INTEGER;
    o.value.integer = value;
    return o;
}

static MinicMachineOperand loong_address(unsigned int base, int64_t offset,
    uint32_t mode) {
    MinicMachineOperand o = {0};
    o.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    o.value.address.target_mode = mode;
    o.value.address.has_base = true;
    o.value.address.base = loong_reg(base);
    o.value.address.scale = 1U;
    o.value.address.displacement = offset;
    return o;
}

static bool loong_selected(MinicMachineBuilder *builder,
    uint32_t opcode, const MinicMachineOperand *ops, size_t count) {
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineOpcodeContract contract = {0};
    if (builder == NULL || builder->function == NULL ||
        !minic_loongarch64_machine_gnu_asm_policy_initialize(
            builder->function->target, &policy) ||
        policy.verify.lookup_opcode == NULL ||
        !policy.verify.lookup_opcode(builder->function->target, opcode,
            &contract, policy.context) ||
        contract.operand_count != count) return false;
    return minic_machine_builder_emit_selected(builder, opcode,
        contract.required_effects, ops, count,
        contract.implicit_count != 0U ? contract.implicit : NULL,
        contract.implicit_count);
}

static bool loong_selected_volatile(MinicMachineBuilder *builder,
    uint32_t opcode, const MinicMachineOperand *ops, size_t count,
    bool is_volatile) {
    MinicMachineGNUAsmPolicy policy = {0};
    MinicMachineOpcodeContract contract = {0};
    unsigned int effects;
    if (!is_volatile) return loong_selected(builder, opcode, ops, count);
    if (builder == NULL || builder->function == NULL ||
        !minic_loongarch64_machine_gnu_asm_policy_initialize(
            builder->function->target, &policy) ||
        policy.verify.lookup_opcode == NULL ||
        !policy.verify.lookup_opcode(builder->function->target, opcode,
            &contract, policy.context) || contract.operand_count != count)
        return false;
    effects = contract.required_effects | MINIC_MACHINE_EFFECT_VOLATILE;
    if ((effects & ~contract.permitted_effects) != 0U) return false;
    return minic_machine_builder_emit_selected(builder, opcode, effects,
        ops, count, contract.implicit_count != 0U ? contract.implicit : NULL,
        contract.implicit_count);
}

static bool loong_rr(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int rd, unsigned int rs) {
    const MinicMachineOperand ops[] = {
        loong_gp(rd, MINIC_MACHINE_REGISTER_WRITE),
        loong_gp(rs, MINIC_MACHINE_REGISTER_READ)
    };
    return loong_selected(b, opcode, ops, 2U);
}

static bool loong_ri(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int rd, int64_t immediate) {
    const MinicMachineOperand ops[] = {
        loong_gp(rd, MINIC_MACHINE_REGISTER_WRITE), loong_imm(immediate)
    };
    return loong_selected(b, opcode, ops, 2U);
}

static bool loong_rrr(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int rd, unsigned int lhs, unsigned int rhs) {
    const MinicMachineOperand ops[] = {
        loong_gp(rd, MINIC_MACHINE_REGISTER_WRITE),
        loong_gp(lhs, MINIC_MACHINE_REGISTER_READ),
        loong_gp(rhs, MINIC_MACHINE_REGISTER_READ)
    };
    return loong_selected(b, opcode, ops, 3U);
}

static bool loong_rri(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int rd, unsigned int src, int64_t immediate) {
    const MinicMachineOperand ops[] = {
        loong_gp(rd, MINIC_MACHINE_REGISTER_WRITE),
        loong_gp(src, MINIC_MACHINE_REGISTER_READ), loong_imm(immediate)
    };
    return loong_selected(b, opcode, ops, 3U);
}

/* A local value has a stable S0 base after a dynamic alloca; outgoing call
 * slots, by contrast, remain SP-relative. No general-purpose register
 * allocator or ISA-independent frame address arithmetic is introduced. */
static bool loong_memory(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int reg, unsigned int base, int64_t offset,
    uint32_t mode, bool read) {
    const MinicMachineOperand ops[] = {
        loong_gp(reg, read ? MINIC_MACHINE_REGISTER_WRITE
                           : MINIC_MACHINE_REGISTER_READ),
        loong_address(base, offset, mode)
    };
    return loong_selected(b, opcode, ops, 2U);
}

static bool loong_memory_volatile(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int reg, unsigned int base, int64_t offset,
    bool read, bool vol) {
    const MinicMachineOperand ops[] = {
        loong_gp(reg, read ? MINIC_MACHINE_REGISTER_WRITE :
                             MINIC_MACHINE_REGISTER_READ),
        loong_address(base, offset, MINIC_LOONG_ADDRESS_IMM12)
    };
    return loong_selected_volatile(b, opcode, ops, 2U, vol);
}

static bool loong_frame_register(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int reg, size_t offset, bool dynamic_frame,
    unsigned int width, bool load) {
    const unsigned int base = dynamic_frame ? LOONG_S0 : LOONG_SP;
    const uint32_t opcode_ptr =
        (opcode == MINIC_LOONG_MACHINE_LD_D ? MINIC_LOONG_MACHINE_LDPTR_D :
         opcode == MINIC_LOONG_MACHINE_ST_D ? MINIC_LOONG_MACHINE_STPTR_D :
         opcode == MINIC_LOONG_MACHINE_LD_W ? MINIC_LOONG_MACHINE_LDPTR_W :
         opcode == MINIC_LOONG_MACHINE_ST_W ? MINIC_LOONG_MACHINE_STPTR_W : 0U);
    if (offset > MINIC_LARGE_FRAME_MAX) return false;
    if (offset <= 2047U)
        return loong_memory(b, opcode, reg, base, (int64_t)offset,
            MINIC_LOONG_ADDRESS_IMM12, load);
    if (opcode_ptr != 0U && offset <= 32764U && (offset & 3U) == 0U &&
        (width != 8U || (offset & 7U) == 0U))
        return loong_memory(b, opcode_ptr, reg, base, (int64_t)offset,
            MINIC_LOONG_ADDRESS_PTR14, load);
    /* Old LP64 frame lowering uses $t3 as large-offset address scratch.
     * Never silently overwrite a store value parked in that same register. */
    if (reg == LOONG_T3 || offset > INT64_MAX ||
        !loong_ri(b, MINIC_LOONG_MACHINE_LI_D, LOONG_T3, (int64_t)offset) ||
        !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D, LOONG_T3, base, LOONG_T3))
        return false;
    return loong_memory(b, opcode, reg, LOONG_T3, 0,
        MINIC_LOONG_ADDRESS_IMM12, load);
}

static bool loong_frame_memory(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int reg, size_t offset, bool dynamic_frame, bool load) {
    unsigned int width;
    switch (opcode) {
    case MINIC_LOONG_MACHINE_LD_B:
    case MINIC_LOONG_MACHINE_LD_BU:
    case MINIC_LOONG_MACHINE_ST_B: width = 1U; break;
    case MINIC_LOONG_MACHINE_LD_H:
    case MINIC_LOONG_MACHINE_LD_HU:
    case MINIC_LOONG_MACHINE_ST_H: width = 2U; break;
    case MINIC_LOONG_MACHINE_LD_W:
    case MINIC_LOONG_MACHINE_LD_WU:
    case MINIC_LOONG_MACHINE_ST_W: width = 4U; break;
    case MINIC_LOONG_MACHINE_LD_D:
    case MINIC_LOONG_MACHINE_ST_D: width = 8U; break;
    default: return false;
    }
    return loong_frame_register(b, opcode, reg, offset,
        dynamic_frame, width, load);
}

static bool loong_sp_adjust(MinicMachineBuilder *b, size_t frame,
    bool restore) {
    if (frame == 0U) return true;
    if (frame > MINIC_LARGE_FRAME_MAX) return false;
    if ((!restore && frame <= 2048U) ||
        (restore && frame <= 2047U))
        return loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D, LOONG_SP,
            LOONG_SP, restore ? (int64_t)frame : -(int64_t)frame);
    if (frame > INT64_MAX ||
        !loong_ri(b, MINIC_LOONG_MACHINE_LI_D, LOONG_T3, (int64_t)frame))
        return false;
    return loong_rrr(b, restore ? MINIC_LOONG_MACHINE_ADD_D :
        MINIC_LOONG_MACHINE_SUB_D, LOONG_SP, LOONG_SP, LOONG_T3);
}

static bool loong_frame_float(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int float_reg, unsigned int width, size_t offset,
    bool dynamic, bool load) {
    unsigned int base = dynamic ? LOONG_S0 : LOONG_SP;
    MinicMachineOperand ops[2] = {{0}};
    if (offset > MINIC_LARGE_FRAME_MAX) return false;
    if (offset > 2047U) {
        if (!loong_ri(b, MINIC_LOONG_MACHINE_LI_D,
                LOONG_T3, (int64_t)offset) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D,
                LOONG_T3, base, LOONG_T3)) return false;
        base = LOONG_T3;
        offset = 0U;
    }
    ops[0] = loong_fp(float_reg, width,
        load ? MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
    ops[1] = loong_address(base, (int64_t)offset,
        MINIC_LOONG_ADDRESS_IMM12);
    return loong_selected(b, opcode, ops, 2U);
}

bool minic_loongarch64_machine_select_prologue(MinicMachineBuilder *b,
    const MinicMachineLP64FrameContract *frame) {
    const bool dynamic = frame != NULL && frame->has_dynamic_stack;
    const size_t saved_s0 = frame != NULL
        ? frame->save_offset + (frame->has_calls ? 8U : 0U) : 0U;
    size_t i;
    if (b == NULL || frame == NULL || frame->frame_alignment < 16U ||
        (frame->frame_alignment & (frame->frame_alignment - 1U)) != 0U ||
        saved_s0 < frame->save_offset) return false;
    if (frame->frame_alignment > 16U) {
        if (!dynamic || frame->saved_entry_sp_offset == SIZE_MAX ||
            frame->frame_alignment - 1U > 2047U ||
            frame->frame_alignment > INT64_MAX ||
            !loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T0, LOONG_SP) ||
            !loong_sp_adjust(b, frame->frame_size, false) ||
            !loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T1, LOONG_S0) ||
            !loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D, LOONG_S0,
                LOONG_SP, (int64_t)frame->frame_alignment - 1) ||
            !loong_ri(b, MINIC_LOONG_MACHINE_LI_D, LOONG_T3,
                -(int64_t)frame->frame_alignment) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_S0, LOONG_S0, LOONG_T3) ||
            !loong_frame_memory(b, MINIC_LOONG_MACHINE_ST_D,
                LOONG_T0, frame->saved_entry_sp_offset, true, false) ||
            !loong_frame_memory(b, MINIC_LOONG_MACHINE_ST_D,
                LOONG_T1, saved_s0, true, false)) return false;
    } else {
        if (!loong_sp_adjust(b, frame->frame_size, false)) return false;
        if (dynamic &&
            (!loong_frame_memory(b, MINIC_LOONG_MACHINE_ST_D,
                LOONG_S0, saved_s0, false, false) ||
             !loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                LOONG_S0, LOONG_SP))) return false;
    }
    if (frame->has_calls &&
        !loong_frame_memory(b, MINIC_LOONG_MACHINE_ST_D,
            LOONG_RA, frame->save_offset, dynamic, false)) return false;
    if (frame->has_indirect_return &&
        !loong_frame_memory(b, MINIC_LOONG_MACHINE_ST_D,
            LOONG_A0, frame->indirect_return_offset, dynamic, false)) return false;
    if (frame->variadic_count != 0U) {
        if (frame->variadic_save_base > SIZE_MAX - 56U) return false;
        for (i = 0U; i < 8U; ++i)
            if (!loong_frame_memory(b, MINIC_LOONG_MACHINE_ST_D,
                    LOONG_A0 + (unsigned int)i,
                    frame->variadic_save_base + i * 8U, dynamic, false))
                return false;
    }
    return true;
}

bool minic_loongarch64_machine_select_epilogue(MinicMachineBuilder *b,
    const MinicMachineLP64FrameContract *frame) {
    const bool dynamic = frame != NULL && frame->has_dynamic_stack;
    const size_t saved_s0 = frame != NULL
        ? frame->save_offset + (frame->has_calls ? 8U : 0U) : 0U;
    if (b == NULL || frame == NULL || saved_s0 < frame->save_offset ||
        (frame->has_calls &&
         !loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
             LOONG_RA, frame->save_offset, dynamic, true))) return false;
    if (frame->frame_alignment > 16U) {
        if (!dynamic || frame->saved_entry_sp_offset == SIZE_MAX ||
            !loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
                LOONG_T0, frame->saved_entry_sp_offset, true, true) ||
            !loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
                LOONG_T1, saved_s0, true, true) ||
            !loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_SP, LOONG_T0) ||
            !loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_S0, LOONG_T1))
            return false;
    } else {
        if (dynamic &&
            (!loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_SP, LOONG_S0) ||
             !loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
                LOONG_S0, saved_s0, false, true))) return false;
        if (!loong_sp_adjust(b, frame->frame_size, true)) return false;
    }
    return loong_selected(b, MINIC_LOONG_MACHINE_RET, NULL, 0U);
}

static bool loong_scalar_type(const MinicCoreFunction *fn,
    MinicCoreTypeId id, size_t *bytes, bool *sign, bool *pointer) {
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || bytes == NULL ||
        sign == NULL || pointer == NULL) return false;
    type = minic_core_type_get(fn->type_context,
        minic_core_type_unqualified(fn->type_context, id));
    if (type == NULL || !type->has_layout) return false;
    *pointer = type->kind == MINIC_CORE_TYPE_POINTER;
    if (*pointer) {
        *bytes = 8U;
        *sign = false;
        return true;
    }
    return type->kind == MINIC_CORE_TYPE_INTEGER &&
        minic_lp64_core_integer_width(fn->type_context, id, bytes, sign) &&
        (*bytes == 1U || *bytes == 2U || *bytes == 4U || *bytes == 8U);
}

/* A Core 128-bit integer occupies two little-endian 64-bit frame words.
 * Its physical representation is an LP64 frame/ABI fact, not a C type. */
static bool loong_integer_pair(const MinicCoreFunction *fn,
    MinicCoreTypeId id, bool *is_signed) {
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL) return false;
    type = minic_core_type_get(fn->type_context,
        minic_core_type_unqualified(fn->type_context, id));
    if (type == NULL || type->kind != MINIC_CORE_TYPE_INTEGER ||
        !type->has_layout || type->size != 16U ||
        type->value.integer.bit_width != 128U) return false;
    if (is_signed != NULL) *is_signed = type->value.integer.is_signed;
    return true;
}

static bool loong_wide_slot(LoongSelector *sel, MinicCoreValueId value,
    size_t *offset) {
    return value < sel->ctx->fn->value_count &&
        minic_lp64_frame_plan_wide_slot(sel->ctx->frame_plan, value, offset);
}

static bool loong_wide_word(LoongSelector *sel, uint32_t opcode,
    unsigned int reg, size_t offset, bool load) {
    return loong_frame_memory(&sel->builder, opcode, reg, offset,
        sel->ctx->machine_frame->has_dynamic_stack, load);
}

static bool loong_wide_load(LoongSelector *sel, MinicCoreValueId value,
    unsigned int low_reg, unsigned int high_reg) {
    size_t offset;
    return loong_wide_slot(sel, value, &offset) &&
        loong_wide_word(sel, MINIC_LOONG_MACHINE_LD_D,
            low_reg, offset, true) &&
        loong_wide_word(sel, MINIC_LOONG_MACHINE_LD_D,
            high_reg, offset + 8U, true);
}

static bool loong_wide_store(LoongSelector *sel, MinicCoreValueId value,
    unsigned int low_reg, unsigned int high_reg) {
    size_t offset;
    return loong_wide_slot(sel, value, &offset) &&
        loong_wide_word(sel, MINIC_LOONG_MACHINE_ST_D,
            low_reg, offset, false) &&
        loong_wide_word(sel, MINIC_LOONG_MACHINE_ST_D,
            high_reg, offset + 8U, false);
}

/* The Core verifier has already established operand/value identities. A
 * second checked lookup prevents an uninitialized frame slot from becoming
 * an invented hardware address in the selected instruction stream. */
static bool loong_value_offset(const LoongSelector *sel,
    MinicCoreValueId value, size_t *offset) {
    return sel != NULL && sel->ctx != NULL && sel->ctx->fn != NULL &&
        value < sel->ctx->fn->value_count &&
        minic_lp64_frame_plan_value_slot(sel->ctx->frame_plan, value, offset);
}

static bool loong_load_value(LoongSelector *sel, MinicCoreValueId value,
    unsigned int reg, bool force_64) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    size_t bytes, offset;
    bool sign, pointer;
    if (!loong_value_offset(sel, value, &offset) ||
        !loong_scalar_type(fn, fn->values[value].core_type_id,
            &bytes, &sign, &pointer)) return false;
    (void)sign;
    (void)pointer;
    return loong_frame_memory(&sel->builder,
        (bytes == 8U || force_64) ? MINIC_LOONG_MACHINE_LD_D :
        MINIC_LOONG_MACHINE_LD_W, reg, offset,
        sel->ctx->machine_frame->has_dynamic_stack, true);
}

/* Byte-exact record transport. The source and destination pointer registers
 * remain distinct, so both independently qualified volatile access sides
 * receive their own effect flags. Large objects use real auxiliary Machine
 * blocks and a counted loop; no source-assembly private labels are emitted. */
static bool loong_copy_record_bytes(LoongSelector *sel,
    const MinicCoreInstruction *origin, size_t bytes,
    bool source_volatile, bool destination_volatile) {
    MinicMachineBuilder *b = &sel->builder;
    size_t byte;
    size_t loop, done, before;
    if (bytes == 0U || bytes > 16U * 1024U * 1024U) return false;
    if (bytes < 8U) {
        for (byte = 0U; byte < bytes; ++byte) {
            if (!loong_memory_volatile(b, MINIC_LOONG_MACHINE_LD_BU,
                    LOONG_T0, LOONG_T2, 0, true, source_volatile) ||
                !loong_memory_volatile(b, MINIC_LOONG_MACHINE_ST_B,
                    LOONG_T0, LOONG_T1, 0, false,
                    destination_volatile) ||
                !loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D,
                    LOONG_T2, LOONG_T2, 1) ||
                !loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D,
                    LOONG_T1, LOONG_T1, 1)) return false;
        }
        return true;
    }
    if (!b->has_position) return false;
    before = b->current_block;
    if (origin != NULL) {
        const MinicCoreFunction *fn = sel->ctx->fn;
        const size_t id = (size_t)(origin - fn->instructions);
        if (id >= fn->instruction_count) return false;
        loop = sel->copy_loop[id];
        done = sel->copy_done[id];
        if (loop == SIZE_MAX || done == SIZE_MAX) return false;
    } else if (!minic_machine_builder_create_block(b, NULL, false,
                   true, &loop) ||
               !minic_machine_builder_create_block(b, NULL, false,
                   true, &done) ||
               !minic_machine_builder_position_at(b, before)) return false;
    if (!loong_ri(b, MINIC_LOONG_MACHINE_LI_D, LOONG_T3,
            (int64_t)bytes) ||
        !loong_branch(sel, MINIC_LOONG_MACHINE_B, loop, false, false))
        return false;
    if (!minic_machine_builder_position_at(b, loop) ||
        !loong_memory_volatile(b, MINIC_LOONG_MACHINE_LD_BU,
            LOONG_T0, LOONG_T2, 0, true, source_volatile) ||
        !loong_memory_volatile(b, MINIC_LOONG_MACHINE_ST_B,
            LOONG_T0, LOONG_T1, 0, false, destination_volatile) ||
        !loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D,
            LOONG_T2, LOONG_T2, 1) ||
        !loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D,
            LOONG_T1, LOONG_T1, 1) ||
        !loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D,
            LOONG_T3, LOONG_T3, -1) ||
        !loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T0, LOONG_T3))
        return false;
    if (!minic_machine_builder_add_successor(b, done) ||
        !loong_branch(sel, MINIC_LOONG_MACHINE_BNEZ, loop, true, false) ||
        !minic_machine_builder_set_fallthrough(b, done) ||
        !minic_machine_builder_position_at(b, done)) return false;
    return true;
}

static bool loong_store_value(LoongSelector *sel,
    const MinicCoreInstruction *ins, unsigned int reg, bool force_64) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    size_t bytes, offset;
    bool sign, pointer;
    if (ins->result == MINIC_CORE_VALUE_INVALID ||
        !loong_value_offset(sel, ins->result, &offset) ||
        !loong_scalar_type(fn, ins->core_type_id,
            &bytes, &sign, &pointer)) return false;
    (void)sign;
    (void)pointer;
    return loong_frame_memory(&sel->builder,
        (bytes == 8U || force_64) ? MINIC_LOONG_MACHINE_ST_D :
        MINIC_LOONG_MACHINE_ST_W, reg, offset,
        sel->ctx->machine_frame->has_dynamic_stack, false);
}

/* Exactly the sub-word normalization sequence used by the old Loong
 * emitter; the width is a Core physical layout fact, not a C source type. */
static bool loong_normalize_integer(MinicMachineBuilder *b,
    size_t bytes, bool sign) {
    switch (bytes) {
    case 1U:
        return sign
            ? loong_rri(b, MINIC_LOONG_MACHINE_SLLI_W, LOONG_T0, LOONG_T0, 24) &&
              loong_rri(b, MINIC_LOONG_MACHINE_SRAI_W, LOONG_T0, LOONG_T0, 24)
            : loong_rri(b, MINIC_LOONG_MACHINE_ANDI, LOONG_T0, LOONG_T0, 255);
    case 2U:
        if (sign)
            return loong_rri(b, MINIC_LOONG_MACHINE_SLLI_W,
                       LOONG_T0, LOONG_T0, 16) &&
                   loong_rri(b, MINIC_LOONG_MACHINE_SRAI_W,
                       LOONG_T0, LOONG_T0, 16);
        {
            const MinicMachineOperand operands[] = {
                loong_gp(LOONG_T0, MINIC_MACHINE_REGISTER_WRITE),
                loong_gp(LOONG_T0, MINIC_MACHINE_REGISTER_READ),
                loong_imm(15), loong_imm(0)
            };
            return loong_selected(b, MINIC_LOONG_MACHINE_BSTRPICK_W,
                operands, 4U);
        }
    case 4U:
    case 8U:
        return true;
    default:
        return false;
    }
}

/* Exact physical lowering of existing GNU checked-arithmetic Core: the
 * result-address pointer receives the truncated value, and the returned SSA
 * boolean signals loss of mathematical representability. Common performs
 * the operand/result shape proofs; no C type interpretation occurs here. */
static bool loong_overflow_store(LoongSelector *sel,
    const MinicCoreInstruction *ins, bool wide, unsigned int result_reg,
    unsigned int flag_reg) {
    MinicMachineBuilder *b = &sel->builder;
    return loong_memory(b, wide ? MINIC_LOONG_MACHINE_ST_D :
        MINIC_LOONG_MACHINE_ST_W, result_reg, LOONG_T2, 0,
        MINIC_LOONG_ADDRESS_IMM12, false) &&
        loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T0, flag_reg) &&
        loong_store_value(sel, ins, LOONG_T0, false);
}

static bool loong_overflow_inputs(LoongSelector *sel,
    const MinicCoreInstruction *ins, bool wide_left, bool wide_right) {
    return loong_load_value(sel, ins->value.integer_overflow.left,
               LOONG_T0, wide_left) &&
           loong_load_value(sel, ins->value.integer_overflow.right,
               LOONG_T1, wide_right) &&
           loong_load_value(sel,
               ins->value.integer_overflow.result_address, LOONG_T2, true);
}

static bool loong_select_integer_overflow(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    MinicMachineBuilder *b = &sel->builder;
    const int op = ins->value.integer_overflow.operator_kind;
    size_t width;
    bool is_signed;
    if (!minic_core_integer_overflow_shape(fn, ins, &width, &is_signed)) {
        bool signed_left, signed_right, signed_result;
        bool wide_left;
        if (minic_core_integer_overflow_i32_u32_to_i32_shape(
                fn, ins, &signed_left)) {
            if (!loong_overflow_inputs(sel, ins, false, false) ||
                !(signed_left ? loong_bstrpick_d(b, LOONG_T1, 31U) :
                               loong_bstrpick_d(b, LOONG_T0, 31U)) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D,
                    LOONG_T3, LOONG_T0, LOONG_T1) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_W,
                    LOONG_T0, LOONG_T3, LOONG_ZERO) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                    LOONG_T4, LOONG_T3, LOONG_T0) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T4, LOONG_ZERO, LOONG_T4)) return false;
            return loong_overflow_store(sel, ins, false,
                LOONG_T0, LOONG_T4);
        }
        if (minic_core_integer_overflow_mixed_multiply_i32_shape(fn, ins,
                &signed_left, &signed_right, &signed_result)) {
            if (!loong_overflow_inputs(sel, ins, false, false) ||
                (!signed_left && !loong_bstrpick_d(b, LOONG_T0, 31U)) ||
                (!signed_right && !loong_bstrpick_d(b, LOONG_T1, 31U)) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_MUL_D,
                    LOONG_T3, LOONG_T0, LOONG_T1) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_W,
                    LOONG_T0, LOONG_T3, LOONG_ZERO) ||
                !(signed_result
                    ? loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                        LOONG_T4, LOONG_T3, LOONG_T0)
                    : loong_rri(b, MINIC_LOONG_MACHINE_SRLI_D,
                        LOONG_T4, LOONG_T3, 32)) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T4, LOONG_ZERO, LOONG_T4)) return false;
            return loong_overflow_store(sel, ins, false,
                LOONG_T0, LOONG_T4);
        }
        if (minic_core_integer_overflow_u64_u32_to_i32_shape(
                fn, ins, &wide_left)) {
            if (!loong_overflow_inputs(sel, ins, wide_left, !wide_left) ||
                !(wide_left ? loong_bstrpick_d(b, LOONG_T1, 31U) :
                               loong_bstrpick_d(b, LOONG_T0, 31U)) ||
                !loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                    LOONG_T3, LOONG_T0) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D,
                    LOONG_T0, LOONG_T0, LOONG_T1) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T3, LOONG_T0, LOONG_T3) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_W,
                    LOONG_T4, LOONG_T0, LOONG_ZERO) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                    LOONG_T5, LOONG_T4, LOONG_T0) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T5, LOONG_ZERO, LOONG_T5) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                    LOONG_T3, LOONG_T3, LOONG_T5)) return false;
            return loong_overflow_store(sel, ins, false,
                LOONG_T4, LOONG_T3);
        }
        if (minic_core_integer_overflow_mixed_add_i32_shape(
                fn, ins, &signed_left)) {
            if (!loong_overflow_inputs(sel, ins, true, true) ||
                !(signed_left
                    ? loong_rrr(b, MINIC_LOONG_MACHINE_ADD_W,
                          LOONG_T0, LOONG_T0, LOONG_ZERO) &&
                      loong_rri(b, MINIC_LOONG_MACHINE_SRAI_D,
                          LOONG_T3, LOONG_T0, 63)
                    : loong_rrr(b, MINIC_LOONG_MACHINE_ADD_W,
                          LOONG_T1, LOONG_T1, LOONG_ZERO) &&
                      loong_rri(b, MINIC_LOONG_MACHINE_SRAI_D,
                          LOONG_T3, LOONG_T1, 63)) ||
                !loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                    LOONG_T4, LOONG_T0) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D,
                    LOONG_T0, LOONG_T0, LOONG_T1) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T5, LOONG_T0, LOONG_T4) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D,
                    LOONG_T3, LOONG_T3, LOONG_T5) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_W,
                    LOONG_T4, LOONG_T0, LOONG_ZERO) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                    LOONG_T5, LOONG_T4, LOONG_T0) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T5, LOONG_ZERO, LOONG_T5) ||
                !loong_rri(b, MINIC_LOONG_MACHINE_SRAI_D,
                    LOONG_T4, LOONG_T4, 63) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                    LOONG_T3, LOONG_T3, LOONG_T4) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T3, LOONG_ZERO, LOONG_T3) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                    LOONG_T3, LOONG_T3, LOONG_T5)) return false;
            return loong_overflow_store(sel, ins, false,
                LOONG_T0, LOONG_T3);
        }
        return false;
    }
    if (width != 4U && width != 8U) return false;
    if (!loong_overflow_inputs(sel, ins, width == 8U, width == 8U) ||
        (width == 4U && !is_signed &&
         (!loong_bstrpick_d(b, LOONG_T0, 31U) ||
          !loong_bstrpick_d(b, LOONG_T1, 31U)))) return false;
    if (op == MINIC_CORE_INTEGER_OVERFLOW_ADD ||
        op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
        const bool sub = op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT;
        if (!loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T3, LOONG_T0) ||
            !loong_rrr(b, width == 8U ?
                (sub ? MINIC_LOONG_MACHINE_SUB_D : MINIC_LOONG_MACHINE_ADD_D) :
                (!is_signed && !sub ? MINIC_LOONG_MACHINE_ADD_D :
                    sub ? MINIC_LOONG_MACHINE_SUB_W : MINIC_LOONG_MACHINE_ADD_W),
                LOONG_T0, LOONG_T0, LOONG_T1)) return false;
        if (is_signed) {
            if (!(sub
                    ? loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                          LOONG_T4, LOONG_T3, LOONG_T1)
                    : loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                          LOONG_T4, LOONG_T1, LOONG_T0)) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                    LOONG_T3, LOONG_T3, LOONG_T0) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                    LOONG_T4, LOONG_T3, LOONG_T4) ||
                (width == 4U && !loong_rri(b,
                    MINIC_LOONG_MACHINE_SLLI_W, LOONG_T4, LOONG_T4, 0)) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLT,
                    LOONG_T4, LOONG_T4, LOONG_ZERO)) return false;
        } else if (!(sub
                ? loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                      LOONG_T4, LOONG_T3, LOONG_T1)
                : width == 8U
                    ? loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                          LOONG_T4, LOONG_T0, LOONG_T3)
                    : loong_rri(b, MINIC_LOONG_MACHINE_SRLI_D,
                          LOONG_T4, LOONG_T0, 32))) return false;
    } else if (op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
        if (width == 8U) {
            if (!loong_rrr(b, is_signed ? MINIC_LOONG_MACHINE_MULH_D :
                    MINIC_LOONG_MACHINE_MULH_DU,
                    LOONG_T3, LOONG_T0, LOONG_T1) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_MUL_D,
                    LOONG_T0, LOONG_T0, LOONG_T1) ||
                (is_signed &&
                 (!loong_rri(b, MINIC_LOONG_MACHINE_SRAI_D,
                      LOONG_T4, LOONG_T0, 63) ||
                  !loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                      LOONG_T3, LOONG_T3, LOONG_T4))) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T4, LOONG_ZERO, LOONG_T3)) return false;
        } else {
            if (!loong_rrr(b, MINIC_LOONG_MACHINE_MUL_D,
                    LOONG_T3, LOONG_T0, LOONG_T1) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_W,
                    LOONG_T0, LOONG_T3, LOONG_ZERO) ||
                !(is_signed
                    ? loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                          LOONG_T4, LOONG_T3, LOONG_T0)
                    : loong_rri(b, MINIC_LOONG_MACHINE_SRLI_D,
                          LOONG_T4, LOONG_T3, 32)) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T4, LOONG_ZERO, LOONG_T4)) return false;
        }
    } else return false;
    return loong_overflow_store(sel, ins, width == 8U,
        LOONG_T0, LOONG_T4);
}

static bool loong_bstrpick_w(MinicMachineBuilder *b,
    unsigned int reg, unsigned int high) {
    const MinicMachineOperand ops[] = {
        loong_gp(reg, MINIC_MACHINE_REGISTER_WRITE),
        loong_gp(reg, MINIC_MACHINE_REGISTER_READ),
        loong_imm((int64_t)high), loong_imm(0)
    };
    return loong_selected(b, MINIC_LOONG_MACHINE_BSTRPICK_W, ops, 4U);
}

static bool loong_bstrpick_d(MinicMachineBuilder *b,
    unsigned int reg, unsigned int high) {
    const MinicMachineOperand ops[] = {
        loong_gp(reg, MINIC_MACHINE_REGISTER_WRITE),
        loong_gp(reg, MINIC_MACHINE_REGISTER_READ),
        loong_imm((int64_t)high), loong_imm(0)
    };
    return loong_selected(b, MINIC_LOONG_MACHINE_BSTRPICK_D, ops, 4U);
}

/* Wide shifts retain the old two-limb arithmetic, but select the 0..63
 * and 64..127 cases with physical GP masks. No textual private labels or
 * pseudo-branches occur inside MachineIR. Hardware variable shifts mask
 * their shift operand to six bits; the masks prevent the wrong limb case
 * from leaking into the final value, including the zero-shift boundary. */
static bool loong_select_wide_shift(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreValueId left = ins->value.binary.left;
    const MinicCoreValueId right = ins->value.binary.right;
    MinicMachineBuilder *b = &sel->builder;
    bool signed_pair, rhs_sign;
    size_t rhs_bytes;
    const bool left_shift =
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
    if (left >= fn->value_count || right >= fn->value_count ||
        !loong_integer_pair(fn, ins->core_type_id, &signed_pair) ||
        !minic_lp64_core_equal_unqualified(fn->type_context,
            ins->core_type_id, fn->values[left].core_type_id) ||
        !minic_lp64_core_integer_width(fn->type_context,
            fn->values[right].core_type_id, &rhs_bytes, &rhs_sign) ||
        (rhs_bytes != 1U && rhs_bytes != 2U &&
         rhs_bytes != 4U && rhs_bytes != 8U) ||
        !loong_wide_load(sel, left, LOONG_T0, LOONG_T1) ||
        !loong_load_value(sel, right, LOONG_T2, false)) return false;
    (void)rhs_sign;
    if (left_shift) {
        /* t4=original low, t5=original high, t3=cross-limb carry. */
        if (!loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T4, LOONG_T0) ||
            !loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T5, LOONG_T1) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                LOONG_T3, LOONG_ZERO, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SRL_D,
                LOONG_T3, LOONG_T4, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                LOONG_T0, LOONG_ZERO, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                LOONG_T0, LOONG_ZERO, LOONG_T0) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T3, LOONG_T3, LOONG_T0) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SLL_D,
                LOONG_T1, LOONG_T5, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T1, LOONG_T1, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SLL_D,
                LOONG_T0, LOONG_T4, LOONG_T2) ||
            !loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D,
                LOONG_T2, LOONG_T2, -64) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SLL_D,
                LOONG_T4, LOONG_T4, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SLT,
                LOONG_T5, LOONG_T2, LOONG_ZERO) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                LOONG_T5, LOONG_ZERO, LOONG_T5) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T0, LOONG_T0, LOONG_T5) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T1, LOONG_T1, LOONG_T5) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_NOR,
                LOONG_T5, LOONG_T5, LOONG_ZERO) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T4, LOONG_T4, LOONG_T5) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T1, LOONG_T1, LOONG_T4)) return false;
    } else {
        const uint32_t shr = signed_pair ? MINIC_LOONG_MACHINE_SRA_D :
                                              MINIC_LOONG_MACHINE_SRL_D;
        if (!loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T5, LOONG_T0) ||
            !loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T4, LOONG_T1) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                LOONG_T3, LOONG_ZERO, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SLL_D,
                LOONG_T3, LOONG_T4, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                LOONG_T0, LOONG_ZERO, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                LOONG_T0, LOONG_ZERO, LOONG_T0) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T3, LOONG_T3, LOONG_T0) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SRL_D,
                LOONG_T5, LOONG_T5, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T5, LOONG_T5, LOONG_T3) ||
            !loong_rrr(b, shr, LOONG_T1, LOONG_T4, LOONG_T2) ||
            !(signed_pair ? loong_rri(b, MINIC_LOONG_MACHINE_SRAI_D,
                    LOONG_T0, LOONG_T4, 63) :
                loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T0, LOONG_ZERO)) ||
            !loong_rri(b, MINIC_LOONG_MACHINE_SLTUI,
                LOONG_T3, LOONG_T2, 64) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                LOONG_T3, LOONG_ZERO, LOONG_T3) ||
            !loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D,
                LOONG_T2, LOONG_T2, -64) ||
            !loong_rrr(b, shr, LOONG_T4, LOONG_T4, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T5, LOONG_T5, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T1, LOONG_T1, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_NOR,
                LOONG_T3, LOONG_T3, LOONG_ZERO) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T4, LOONG_T4, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T0, LOONG_T0, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T0, LOONG_T0, LOONG_T1) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T5, LOONG_T5, LOONG_T4) ||
            !loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                LOONG_T1, LOONG_T0) ||
            !loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                LOONG_T0, LOONG_T5)) return false;
    }
    return loong_wide_store(sel, ins->result, LOONG_T0, LOONG_T1);
}

static bool loong_select_wide_integer(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    MinicMachineBuilder *b = &sel->builder;
    MinicCoreValueId lhs, rhs;
    bool signed_pair;
    uint32_t opcode;
    if ((ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
         ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) &&
        loong_integer_pair(fn, ins->core_type_id, NULL))
        return loong_select_wide_shift(sel, ins);
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ||
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT) {
        lhs = ins->value.operand;
        if (!loong_integer_pair(fn, ins->core_type_id, NULL) ||
            lhs >= fn->value_count ||
            !minic_lp64_core_equal_unqualified(fn->type_context,
                ins->core_type_id, fn->values[lhs].core_type_id) ||
            !loong_wide_load(sel, lhs, LOONG_T0, LOONG_T1)) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE) {
            if (!loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                    LOONG_T2, LOONG_ZERO, LOONG_T0) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T4, LOONG_ZERO, LOONG_T0) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                    LOONG_T1, LOONG_ZERO, LOONG_T1) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                    LOONG_T1, LOONG_T1, LOONG_T4) ||
                !loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                    LOONG_T0, LOONG_T2)) return false;
        } else if (!loong_rrr(b, MINIC_LOONG_MACHINE_NOR,
                       LOONG_T0, LOONG_T0, LOONG_ZERO) ||
                   !loong_rrr(b, MINIC_LOONG_MACHINE_NOR,
                       LOONG_T1, LOONG_T1, LOONG_ZERO)) return false;
        return loong_wide_store(sel, ins->result, LOONG_T0, LOONG_T1);
    }
    if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO) {
        lhs = ins->value.operand;
        if (lhs >= fn->value_count ||
            !loong_integer_pair(fn, fn->values[lhs].core_type_id, NULL) ||
            !loong_wide_load(sel, lhs, LOONG_T0, LOONG_T1) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T0, LOONG_T0, LOONG_T1) ||
            !loong_rri(b, MINIC_LOONG_MACHINE_SLTUI,
                LOONG_T0, LOONG_T0, 1)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    }
    lhs = ins->value.binary.left;
    rhs = ins->value.binary.right;
    if (lhs >= fn->value_count || rhs >= fn->value_count ||
        !loong_integer_pair(fn, fn->values[lhs].core_type_id,
            &signed_pair) ||
        !minic_lp64_core_equal_unqualified(fn->type_context,
            fn->values[lhs].core_type_id,
            fn->values[rhs].core_type_id) ||
        ((ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_LESS &&
          ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_EQUAL) &&
         !minic_lp64_core_equal_unqualified(fn->type_context,
            ins->core_type_id, fn->values[lhs].core_type_id)) ||
        !loong_wide_load(sel, lhs, LOONG_T0, LOONG_T1) ||
        !loong_wide_load(sel, rhs, LOONG_T2, LOONG_T3)) return false;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        if (!loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T4, LOONG_T0) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D,
                LOONG_T0, LOONG_T0, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                LOONG_T4, LOONG_T0, LOONG_T4) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D,
                LOONG_T1, LOONG_T1, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D,
                LOONG_T1, LOONG_T1, LOONG_T4)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        if (!loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                LOONG_T4, LOONG_T0, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                LOONG_T0, LOONG_T0, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                LOONG_T1, LOONG_T1, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D,
                LOONG_T1, LOONG_T1, LOONG_T4)) return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
            ? MINIC_LOONG_MACHINE_AND :
            ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
            ? MINIC_LOONG_MACHINE_XOR : MINIC_LOONG_MACHINE_OR;
        if (!loong_rrr(b, opcode, LOONG_T0, LOONG_T0, LOONG_T2) ||
            !loong_rrr(b, opcode, LOONG_T1, LOONG_T1, LOONG_T3))
            return false;
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        if (!loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                LOONG_T4, LOONG_T1, LOONG_T3) ||
            !loong_rri(b, MINIC_LOONG_MACHINE_SLTUI,
                LOONG_T4, LOONG_T4, 1) ||
            !loong_rrr(b, signed_pair ? MINIC_LOONG_MACHINE_SLT :
                    MINIC_LOONG_MACHINE_SLTU,
                LOONG_T1, LOONG_T1, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                LOONG_T0, LOONG_T0, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_AND,
                LOONG_T0, LOONG_T0, LOONG_T4) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T0, LOONG_T0, LOONG_T1)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                LOONG_T0, LOONG_T0, LOONG_T2) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_XOR,
                LOONG_T1, LOONG_T1, LOONG_T3) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T0, LOONG_T0, LOONG_T1) ||
            !loong_rri(b, MINIC_LOONG_MACHINE_SLTUI,
                LOONG_T0, LOONG_T0, 1)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    default: return false;
    }
    return loong_wide_store(sel, ins->result, LOONG_T0, LOONG_T1);
}

static bool loong_select_integer(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreValueId lhs = ins->value.binary.left;
    const MinicCoreValueId rhs = ins->value.binary.right;
    size_t lw, rw;
    bool ls, rs;
    uint32_t opcode = 0U;
    bool normalize = true;
    /* Keep the legacy TI integer semantic family together: wide frame
     * values never enter 32/64-bit scalar normalization or GP writeback. */
    if (((ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE ||
           ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT ||
           ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT ||
           ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT) &&
          loong_integer_pair(fn, ins->core_type_id, NULL)) ||
        ((ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD ||
          ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT ||
          ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY ||
          ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE ||
          ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER ||
          ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND ||
          ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR ||
          ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR ||
          ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS ||
          ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL) &&
         lhs < fn->value_count &&
         loong_integer_pair(fn, fn->values[lhs].core_type_id, NULL)) ||
        (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO &&
         ins->value.operand < fn->value_count &&
         loong_integer_pair(fn,
            fn->values[ins->value.operand].core_type_id, NULL)))
        return loong_select_wide_integer(sel, ins);
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT: {
        int64_t value = ins->value.integer_value;
        if (!minic_multi_direct_constant_integer_shape(sel->ctx->module,
                sel->ctx->target, fn, ins, &lw, &ls)) return false;
        if (lw != 8U) value = (int64_t)(int32_t)(uint32_t)value;
        if (!loong_ri(&sel->builder,
                lw == 8U ? MINIC_LOONG_MACHINE_LI_D :
                            MINIC_LOONG_MACHINE_LI_W, LOONG_T0, value) ||
            !loong_normalize_integer(&sel->builder, lw, ls)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        if (!minic_multi_direct_integer_unary_shape(fn, ins, &lw, &ls) ||
            !loong_load_value(sel, ins->value.operand, LOONG_T0, false) ||
            !(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_NEGATE
              ? loong_rrr(&sel->builder,
                    lw == 8U ? MINIC_LOONG_MACHINE_SUB_D :
                               MINIC_LOONG_MACHINE_SUB_W,
                    LOONG_T0, LOONG_ZERO, LOONG_T0)
              : loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_NOR,
                    LOONG_T0, LOONG_T0, LOONG_ZERO)) ||
            !loong_normalize_integer(&sel->builder, lw, ls)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        if (!minic_multi_direct_integer_binary_shape(fn, ins,
                &lw, &rw, &ls, &rs) ||
            !loong_load_value(sel, lhs, LOONG_T0, false) ||
            !loong_load_value(sel, rhs, LOONG_T1, false)) return false;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
            opcode = lw == 8U ? MINIC_LOONG_MACHINE_ADD_D :
                                MINIC_LOONG_MACHINE_ADD_W; break;
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
            opcode = lw == 8U ? MINIC_LOONG_MACHINE_SUB_D :
                                MINIC_LOONG_MACHINE_SUB_W; break;
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
            opcode = lw == 8U ? MINIC_LOONG_MACHINE_MUL_D :
                                MINIC_LOONG_MACHINE_MUL_W; break;
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
            opcode = lw == 8U ? (ls ? MINIC_LOONG_MACHINE_DIV_D :
                                     MINIC_LOONG_MACHINE_DIV_DU) :
                                (ls ? MINIC_LOONG_MACHINE_DIV_W :
                                     MINIC_LOONG_MACHINE_DIV_WU); break;
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
            opcode = lw == 8U ? (ls ? MINIC_LOONG_MACHINE_MOD_D :
                                     MINIC_LOONG_MACHINE_MOD_DU) :
                                (ls ? MINIC_LOONG_MACHINE_MOD_W :
                                     MINIC_LOONG_MACHINE_MOD_WU); break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
            opcode = MINIC_LOONG_MACHINE_AND; break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
            opcode = MINIC_LOONG_MACHINE_XOR; break;
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
            opcode = MINIC_LOONG_MACHINE_OR; break;
        default: return false;
        }
        if (!loong_rrr(&sel->builder, opcode,
                LOONG_T0, LOONG_T0, LOONG_T1) ||
            !loong_normalize_integer(&sel->builder, lw, ls)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (!minic_multi_direct_pointer_less_shape(fn, ins) ||
            !loong_load_value(sel, lhs, LOONG_T0, true) ||
            !loong_load_value(sel, rhs, LOONG_T1, true) ||
            !loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_SLTU,
                LOONG_T0, LOONG_T0, LOONG_T1)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!minic_multi_direct_integer_compare_shape(fn, ins,
                &lw, &rw, &ls, &rs)) {
            /* Pointer equality is a distinct Core comparison shape. */
            if (ins->kind != MINIC_CORE_INSTRUCTION_SCALAR_EQUAL ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !loong_scalar_type(fn, fn->values[lhs].core_type_id,
                    &lw, &ls, &normalize) || !normalize ||
                !loong_scalar_type(fn, fn->values[rhs].core_type_id,
                    &rw, &rs, &normalize) || !normalize) return false;
        }
        if (!loong_load_value(sel, lhs, LOONG_T0, false) ||
            !loong_load_value(sel, rhs, LOONG_T1, false)) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS) {
            if (!loong_rrr(&sel->builder, ls ? MINIC_LOONG_MACHINE_SLT :
                    MINIC_LOONG_MACHINE_SLTU,
                    LOONG_T0, LOONG_T0, LOONG_T1)) return false;
        } else if (!loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_XOR,
                       LOONG_T0, LOONG_T0, LOONG_T1) ||
                   !loong_rri(&sel->builder, MINIC_LOONG_MACHINE_SLTUI,
                       LOONG_T0, LOONG_T0, 1)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO: {
        bool pointer;
        if (!minic_multi_direct_scalar_zero_shape(fn, ins,
                &lw, &ls, &pointer) ||
            !loong_load_value(sel, ins->value.operand, LOONG_T0, pointer) ||
            !loong_rri(&sel->builder, MINIC_LOONG_MACHINE_SLTUI,
                LOONG_T0, LOONG_T0, 1)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
        unsigned int bits;
        if (!minic_core_integer_bitscan_shape(fn, ins, &bits) ||
            !loong_load_value(sel, ins->value.operand, LOONG_T0, false) ||
            (bits < 32U && !loong_bstrpick_w(&sel->builder,
                 LOONG_T0, bits - 1U)) ||
            !loong_rr(&sel->builder,
                ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ
                ? (bits == 64U ? MINIC_LOONG_MACHINE_CLZ_D :
                                 MINIC_LOONG_MACHINE_CLZ_W)
                : (bits == 64U ? MINIC_LOONG_MACHINE_CTZ_D :
                                 MINIC_LOONG_MACHINE_CTZ_W),
                LOONG_T0, LOONG_T0) ||
            (bits < 32U && ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ &&
             !loong_rri(&sel->builder, MINIC_LOONG_MACHINE_ADDI_W,
                LOONG_T0, LOONG_T0, (int64_t)bits - 32))) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
        const MinicCoreValueId left = ins->value.binary.left;
        const MinicCoreValueId right = ins->value.binary.right;
        if (!minic_multi_direct_integer_shift_shape(fn, ins,
                &lw, &rw, &ls, &rs) ||
            !loong_load_value(sel, left, LOONG_T0, false) ||
            (lw < 4U && !loong_normalize_integer(&sel->builder, lw, ls)) ||
            !loong_load_value(sel, right, LOONG_T1, false)) return false;
        if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT)
            opcode = lw == 8U ? MINIC_LOONG_MACHINE_SLL_D :
                                MINIC_LOONG_MACHINE_SLL_W;
        else if (ls)
            opcode = lw == 8U ? MINIC_LOONG_MACHINE_SRA_D :
                                MINIC_LOONG_MACHINE_SRA_W;
        else opcode = lw == 8U ? MINIC_LOONG_MACHINE_SRL_D :
                                    MINIC_LOONG_MACHINE_SRL_W;
        if (!loong_rrr(&sel->builder, opcode,
                LOONG_T0, LOONG_T0, LOONG_T1) ||
            !loong_normalize_integer(&sel->builder, lw, ls)) return false;
        return loong_store_value(sel, ins, LOONG_T0, false);
    }
    default:
        return false;
    }
}

static bool loong_float_register_op(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int dst, unsigned int src, unsigned int width) {
    const MinicMachineOperand ops[] = {
        loong_fp(dst, width, MINIC_MACHINE_REGISTER_WRITE),
        loong_fp(src, opcode == MINIC_LOONG_MACHINE_FCVT_D_S
            || opcode == MINIC_LOONG_MACHINE_FFINT_D_W
            ? 32U : opcode == MINIC_LOONG_MACHINE_FCVT_S_D ||
                      opcode == MINIC_LOONG_MACHINE_FTINTRZ_W_D
                ? 64U : width, MINIC_MACHINE_REGISTER_READ)
    };
    return loong_selected(b, opcode, ops, 2U);
}

static bool loong_float_from_gp(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int dst, unsigned int src) {
    const MinicMachineOperand ops[] = {
        loong_fp(dst, opcode == MINIC_LOONG_MACHINE_MOVGR2FR_W
            ? 32U : 64U, MINIC_MACHINE_REGISTER_WRITE),
        loong_gp(src, MINIC_MACHINE_REGISTER_READ)
    };
    return loong_selected(b, opcode, ops, 2U);
}

static bool loong_gp_from_float(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int dst, unsigned int src) {
    const MinicMachineOperand ops[] = {
        loong_gp(dst, MINIC_MACHINE_REGISTER_WRITE),
        loong_fp(src, opcode == MINIC_LOONG_MACHINE_MOVFR2GR_S
            ? 32U : 64U, MINIC_MACHINE_REGISTER_READ)
    };
    return loong_selected(b, opcode, ops, 2U);
}

static bool loong_float_binary_op(MinicMachineBuilder *b, uint32_t opcode,
    unsigned int width) {
    const MinicMachineOperand ops[] = {
        loong_fp(8U, width, MINIC_MACHINE_REGISTER_WRITE),
        loong_fp(8U, width, MINIC_MACHINE_REGISTER_READ),
        loong_fp(9U, width, MINIC_MACHINE_REGISTER_READ)
    };
    return loong_selected(b, opcode, ops, 3U);
}

static bool loong_float_binary_regs(MinicMachineBuilder *b,
    uint32_t opcode, unsigned int rd, unsigned int lhs, unsigned int rhs) {
    const MinicMachineOperand ops[] = {
        loong_fp(rd, 64U, MINIC_MACHINE_REGISTER_WRITE),
        loong_fp(lhs, 64U, MINIC_MACHINE_REGISTER_READ),
        loong_fp(rhs, 64U, MINIC_MACHINE_REGISTER_READ)
    };
    return loong_selected(b, opcode, ops, 3U);
}

static bool loong_float_compare(LoongSelector *sel, uint32_t opcode) {
    MinicMachineOperand ops[3] = {{0}};
    ops[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ops[0].value.physical.reg = (MinicMachineRegister){0U, 1U,
        MINIC_MACHINE_REGISTER_FLAGS};
    ops[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    ops[1] = loong_fp(8U, 64U, MINIC_MACHINE_REGISTER_READ);
    ops[2] = loong_fp(9U, 64U, MINIC_MACHINE_REGISTER_READ);
    if (!loong_selected(&sel->builder, opcode, ops, 3U)) return false;
    ops[0] = loong_gp(LOONG_T0, MINIC_MACHINE_REGISTER_WRITE);
    ops[1].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ops[1].value.physical.reg = (MinicMachineRegister){0U, 1U,
        MINIC_MACHINE_REGISTER_FLAGS};
    ops[1].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    return loong_selected(&sel->builder,
        MINIC_LOONG_MACHINE_MOVCF2GR, ops, 2U);
}

static bool loong_float_value(LoongSelector *sel, MinicCoreValueId value,
    unsigned int freg, unsigned int width, bool load) {
    size_t offset;
    if (!loong_value_offset(sel, value, &offset)) return false;
    return loong_frame_float(&sel->builder,
        load ? (width == 32U ? MINIC_LOONG_MACHINE_FLD_S :
                               MINIC_LOONG_MACHINE_FLD_D) :
               (width == 32U ? MINIC_LOONG_MACHINE_FST_S :
                               MINIC_LOONG_MACHINE_FST_D),
        freg, width, offset, sel->ctx->machine_frame->has_dynamic_stack, load);
}

static bool loong_select_float(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    MinicMachineBuilder *b = &sel->builder;
    uint32_t opcode;
    unsigned int width = 64U;
    const MinicCoreValueId lhs = ins->value.binary.left;
    const MinicCoreValueId rhs = ins->value.binary.right;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT: {
        const MinicCoreType *type = minic_core_type_get(fn->type_context,
            minic_core_type_unqualified(fn->type_context, ins->core_type_id));
        size_t offset;
        if (type == NULL || type->kind != MINIC_CORE_TYPE_FLOAT ||
            (type->value.floating.format != MINIC_CORE_FLOAT_BINARY64 &&
             type->value.floating.format != MINIC_CORE_FLOAT_BINARY32) ||
            !loong_value_offset(sel, ins->result, &offset) ||
            !loong_ri(b, MINIC_LOONG_MACHINE_LI_D,
                LOONG_T0, (int64_t)ins->value.floating_bits)) return false;
        return loong_frame_memory(b,
            type->value.floating.format == MINIC_CORE_FLOAT_BINARY64
                ? MINIC_LOONG_MACHINE_ST_D : MINIC_LOONG_MACHINE_ST_W,
            LOONG_T0, offset, sel->ctx->machine_frame->has_dynamic_stack,
            false);
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
        if (!minic_multi_direct_floating_shape(fn, ins, NULL, NULL) ||
            !loong_float_value(sel, lhs, 8U, 64U, true) ||
            !loong_float_value(sel, rhs, 9U, 64U, true)) return false;
        opcode = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
            ? MINIC_LOONG_MACHINE_FCMP_CEQ_D :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS
            ? MINIC_LOONG_MACHINE_FCMP_CLT_D : MINIC_LOONG_MACHINE_FCMP_CLE_D;
        return loong_float_compare(sel, opcode) &&
            loong_store_value(sel, ins, LOONG_T0, false);
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        width = ins->kind >= MINIC_CORE_INSTRUCTION_FLOAT_ADD &&
            ins->kind <= MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE ? 32U : 64U;
        if (!minic_multi_direct_floating_shape(fn, ins, NULL, NULL) ||
            !loong_float_value(sel, lhs, 8U, width, true) ||
            !loong_float_value(sel, rhs, 9U, width, true)) return false;
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
            opcode = MINIC_LOONG_MACHINE_FADD_D; break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
            opcode = MINIC_LOONG_MACHINE_FSUB_D; break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
            opcode = MINIC_LOONG_MACHINE_FMUL_D; break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
            opcode = MINIC_LOONG_MACHINE_FDIV_D; break;
        case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
            opcode = MINIC_LOONG_MACHINE_FADD_S; break;
        case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
            opcode = MINIC_LOONG_MACHINE_FSUB_S; break;
        case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
            opcode = MINIC_LOONG_MACHINE_FMUL_S; break;
        default: opcode = MINIC_LOONG_MACHINE_FDIV_S; break;
        }
        return loong_float_binary_op(b, opcode, width) &&
            loong_float_value(sel, ins->result, 8U, width, false);
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
        if (!minic_multi_direct_floating_shape(fn, ins, NULL, NULL)) return false;
        width = ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE
            ? 32U : 64U;
        if (!loong_float_value(sel, ins->value.operand, 8U,
                width, true)) return false;
        opcode = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE
            ? MINIC_LOONG_MACHINE_FNEG_D :
            ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT
                ? MINIC_LOONG_MACHINE_FCVT_S_D :
                    MINIC_LOONG_MACHINE_FCVT_D_S;
        width = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT
            ? 32U : 64U;
        return loong_float_register_op(b, opcode, 8U, 8U, width) &&
            loong_float_value(sel, ins->result, 8U, width, false);
    default:
        return false;
    }
}

/* LP64D provides only SIGNED hardware conversion. Cover every C-defined
 * scalar width explicitly; unsigned 64-bit is exact in FP64 by splitting
 * the source at 32 bits (convert each exact half, one final FP add). The
 * reverse conversion uses real auxiliary CFG blocks for the 2^63 range,
 * never an untracked assembler-local label or textual fallback. */
static bool loong_select_float_integer_cast(LoongSelector *sel,
    const MinicCoreInstruction *ins, MinicCoreInstructionId ins_id) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    MinicMachineBuilder *b = &sel->builder;
    size_t width;
    bool sign;
    if (!minic_multi_direct_floating_shape(fn, ins, &width, &sign) ||
        (width != 1U && width != 2U && width != 4U && width != 8U))
        return false;
    if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE) {
        if (!loong_load_value(sel, ins->value.operand, LOONG_T0, false) ||
            (width < 4U && !loong_normalize_integer(b, width, sign)))
            return false;
        if (width == 8U && !sign) {
            /* High 32 and low 32 each convert without loss; 2^32 itself
             * has one exact binary64 representation. */
            if (!loong_rri(b, MINIC_LOONG_MACHINE_SRLI_D,
                    LOONG_T1, LOONG_T0, 32) ||
                !loong_bstrpick_d(b, LOONG_T0, 31U) ||
                !loong_float_from_gp(b, MINIC_LOONG_MACHINE_MOVGR2FR_D,
                    8U, LOONG_T1) ||
                !loong_float_register_op(b, MINIC_LOONG_MACHINE_FFINT_D_L,
                    8U, 8U, 64U) ||
                !loong_float_from_gp(b, MINIC_LOONG_MACHINE_MOVGR2FR_D,
                    9U, LOONG_T0) ||
                !loong_float_register_op(b, MINIC_LOONG_MACHINE_FFINT_D_L,
                    9U, 9U, 64U) ||
                !loong_ri(b, MINIC_LOONG_MACHINE_LI_D,
                    LOONG_T2, INT64_C(0x41f0000000000000)) ||
                !loong_float_from_gp(b, MINIC_LOONG_MACHINE_MOVGR2FR_D,
                    10U, LOONG_T2) ||
                !loong_float_binary_regs(b, MINIC_LOONG_MACHINE_FMUL_D,
                    8U, 8U, 10U) ||
                !loong_float_binary_regs(b, MINIC_LOONG_MACHINE_FADD_D,
                    8U, 8U, 9U)) return false;
        } else if (width == 4U && !sign) {
            if (!loong_bstrpick_d(b, LOONG_T0, 31U) ||
                !loong_float_from_gp(b, MINIC_LOONG_MACHINE_MOVGR2FR_D,
                    8U, LOONG_T0) ||
                !loong_float_register_op(b, MINIC_LOONG_MACHINE_FFINT_D_L,
                    8U, 8U, 64U)) return false;
        } else {
            if (!loong_float_from_gp(b, width == 8U
                    ? MINIC_LOONG_MACHINE_MOVGR2FR_D :
                      MINIC_LOONG_MACHINE_MOVGR2FR_W,
                    8U, LOONG_T0) ||
                !loong_float_register_op(b, width == 8U
                    ? MINIC_LOONG_MACHINE_FFINT_D_L :
                      MINIC_LOONG_MACHINE_FFINT_D_W,
                    8U, 8U, 64U)) return false;
        }
        return loong_float_value(sel, ins->result, 8U, 64U, false);
    }
    if (ins->kind != MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER ||
        !loong_float_value(sel, ins->value.operand, 8U, 64U, true))
        return false;
    if (width == 8U && !sign) {
        const size_t high = sel->u64_high[ins_id];
        const size_t low = sel->u64_low[ins_id];
        const size_t done = sel->u64_done[ins_id];
        if (high == SIZE_MAX || low == SIZE_MAX || done == SIZE_MAX ||
            !loong_ri(b, MINIC_LOONG_MACHINE_LI_D, LOONG_T1,
                INT64_C(0x43e0000000000000)) ||
            !loong_float_from_gp(b, MINIC_LOONG_MACHINE_MOVGR2FR_D,
                9U, LOONG_T1) ||
            !loong_float_compare(sel, MINIC_LOONG_MACHINE_FCMP_CLT_D) ||
            !loong_branch(sel, MINIC_LOONG_MACHINE_BNEZ,
                low, true, false) ||
            !minic_machine_builder_add_successor(b, high) ||
            !minic_machine_builder_set_fallthrough(b, high) ||
            !minic_machine_builder_position_at(b, high) ||
            !loong_float_binary_regs(b, MINIC_LOONG_MACHINE_FSUB_D,
                8U, 8U, 9U) ||
            !loong_float_register_op(b, MINIC_LOONG_MACHINE_FTINTRZ_L_D,
                8U, 8U, 64U) ||
            !loong_gp_from_float(b, MINIC_LOONG_MACHINE_MOVFR2GR_D,
                LOONG_T0, 8U) ||
            !loong_ri(b, MINIC_LOONG_MACHINE_LI_D, LOONG_T1, INT64_MIN) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T0, LOONG_T0, LOONG_T1) ||
            !loong_branch(sel, MINIC_LOONG_MACHINE_B,
                done, false, false) ||
            !minic_machine_builder_position_at(b, low) ||
            !loong_float_register_op(b, MINIC_LOONG_MACHINE_FTINTRZ_L_D,
                8U, 8U, 64U) ||
            !loong_gp_from_float(b, MINIC_LOONG_MACHINE_MOVFR2GR_D,
                LOONG_T0, 8U) ||
            !loong_branch(sel, MINIC_LOONG_MACHINE_B,
                done, false, false) ||
            !minic_machine_builder_position_at(b, done)) return false;
    } else {
        if (!loong_float_register_op(b, width == 4U && sign
                ? MINIC_LOONG_MACHINE_FTINTRZ_W_D :
                  MINIC_LOONG_MACHINE_FTINTRZ_L_D,
                8U, 8U, width == 4U && sign ? 32U : 64U) ||
            !loong_gp_from_float(b, width == 4U && sign
                ? MINIC_LOONG_MACHINE_MOVFR2GR_S :
                  MINIC_LOONG_MACHINE_MOVFR2GR_D,
                LOONG_T0, 8U) ||
            (width == 4U && !sign &&
             !loong_bstrpick_d(b, LOONG_T0, 31U)) ||
            (width < 4U && !loong_normalize_integer(b, width, sign)))
            return false;
    }
    return loong_store_value(sel, ins, LOONG_T0, false);
}

/* The first typed transport uses exact-byte scalar loads/stores for
 * potentially unaligned Core pointers. No out-of-line FILE* helper can be
 * retained outside MachineFunction. This is intentionally conservative;
 * before production cutover the old aligned fast-path/slow-path split must
 * be represented as adjacent Machine auxiliary CFG blocks. */
static bool loong_select_memory(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const bool is_load = ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
        ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
    const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD ||
        ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
    const bool vol = atomic || (is_load ? ins->value.load.is_volatile :
        ins->value.store.is_volatile);
    const MinicCoreValueId address = is_load
        ? (atomic ? ins->value.atomic_load.address : ins->value.load.address)
        : (atomic ? ins->value.atomic_store.address : ins->value.store.address);
    size_t bytes;
    bool sign, pointer;
    bool floating_transport;
    uint32_t opcode;
    unsigned int i;
    /* Both integer128 and binary128 are transported as two exact eightbytes.
     * The old emitter admits unknown alignment. Keep that byte-exact path
     * here, including per-byte volatile effects, instead of assuming LD_D
     * alignment or emitting an untyped helper-assembly trampoline. */
    if (!atomic && (ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                    ins->kind == MINIC_CORE_INSTRUCTION_STORE)) {
        const MinicCoreValueId wide_value = is_load ? ins->result :
            ins->value.store.stored_value;
        const MinicCoreType *wide_type = wide_value < fn->value_count ?
            minic_core_type_get(fn->type_context,
                minic_core_type_unqualified(fn->type_context,
                    fn->values[wide_value].core_type_id)) : NULL;
        if (wide_type != NULL && wide_type->has_layout &&
            wide_type->size == 16U &&
            ((wide_type->kind == MINIC_CORE_TYPE_INTEGER &&
              wide_type->value.integer.bit_width == 128U) ||
             (wide_type->kind == MINIC_CORE_TYPE_FLOAT &&
              wide_type->value.floating.format == MINIC_CORE_FLOAT_BINARY128))) {
            size_t wide_offset;
            MinicCoreValueId source_address = address;
            if (is_load) {
                if (!minic_multi_direct_wide_scalar_load_location(fn, ins,
                        sel->ctx->frame_plan, &source_address,
                        &wide_offset)) return false;
            } else if (!minic_multi_direct_wide_store_shape(fn, ins,
                           sel->ctx->frame_plan, &wide_offset)) return false;
            if (!loong_load_value(sel, source_address, LOONG_T1, true))
                return false;
            for (i = 0U; i < 16U; ++i) {
                const size_t half_offset = wide_offset + (i >= 8U ? 8U : 0U);
                if (is_load) {
                    if ((i & 7U) == 0U &&
                        !loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
                            LOONG_T0, LOONG_ZERO)) return false;
                    if (!loong_memory_volatile(&sel->builder,
                            MINIC_LOONG_MACHINE_LD_BU, LOONG_T2, LOONG_T1,
                            (int64_t)i, true, vol) ||
                        ((i & 7U) != 0U &&
                         !loong_rri(&sel->builder,
                            MINIC_LOONG_MACHINE_SLLI_D, LOONG_T2, LOONG_T2,
                            (int64_t)(i & 7U) * 8)) ||
                        !loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_OR,
                            LOONG_T0, LOONG_T0, LOONG_T2)) return false;
                    if ((i & 7U) == 7U &&
                        !loong_wide_word(sel, MINIC_LOONG_MACHINE_ST_D,
                            LOONG_T0, half_offset, false)) return false;
                } else {
                    if ((i & 7U) == 0U &&
                        !loong_wide_word(sel, MINIC_LOONG_MACHINE_LD_D,
                            LOONG_T0, half_offset, true)) return false;
                    if (!loong_memory_volatile(&sel->builder,
                            MINIC_LOONG_MACHINE_ST_B, LOONG_T0, LOONG_T1,
                            (int64_t)i, false, vol)) return false;
                    if ((i & 7U) != 7U &&
                        !loong_rri(&sel->builder,
                            MINIC_LOONG_MACHINE_SRLI_D, LOONG_T0, LOONG_T0,
                            8)) return false;
                }
            }
            return true;
        }
    }
    if (is_load) {
        if (!minic_multi_direct_scalar_load_shape(fn, ins,
                &bytes, &sign, &pointer)) return false;
    } else if (!minic_multi_direct_scalar_store_shape(fn, ins,
                   &bytes, &sign, &pointer)) return false;
    if (pointer) bytes = 8U;
    if (bytes != 1U && bytes != 2U && bytes != 4U && bytes != 8U) return false;
    {
        const MinicCoreValueId value = is_load ? ins->result :
            (atomic ? ins->value.atomic_store.stored_value :
                ins->value.store.stored_value);
        const MinicCoreType *type = value < fn->value_count ?
            minic_core_type_get(fn->type_context,
                minic_core_type_unqualified(fn->type_context,
                    fn->values[value].core_type_id)) : NULL;
        if (type == NULL) return false;
        floating_transport = type->kind == MINIC_CORE_TYPE_FLOAT &&
            (type->value.floating.format == MINIC_CORE_FLOAT_BINARY32 ||
             type->value.floating.format == MINIC_CORE_FLOAT_BINARY64);
        /* FP values retain their exact IEEE payload in the owned frame slot;
         * unaligned source pointers still use the byte-safe GP transport. */
    }
    if (!loong_load_value(sel, address, LOONG_T1, true)) return false;
    if (!is_load) {
        const MinicCoreValueId value = atomic
            ? ins->value.atomic_store.stored_value
            : ins->value.store.stored_value;
        if (floating_transport) {
            size_t at;
            if (!loong_value_offset(sel, value, &at) ||
                !loong_frame_memory(&sel->builder,
                    bytes == 8U ? MINIC_LOONG_MACHINE_LD_D :
                                  MINIC_LOONG_MACHINE_LD_W,
                    LOONG_T0, at,
                    sel->ctx->machine_frame->has_dynamic_stack, true))
                return false;
        } else if (!loong_load_value(sel, value, LOONG_T0, pointer)) return false;
    }
    if (is_load) {
        opcode = bytes == 1U ? (sign ? MINIC_LOONG_MACHINE_LD_B :
                                       MINIC_LOONG_MACHINE_LD_BU) :
                 bytes == 2U ? (sign ? MINIC_LOONG_MACHINE_LD_H :
                                       MINIC_LOONG_MACHINE_LD_HU) :
                 bytes == 4U ? MINIC_LOONG_MACHINE_LD_W :
                               MINIC_LOONG_MACHINE_LD_D;
    } else {
        opcode = bytes == 1U ? MINIC_LOONG_MACHINE_ST_B :
                 bytes == 2U ? MINIC_LOONG_MACHINE_ST_H :
                 bytes == 4U ? MINIC_LOONG_MACHINE_ST_W :
                               MINIC_LOONG_MACHINE_ST_D;
    }
    if (bytes == 1U || atomic) {
        if (!loong_memory_volatile(&sel->builder, opcode, LOONG_T0,
                LOONG_T1, 0, is_load, vol)) return false;
    } else if (is_load) {
        if (!loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
                LOONG_T0, LOONG_ZERO)) return false;
        for (i = 0U; i < bytes; ++i) {
            if (!loong_memory_volatile(&sel->builder,
                    MINIC_LOONG_MACHINE_LD_BU,
                    LOONG_T2, LOONG_T1, i, true, vol) ||
                (i != 0U && !loong_rri(&sel->builder,
                    MINIC_LOONG_MACHINE_SLLI_D, LOONG_T2, LOONG_T2,
                    (int64_t)i * 8)) ||
                !loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_OR,
                    LOONG_T0, LOONG_T0, LOONG_T2)) return false;
        }
        if ((bytes == 4U && !loong_rrr(&sel->builder,
                MINIC_LOONG_MACHINE_ADD_W, LOONG_T0, LOONG_T0, LOONG_ZERO)) ||
            (bytes == 2U && sign &&
             (!loong_rri(&sel->builder, MINIC_LOONG_MACHINE_SLLI_D,
                 LOONG_T0, LOONG_T0, 48) ||
              !loong_rri(&sel->builder, MINIC_LOONG_MACHINE_SRAI_D,
                 LOONG_T0, LOONG_T0, 48)))) return false;
    } else {
        for (i = 0U; i < bytes; ++i) {
            if (!loong_memory_volatile(&sel->builder,
                    MINIC_LOONG_MACHINE_ST_B, LOONG_T0,
                    LOONG_T1, i, false, vol)) return false;
            if (i + 1U != bytes &&
                !loong_rri(&sel->builder, MINIC_LOONG_MACHINE_SRLI_D,
                    LOONG_T0, LOONG_T0, 8)) return false;
        }
    }
    if (!is_load) return true;
    if (floating_transport) {
        size_t at;
        return loong_value_offset(sel, ins->result, &at) &&
            loong_frame_memory(&sel->builder,
                bytes == 8U ? MINIC_LOONG_MACHINE_ST_D :
                              MINIC_LOONG_MACHINE_ST_W,
                LOONG_T0, at,
                sel->ctx->machine_frame->has_dynamic_stack, false);
    }
    return loong_store_value(sel, ins, LOONG_T0, pointer);
}

/* Aggregate and fixed-vector storage is address-backed Core state. Unlike
 * scalar integer LOAD, alignment is not presumed, and a source-volatile read
 * must not be collapsed with an independently volatile destination write. */
static bool loong_select_record_transfer(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    MinicCoreValueId source, destination;
    size_t bytes, destination_offset;
    bool source_volatile, destination_volatile;
    if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD) {
        if (!minic_multi_direct_record_load_extent(fn, ins,
                sel->ctx->frame_plan, &source,
                &destination_offset, &bytes) ||
            !loong_load_value(sel, source, LOONG_T2, true) ||
            !loong_local_frame_address(sel, LOONG_T1,
                destination_offset)) return false;
        source_volatile = ins->value.record_load.is_volatile;
        destination_volatile = false;
    } else {
        if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_COPY) {
            if (!minic_multi_direct_record_copy_extent(fn, ins, &bytes))
                return false;
            source = ins->value.record_copy.source_address;
            destination = ins->value.record_copy.destination_address;
            source_volatile = ins->value.record_copy.source_volatile;
            destination_volatile =
                ins->value.record_copy.destination_volatile;
        } else if (ins->kind ==
                       MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY) {
            const MinicCoreType *type = minic_core_type_get(
                fn->type_context, minic_core_type_unqualified(
                    fn->type_context, ins->core_type_id));
            if (type == NULL || type->kind != MINIC_CORE_TYPE_VECTOR ||
                !type->has_layout || type->size != 16U ||
                type->alignment == 0U) return false;
            bytes = 16U;
            source = ins->value.vector_storage_copy.source_address;
            destination =
                ins->value.vector_storage_copy.destination_address;
            source_volatile =
                ins->value.vector_storage_copy.source_volatile;
            destination_volatile =
                ins->value.vector_storage_copy.destination_volatile;
        } else return false;
        if (source >= fn->value_count || destination >= fn->value_count ||
            !loong_load_value(sel, source, LOONG_T2, true) ||
            !loong_load_value(sel, destination, LOONG_T1, true))
            return false;
    }
    return loong_copy_record_bytes(sel, ins, bytes, source_volatile,
        destination_volatile);
}

static bool loong_incoming_load(LoongSelector *sel,
    size_t incoming_offset, unsigned int reg, bool wide) {
    const MinicMachineLP64FrameContract *frame = sel->ctx->machine_frame;
    MinicMachineBuilder *b = &sel->builder;
    const uint32_t opcode = wide ? MINIC_LOONG_MACHINE_LD_D :
                                   MINIC_LOONG_MACHINE_LD_W;
    if (frame->saved_entry_sp_offset == SIZE_MAX) {
        if (incoming_offset > SIZE_MAX - frame->frame_size) return false;
        return loong_frame_memory(b, opcode, reg,
            frame->frame_size + incoming_offset, false, true);
    }
    if (!loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
            LOONG_T3, frame->saved_entry_sp_offset, true, true) ||
        incoming_offset > MINIC_LARGE_FRAME_MAX) return false;
    if (incoming_offset > 2047U) {
        if (!loong_ri(b, MINIC_LOONG_MACHINE_LI_D,
                LOONG_T2, (int64_t)incoming_offset) ||
            !loong_rrr(b, MINIC_LOONG_MACHINE_ADD_D,
                LOONG_T3, LOONG_T3, LOONG_T2)) return false;
        incoming_offset = 0U;
    }
    return loong_memory(b, opcode, reg, LOONG_T3,
        (int64_t)incoming_offset, MINIC_LOONG_ADDRESS_IMM12, true);
}

/* The Common incoming-ABI plan determines record classification; the target
 * owns all physical GAR/stack ingress. A by-value indirect record is copied
 * byte-for-byte into private local storage, while register-class records
 * unpack each eightbyte without touching tail padding. */
static bool loong_select_parameter_object(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicLP64ArgumentLocation *location = NULL;
    const bool dynamic = sel->ctx->machine_frame->has_dynamic_stack;
    size_t object_offset, chunk;
    if (!minic_multi_direct_parameter_object_location(sel->ctx->fn, ins,
            sel->ctx->incoming_plan, sel->ctx->frame_plan,
            &location, &object_offset) || location == NULL) return false;
    /* Empty records have no physical write. The Core effect-coverage gate
     * must first model them as explicitly effect-free before admission. */
    if (location->kind == MINIC_LP64_ARGUMENT_EMPTY_RECORD)
        return false;
    if (location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
        if (location->gp_registers != 0U) {
            if (location->gp_first >= 8U ||
                !loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
                    LOONG_T2, LOONG_A0 + (unsigned int)location->gp_first))
                return false;
        } else {
            if (location->stack_first > SIZE_MAX / 8U ||
                !loong_incoming_load(sel, location->stack_first * 8U,
                    LOONG_T2, true)) return false;
        }
        return loong_local_frame_address(sel, LOONG_T1, object_offset) &&
            loong_copy_record_bytes(sel, ins,
                location->size_bytes, false, false);
    }
    if (location->kind != MINIC_LP64_ARGUMENT_RECORD ||
        location->chunks > location->size_bytes / 8U +
            (location->size_bytes % 8U != 0U ? 1U : 0U))
        return false;
    for (chunk = 0U; chunk < location->chunks; ++chunk) {
        const bool in_register = chunk < location->gp_registers;
        if ((!in_register && location->stack_first >
                SIZE_MAX - (chunk - location->gp_registers)) ||
            chunk > SIZE_MAX / 8U) return false;
        const size_t slot = in_register ? 0U :
            location->stack_first + chunk - location->gp_registers;
        const size_t remaining = location->size_bytes - chunk * 8U;
        const size_t payload = remaining >= 8U ? 8U : remaining;
        size_t byte;
        if (payload == 0U || slot > SIZE_MAX / 8U ||
            object_offset > SIZE_MAX - chunk * 8U) return false;
        if (in_register) {
            if (location->gp_first + chunk >= 8U ||
                !loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
                    LOONG_T0, LOONG_A0 +
                        (unsigned int)(location->gp_first + chunk)))
                return false;
        } else if (!loong_incoming_load(sel, slot * 8U,
                       LOONG_T0, true)) return false;
        if (payload == 8U && ((object_offset + chunk * 8U) & 7U) == 0U) {
            if (!loong_frame_memory(&sel->builder,
                    MINIC_LOONG_MACHINE_ST_D, LOONG_T0,
                    object_offset + chunk * 8U, dynamic, false))
                return false;
            continue;
        }
        for (byte = 0U; byte < payload; ++byte) {
            if (object_offset + chunk * 8U > SIZE_MAX - byte ||
                !loong_frame_memory(&sel->builder,
                    MINIC_LOONG_MACHINE_ST_B, LOONG_T0,
                    object_offset + chunk * 8U + byte, dynamic, false) ||
                (byte + 1U < payload &&
                 !loong_rri(&sel->builder, MINIC_LOONG_MACHINE_SRLI_D,
                     LOONG_T0, LOONG_T0, 8))) return false;
        }
    }
    return true;
}

static bool loong_select_parameter(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicLP64ArgumentLocation *location = NULL;
    const MinicCoreType *type;
    size_t bytes, offset;
    bool sign, pointer;
    if (!minic_multi_direct_scalar_parameter_location(fn, ins,
            sel->ctx->incoming_plan, &location) || location == NULL ||
        !loong_value_offset(sel, ins->result, &offset)) return false;
    type = minic_core_type_get(fn->type_context,
        minic_core_type_unqualified(fn->type_context, ins->core_type_id));
    if (type == NULL) return false;
    if (type->has_layout && type->size == 16U &&
        ((type->kind == MINIC_CORE_TYPE_INTEGER &&
          type->value.integer.bit_width == 128U) ||
         (type->kind == MINIC_CORE_TYPE_FLOAT &&
          type->value.floating.format == MINIC_CORE_FLOAT_BINARY128))) {
        size_t chunk;
        if (!loong_wide_slot(sel, ins->result, &offset) ||
            location->gp_registers > 2U ||
            location->gp_first > 8U ||
            location->gp_registers > 8U - location->gp_first) return false;
        for (chunk = 0U; chunk < 2U; ++chunk) {
            if (chunk < location->gp_registers) {
                if (!loong_wide_word(sel, MINIC_LOONG_MACHINE_ST_D,
                        LOONG_A0 + (unsigned int)location->gp_first +
                            (unsigned int)chunk,
                        offset + chunk * 8U, false)) return false;
            } else {
                if (location->stack_first > (SIZE_MAX / 8U) -
                        (chunk - location->gp_registers) ||
                    !loong_incoming_load(sel,
                        (location->stack_first + chunk -
                            location->gp_registers) * 8U,
                        LOONG_T0, true) ||
                    !loong_wide_word(sel, MINIC_LOONG_MACHINE_ST_D,
                        LOONG_T0, offset + chunk * 8U, false)) return false;
            }
        }
        return true;
    }
    if (type->kind == MINIC_CORE_TYPE_FLOAT &&
        location->fp_registers != 0U) {
        const unsigned int width = type->value.floating.format ==
            MINIC_CORE_FLOAT_BINARY32 ? 32U : 64U;
        if (type->value.floating.format != MINIC_CORE_FLOAT_BINARY32 &&
            type->value.floating.format != MINIC_CORE_FLOAT_BINARY64)
            return false;
        return loong_frame_float(&sel->builder,
            width == 32U ? MINIC_LOONG_MACHINE_FST_S :
                           MINIC_LOONG_MACHINE_FST_D,
            (unsigned int)location->fp_first, width, offset,
            sel->ctx->machine_frame->has_dynamic_stack, false);
    }
    if (type->kind == MINIC_CORE_TYPE_FLOAT) {
        if (type->value.floating.format != MINIC_CORE_FLOAT_BINARY32 &&
            type->value.floating.format != MINIC_CORE_FLOAT_BINARY64)
            return false;
        bytes = type->value.floating.format == MINIC_CORE_FLOAT_BINARY64
            ? 8U : 4U;
    } else if (!loong_scalar_type(fn, ins->core_type_id,
                   &bytes, &sign, &pointer)) return false;
    if (location->gp_registers != 0U) {
        if (location->gp_first >= 8U) return false;
        return loong_frame_memory(&sel->builder,
            bytes == 8U ? MINIC_LOONG_MACHINE_ST_D :
                          MINIC_LOONG_MACHINE_ST_W,
            LOONG_A0 + (unsigned int)location->gp_first, offset,
            sel->ctx->machine_frame->has_dynamic_stack, false);
    }
    if (location->stack_first > SIZE_MAX / 8U ||
        !loong_incoming_load(sel, location->stack_first * 8U,
            LOONG_T0, bytes == 8U)) return false;
    return loong_frame_memory(&sel->builder,
        bytes == 8U ? MINIC_LOONG_MACHINE_ST_D :
                      MINIC_LOONG_MACHINE_ST_W,
        LOONG_T0, offset, sel->ctx->machine_frame->has_dynamic_stack, false);
}

static bool loong_call_load_value(LoongSelector *sel,
    MinicCoreValueId value, unsigned int gpr,
    size_t stack_bytes, bool wide) {
    size_t offset;
    if (!loong_value_offset(sel, value, &offset)) return false;
    if (!sel->ctx->machine_frame->has_dynamic_stack) {
        if (offset > SIZE_MAX - stack_bytes) return false;
        offset += stack_bytes;
    }
    return loong_frame_memory(&sel->builder,
        wide ? MINIC_LOONG_MACHINE_LD_D : MINIC_LOONG_MACHINE_LD_W,
        gpr, offset, sel->ctx->machine_frame->has_dynamic_stack, true);
}

static bool loong_call_load_fp(LoongSelector *sel,
    MinicCoreValueId value, unsigned int freg,
    size_t stack_bytes, unsigned int width) {
    size_t offset;
    if (!loong_value_offset(sel, value, &offset)) return false;
    if (!sel->ctx->machine_frame->has_dynamic_stack) {
        if (offset > SIZE_MAX - stack_bytes) return false;
        offset += stack_bytes;
    }
    return loong_frame_float(&sel->builder,
        width == 32U ? MINIC_LOONG_MACHINE_FLD_S :
                       MINIC_LOONG_MACHINE_FLD_D,
        freg, width, offset, sel->ctx->machine_frame->has_dynamic_stack,
        true);
}

/* Reuse the already-planned LP64D locations: this layer only transports
 * physical bytes. Core's record layout and wide scalar storage remain owned
 * by the Common planners, including the split-register-and-stack decision. */
static bool loong_call_frame_offset(LoongSelector *sel,
    size_t local_offset, size_t outgoing_bytes, size_t *adjusted) {
    const bool dynamic = sel->ctx->machine_frame->has_dynamic_stack;
    if (adjusted == NULL ||
        (!dynamic && local_offset > SIZE_MAX - outgoing_bytes))
        return false;
    *adjusted = local_offset + (dynamic ? 0U : outgoing_bytes);
    return true;
}

static bool loong_call_record_chunk(LoongSelector *sel,
    size_t src_offset, size_t payload, bool in_register,
    size_t gp_index, size_t stack_index) {
    const bool dynamic = sel->ctx->machine_frame->has_dynamic_stack;
    MinicMachineBuilder *b = &sel->builder;
    const unsigned int dst = in_register
        ? LOONG_A0 + (unsigned int)gp_index : LOONG_T1;
    size_t byte;
    if (payload == 0U || payload > 8U ||
        (in_register && gp_index >= 8U) ||
        (!in_register && stack_index > SIZE_MAX / 8U)) return false;
    if (payload == 8U && (src_offset & 7U) == 0U) {
        if (!loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
                dst, src_offset, dynamic, true)) return false;
    } else {
        if (!loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                LOONG_T1, LOONG_ZERO)) return false;
        for (byte = 0U; byte < payload; ++byte) {
            if (src_offset > SIZE_MAX - byte ||
                !loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_BU,
                    LOONG_T0, src_offset + byte, dynamic, true) ||
                (byte != 0U && !loong_rri(b,
                    MINIC_LOONG_MACHINE_SLLI_D, LOONG_T0,
                    LOONG_T0, (int64_t)(8U * byte))) ||
                !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                    LOONG_T1, LOONG_T1, LOONG_T0)) return false;
        }
        if (in_register && !loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                dst, LOONG_T1)) return false;
    }
    return in_register || loong_frame_memory(b,
        MINIC_LOONG_MACHINE_ST_D, dst,
        stack_index * 8U, false, false);
}

static bool loong_call_record_argument(LoongSelector *sel,
    MinicCoreObjectId object, const MinicLP64ArgumentLocation *loc,
    size_t outgoing_bytes) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    size_t bytes, chunks, offset, chunk;
    if (object >= fn->object_count || loc == NULL) return false;
    if (loc->kind == MINIC_LP64_ARGUMENT_EMPTY_RECORD)
        return minic_multi_empty_record_core(fn->type_context,
            fn->objects[object].core_type_id) && loc->size_bytes == 0U &&
            loc->chunks == 0U && loc->gp_registers == 0U &&
            loc->fp_registers == 0U && loc->stack_slots == 0U;
    if (!minic_lp64_frame_plan_object(sel->ctx->frame_plan,
            object, &offset) ||
        !loong_call_frame_offset(sel, offset, outgoing_bytes, &offset))
        return false;
    if (loc->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
        if (!minic_multi_indirect_record_core(fn->type_context,
                fn->objects[object].core_type_id, &bytes) ||
            bytes != loc->size_bytes || loc->gp_registers > 1U ||
            loc->stack_slots > 1U ||
            loc->gp_registers + loc->stack_slots != 1U ||
            !loong_local_frame_address(sel, LOONG_T0, offset))
            return false;
        if (loc->gp_registers != 0U)
            return loc->gp_first < 8U &&
                loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
                    LOONG_A0 + (unsigned int)loc->gp_first, LOONG_T0);
        return loc->stack_first <= SIZE_MAX / 8U &&
            loong_frame_memory(&sel->builder, MINIC_LOONG_MACHINE_ST_D,
                LOONG_T0, loc->stack_first * 8U, false, false);
    }
    if (loc->kind != MINIC_LP64_ARGUMENT_RECORD ||
        !minic_multi_gp_record_core(fn->type_context,
            fn->objects[object].core_type_id, &bytes, &chunks) ||
        loc->size_bytes != bytes || loc->chunks != chunks ||
        loc->gp_registers + loc->stack_slots != chunks ||
        loc->fp_registers != 0U) return false;
    for (chunk = 0U; chunk < chunks; ++chunk) {
        const bool in_reg = chunk < loc->gp_registers;
        const size_t payload = bytes - chunk * 8U >= 8U
            ? 8U : bytes - chunk * 8U;
        if (offset > SIZE_MAX - chunk * 8U ||
            (!in_reg && loc->stack_first >
                SIZE_MAX - (chunk - loc->gp_registers)) ||
            !loong_call_record_chunk(sel, offset + chunk * 8U,
                payload, in_reg, in_reg ? loc->gp_first + chunk : 0U,
                in_reg ? 0U : loc->stack_first + chunk - loc->gp_registers))
            return false;
    }
    return true;
}

static bool loong_call_wide_argument(LoongSelector *sel,
    MinicCoreValueId value, const MinicLP64ArgumentLocation *loc,
    size_t outgoing_bytes) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreType *type;
    size_t offset, chunk;
    if (value >= fn->value_count ||
        !minic_core_abi_wide_scalar(fn,
            fn->values[value].core_type_id)) return false;
    type = minic_core_type_get(fn->type_context,
        minic_core_type_unqualified(fn->type_context,
            fn->values[value].core_type_id));
    /* The shared ABI planner's float128 WIDE location records one semantic
     * value (chunks=1), while integer128 records two integer eightbytes.
     * Both physically move two 64-bit carriers on LoongArch LP64D. */
    if (type == NULL || loc->kind != MINIC_LP64_ARGUMENT_WIDE ||
        (type->kind == MINIC_CORE_TYPE_INTEGER
            ? loc->chunks != 2U || loc->size_bytes != 16U
            : type->kind != MINIC_CORE_TYPE_FLOAT ||
              type->value.floating.format != MINIC_CORE_FLOAT_BINARY128 ||
              loc->chunks != 1U) ||
        loc->gp_registers + loc->stack_slots != 2U ||
        loc->fp_registers != 0U ||
        !minic_lp64_frame_plan_wide_slot(sel->ctx->frame_plan,
            value, &offset) ||
        !loong_call_frame_offset(sel, offset, outgoing_bytes, &offset))
        return false;
    for (chunk = 0U; chunk < 2U; ++chunk) {
        const bool in_reg = chunk < loc->gp_registers;
        size_t stack_index = 0U;
        const unsigned int dst = in_reg
            ? LOONG_A0 + (unsigned int)(loc->gp_first + chunk) : LOONG_T0;
        if (!in_reg) {
            if (loc->stack_first >
                SIZE_MAX - (chunk - loc->gp_registers)) return false;
            stack_index = loc->stack_first + chunk - loc->gp_registers;
        }
        if (offset > SIZE_MAX - chunk * 8U ||
            (in_reg && loc->gp_first + chunk >= 8U) ||
            (!in_reg && stack_index > SIZE_MAX / 8U) ||
            !loong_frame_memory(&sel->builder, MINIC_LOONG_MACHINE_LD_D,
                dst, offset + chunk * 8U,
                sel->ctx->machine_frame->has_dynamic_stack, true) ||
            (!in_reg && !loong_frame_memory(&sel->builder,
                MINIC_LOONG_MACHINE_ST_D, dst,
                stack_index * 8U, false, false))) return false;
    }
    return true;
}

static bool loong_call_record_result(LoongSelector *sel,
    MinicCoreObjectId object, MinicCoreTypeId type) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    size_t offset, bytes, chunks, chunk, fp_elements, fp_width;
    if (object >= fn->object_count ||
        !minic_lp64_frame_plan_object(sel->ctx->frame_plan,
            object, &offset)) return false;
    if (minic_multi_empty_record_core(fn->type_context, type)) return true;
    if (minic_multi_indirect_record_core(fn->type_context, type, &bytes))
        return true; /* The callee filled caller-owned object storage. */
    if (minic_multi_fp_return_record_core(fn->type_context,
            type, &fp_elements, &fp_width)) {
        for (chunk = 0U; chunk < fp_elements; ++chunk)
            if (offset > SIZE_MAX - chunk * fp_width ||
                !loong_frame_float(&sel->builder,
                    fp_width == 4U ? MINIC_LOONG_MACHINE_FST_S :
                        MINIC_LOONG_MACHINE_FST_D,
                    (unsigned int)chunk, (unsigned int)(fp_width * 8U),
                    offset + chunk * fp_width,
                    sel->ctx->machine_frame->has_dynamic_stack, false))
                return false;
        return true;
    }
    if (!minic_multi_gp_record_core(fn->type_context,
            type, &bytes, &chunks)) return false;
    for (chunk = 0U; chunk < chunks; ++chunk) {
        const size_t payload = bytes - chunk * 8U >= 8U
            ? 8U : bytes - chunk * 8U;
        size_t byte;
        if (offset > SIZE_MAX - chunk * 8U ||
            !loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
                LOONG_T1, LOONG_A0 + (unsigned int)chunk)) return false;
        if (payload == 8U && ((offset + chunk * 8U) & 7U) == 0U) {
            if (!loong_frame_memory(&sel->builder,
                    MINIC_LOONG_MACHINE_ST_D, LOONG_T1,
                    offset + chunk * 8U,
                    sel->ctx->machine_frame->has_dynamic_stack, false))
                return false;
            continue;
        }
        for (byte = 0U; byte < payload; ++byte)
            if (offset + chunk * 8U > SIZE_MAX - byte ||
                !loong_frame_memory(&sel->builder,
                    MINIC_LOONG_MACHINE_ST_B, LOONG_T1,
                    offset + chunk * 8U + byte,
                    sel->ctx->machine_frame->has_dynamic_stack, false) ||
                (byte + 1U < payload &&
                 !loong_rri(&sel->builder, MINIC_LOONG_MACHINE_SRLI_D,
                     LOONG_T1, LOONG_T1, 8))) return false;
    }
    return true;
}

/* Calls and returns are one migration family: the existing Common LP64D
 * plan is authoritative for scalar, split-record, indirect-record and wide
 * carriers. Native builtins still fail closed until their typed forms land. */
static bool loong_select_call(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    const size_t begin = indirect ? ins->value.indirect_call.argument_begin :
        ins->value.call.argument_begin;
    const size_t count = indirect ? ins->value.indirect_call.argument_count :
        ins->value.call.argument_count;
    const MinicCoreObjectId result_object = indirect
        ? ins->value.indirect_call.result_object : ins->value.call.result_object;
    const size_t call_id = (size_t)(ins - fn->instructions);
    const MinicLP64PreparedCall *call;
    const MinicCoreType *result;
    size_t j;
    uint32_t opcode;
    MinicMachineOperand operands[2] = {{0}};
    if (sel->ctx->outgoing_calls == NULL ||
        ins < fn->instructions || call_id >= fn->instruction_count ||
        (count != 0U && (begin > fn->call_argument_count ||
            count > fn->call_argument_count - begin))) return false;
    call = minic_lp64_function_call_plan(sel->ctx->outgoing_calls, call_id);
    if (call == NULL || call->argument_count != count ||
        !loong_sp_adjust(&sel->builder, call->stack_bytes, false)) return false;
    for (j = 0U; j < count; ++j) {
        const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
        const MinicLP64ArgumentLocation *loc = &call->arguments[j];
        const MinicCoreType *type;
        MinicCoreAbiValueShape shape;
        if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            if (!loong_call_record_argument(sel, arg->value.object_id,
                    loc, call->stack_bytes)) return false;
            continue;
        }
        if (arg->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            arg->value.value_id >= fn->value_count ||
            !minic_core_abi_value_shape(fn,
                fn->values[arg->value.value_id].core_type_id, &shape))
            return false;
        type = minic_core_type_get(fn->type_context,
            minic_core_type_unqualified(fn->type_context,
                fn->values[arg->value.value_id].core_type_id));
        if (type == NULL || shape.bytes == 0U) return false;
        if (minic_core_abi_wide_scalar(fn,
                fn->values[arg->value.value_id].core_type_id)) {
            if (!loong_call_wide_argument(sel, arg->value.value_id,
                    loc, call->stack_bytes)) return false;
            continue;
        }
        if (shape.bytes > 8U) return false;
        if (loc->fp_registers != 0U) {
            unsigned int width;
            if (type->kind != MINIC_CORE_TYPE_FLOAT ||
                loc->fp_first >= 8U) return false;
            width = type->value.floating.format == MINIC_CORE_FLOAT_BINARY32
                ? 32U : type->value.floating.format ==
                MINIC_CORE_FLOAT_BINARY64 ? 64U : 0U;
            if (width == 0U || !loong_call_load_fp(sel, arg->value.value_id,
                    (unsigned int)loc->fp_first, call->stack_bytes, width))
                return false;
        } else if (loc->gp_registers != 0U) {
            if (loc->gp_first >= 8U ||
                !loong_call_load_value(sel, arg->value.value_id,
                    LOONG_A0 + (unsigned int)loc->gp_first,
                    call->stack_bytes, shape.bytes == 8U)) return false;
        } else if (loc->stack_slots != 0U) {
            if (loc->stack_first > SIZE_MAX / 8U ||
                !loong_call_load_value(sel, arg->value.value_id,
                    LOONG_T0, call->stack_bytes, shape.bytes == 8U) ||
                !loong_frame_memory(&sel->builder,
                    MINIC_LOONG_MACHINE_ST_D, LOONG_T0,
                    loc->stack_first * 8U, false, false)) return false;
        } else return false;
    }
    if (minic_lp64_core_kind(fn->type_context, ins->core_type_id,
            MINIC_CORE_TYPE_RECORD)) {
        size_t bytes, offset;
        if (minic_multi_indirect_record_core(fn->type_context,
                ins->core_type_id, &bytes)) {
            if (result_object >= fn->object_count ||
                !minic_lp64_frame_plan_object(sel->ctx->frame_plan,
                    result_object, &offset) ||
                !loong_call_frame_offset(sel, offset, call->stack_bytes,
                    &offset) ||
                !loong_local_frame_address(sel, LOONG_A0, offset))
                return false;
        }
    }
    if (indirect) {
        if (!loong_call_load_value(sel, ins->value.indirect_call.callee,
                LOONG_T2, call->stack_bytes, true)) return false;
        operands[0] = loong_gp(LOONG_T2, MINIC_MACHINE_REGISTER_READ);
        operands[1] = loong_imm(0);
        opcode = MINIC_LOONG_MACHINE_JIRL_CALL;
    } else {
        const MinicCoreCallee *callee;
        const char *symbol;
        if (ins->value.call.callee_id >= fn->callee_count) return false;
        callee = &fn->callees[ins->value.call.callee_id];
        if (callee->name != NULL &&
            strncmp(callee->name, "__minic_target_builtin_loongarch_",
                sizeof("__minic_target_builtin_loongarch_") - 1U) == 0)
            return false; /* Privileged ISA builtin not generalized to BL. */
        symbol = ins->value.call.extended_float_operation ==
            MINIC_CORE_EXT_FLOAT_NONE ? callee->name :
            minic_core_extended_float_runtime_symbol(
                ins->value.call.extended_float_operation);
        if (symbol == NULL) return false;
        operands[0].kind = MINIC_MACHINE_OPERAND_SYMBOL;
        operands[0].value.symbol.name = (char *)symbol;
        operands[0].value.symbol.relocation = MINIC_LOONG_RELOC_DIRECT;
        opcode = MINIC_LOONG_MACHINE_BL;
    }
    if (!loong_selected(&sel->builder, opcode, operands,
            indirect ? 2U : 1U) ||
        !loong_sp_adjust(&sel->builder, call->stack_bytes, true)) return false;
    if (!indirect) {
        switch (ins->value.call.extended_float_operation) {
        case MINIC_CORE_EXT_FLOAT_EQUAL:
            if (!loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_SLTU,
                    LOONG_A0, LOONG_ZERO, LOONG_A0) ||
                !loong_rri(&sel->builder, MINIC_LOONG_MACHINE_XORI,
                    LOONG_A0, LOONG_A0, 1)) return false;
            break;
        case MINIC_CORE_EXT_FLOAT_LESS:
            if (!loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_SLT,
                    LOONG_A0, LOONG_A0, LOONG_ZERO)) return false;
            break;
        case MINIC_CORE_EXT_FLOAT_LESS_EQUAL:
            if (!loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_SLT,
                    LOONG_A0, LOONG_ZERO, LOONG_A0) ||
                !loong_rri(&sel->builder, MINIC_LOONG_MACHINE_XORI,
                    LOONG_A0, LOONG_A0, 1)) return false;
            break;
        default:
            break;
        }
    }
    result = minic_core_type_get(fn->type_context,
        minic_core_type_unqualified(fn->type_context, ins->core_type_id));
    if (result == NULL) return false;
    if (result->kind == MINIC_CORE_TYPE_VOID) return true;
    if (result->kind == MINIC_CORE_TYPE_RECORD)
        return loong_call_record_result(sel, result_object,
            ins->core_type_id);
    if (minic_core_abi_wide_scalar(fn, ins->core_type_id)) {
        size_t offset;
        return minic_lp64_frame_plan_wide_slot(sel->ctx->frame_plan,
                   ins->result, &offset) &&
            loong_frame_memory(&sel->builder, MINIC_LOONG_MACHINE_ST_D,
                LOONG_A0, offset,
                sel->ctx->machine_frame->has_dynamic_stack, false) &&
            offset <= SIZE_MAX - 8U &&
            loong_frame_memory(&sel->builder, MINIC_LOONG_MACHINE_ST_D,
                LOONG_A0 + 1U, offset + 8U,
                sel->ctx->machine_frame->has_dynamic_stack, false);
    }
    if (result->kind == MINIC_CORE_TYPE_INTEGER && result->size > 8U)
        return false;
    if (result->kind == MINIC_CORE_TYPE_FLOAT) {
        const unsigned int width = result->value.floating.format ==
            MINIC_CORE_FLOAT_BINARY32 ? 32U : 64U;
        if (result->value.floating.format != MINIC_CORE_FLOAT_BINARY32 &&
            result->value.floating.format != MINIC_CORE_FLOAT_BINARY64)
            return false;
        return loong_float_value(sel, ins->result, 0U, width, false);
    }
    if (!loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
            LOONG_T0, LOONG_A0)) return false;
    if (result->kind == MINIC_CORE_TYPE_INTEGER) {
        size_t bytes;
        bool sign;
        if (!minic_lp64_core_integer_width(fn->type_context,
                ins->core_type_id, &bytes, &sign) ||
            !loong_normalize_integer(&sel->builder, bytes, sign)) return false;
    }
    return loong_store_value(sel, ins, LOONG_T0, false);
}

static bool loong_select_integer_cast(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    MinicDirectIntegerCastPlan cast = {0};
    MinicMachineBuilder *b = &sel->builder;
    const bool dynamic = sel->ctx->machine_frame->has_dynamic_stack;
    if (!minic_multi_direct_integer_cast_plan(sel->ctx->module,
            sel->ctx->target, sel->ctx->fn, ins,
            sel->ctx->frame_plan, &cast)) return false;
    if (cast.source_wide) {
        if (!loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
                LOONG_T0, cast.source_wide_offset, dynamic, true) ||
            !loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
                LOONG_T1, cast.source_wide_offset + 8U, dynamic, true))
            return false;
    } else {
        if (!loong_load_value(sel, cast.operand, LOONG_T0, false)) return false;
        if (cast.source_wide != cast.target_wide ||
            cast.target_boolean || cast.target_width == 8U) {
            if (cast.source_width < 8U &&
                !loong_normalize_integer(b,
                    cast.source_width, cast.source_signed)) return false;
            if (cast.source_width == 4U && !cast.source_signed &&
                !loong_bstrpick_d(b, LOONG_T0, 31U)) return false;
        }
        if (cast.target_wide &&
            !(cast.source_signed
                ? loong_rri(b, MINIC_LOONG_MACHINE_SRAI_D,
                    LOONG_T1, LOONG_T0, 63)
                : loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                    LOONG_T1, LOONG_ZERO))) return false;
    }
    if (cast.target_wide)
        return loong_frame_memory(b, MINIC_LOONG_MACHINE_ST_D,
                   LOONG_T0, cast.target_wide_offset, dynamic, false) &&
               loong_frame_memory(b, MINIC_LOONG_MACHINE_ST_D,
                   LOONG_T1, cast.target_wide_offset + 8U, dynamic, false);
    if (cast.target_boolean) {
        if (cast.source_wide &&
            !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                LOONG_T0, LOONG_T0, LOONG_T1)) return false;
        if (!loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
                LOONG_T0, LOONG_ZERO, LOONG_T0)) return false;
    } else if (!loong_normalize_integer(b,
                   cast.target_width, cast.target_signed)) return false;
    return loong_store_value(sel, ins, LOONG_T0, false);
}

static bool loong_select_scalar_bitcast(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    bool pointer_to_integer, source_signed, source_pointer;
    size_t source_width;
    if (!minic_multi_direct_scalar_bitcast_shape(sel->ctx->fn, ins,
            &pointer_to_integer, &source_width, &source_signed,
            &source_pointer) ||
        !loong_load_value(sel, ins->value.operand,
            LOONG_T0, source_pointer || pointer_to_integer)) return false;
    if (pointer_to_integer) {
        if ((source_width != 0U && source_width != 1U &&
             source_width != 8U) ||
            (source_width == 1U &&
             !loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_SLTU,
                LOONG_T0, LOONG_ZERO, LOONG_T0))) return false;
    } else if (!source_pointer && source_width != 8U) {
        if (!loong_normalize_integer(&sel->builder,
                source_width, source_signed) ||
            !(source_signed ? loong_rri(&sel->builder,
                MINIC_LOONG_MACHINE_SLLI_W, LOONG_T0, LOONG_T0, 0) :
                loong_bstrpick_d(&sel->builder, LOONG_T0, 31U))) return false;
    }
    return loong_store_value(sel, ins, LOONG_T0, false);
}

static bool loong_select_pointer_offset(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreValueId base = ins->value.pointer_offset.base;
    const MinicCoreValueId index = ins->value.pointer_offset.index;
    const size_t stride = ins->value.pointer_offset.element_size;
    size_t width, offset;
    bool sign;
    uint32_t index_opcode;
    if (!minic_multi_direct_pointer_offset_shape(sel->ctx->fn, ins,
            SIZE_MAX, &width, &sign) ||
        !loong_load_value(sel, base, LOONG_T0, true) ||
        !loong_value_offset(sel, index, &offset)) return false;
    index_opcode = width == 1U ? (sign ? MINIC_LOONG_MACHINE_LD_B :
                                         MINIC_LOONG_MACHINE_LD_BU) :
                   width == 2U ? (sign ? MINIC_LOONG_MACHINE_LD_H :
                                         MINIC_LOONG_MACHINE_LD_HU) :
                   width == 4U ? (sign ? MINIC_LOONG_MACHINE_LD_W :
                                         MINIC_LOONG_MACHINE_LD_WU) :
                   width == 8U ? MINIC_LOONG_MACHINE_LD_D : 0U;
    if (index_opcode == 0U ||
        !loong_frame_memory(&sel->builder, index_opcode,
            LOONG_T1, offset,
            sel->ctx->machine_frame->has_dynamic_stack, true)) return false;
    if (stride != 0U) {
        if ((stride & (stride - 1U)) == 0U) {
            size_t shift = 0U, n = stride;
            while (n > 1U) { ++shift; n >>= 1U; }
            if (shift != 0U && !loong_rri(&sel->builder,
                    MINIC_LOONG_MACHINE_SLLI_D, LOONG_T1,
                    LOONG_T1, (int64_t)shift)) return false;
        } else if (stride > INT64_MAX ||
            !loong_ri(&sel->builder, MINIC_LOONG_MACHINE_LI_D,
                LOONG_T2, (int64_t)stride) ||
            !loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_MUL_D,
                LOONG_T1, LOONG_T1, LOONG_T2)) return false;
        if (!loong_rrr(&sel->builder,
                ins->value.pointer_offset.subtract
                    ? MINIC_LOONG_MACHINE_SUB_D : MINIC_LOONG_MACHINE_ADD_D,
                LOONG_T0, LOONG_T0, LOONG_T1)) return false;
    }
    return loong_store_value(sel, ins, LOONG_T0, true);
}

static bool loong_select_fixed_register(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreFixedRegisterBinding *binding;
    unsigned int reg;
    bool write = ins->kind == MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE;
    size_t bytes;
    bool sign, pointer;
    if (!(write ? minic_multi_direct_fixed_register_write_shape(
                        sel->ctx->target, fn, ins, &binding)
                 : minic_multi_direct_fixed_register_read_shape(
                        sel->ctx->target, fn, ins, &binding)) ||
        binding == NULL) return false;
    if (binding->register_name_length == 3U &&
        memcmp(binding->register_name, "$tp", 3U) == 0)
        reg = 2U;
    else if (!write && binding->register_name_length == 3U &&
             memcmp(binding->register_name, "$sp", 3U) == 0 &&
             !sel->ctx->machine_frame->has_dynamic_stack)
        reg = LOONG_SP;
    else if (binding->register_name_length == 4U &&
             memcmp(binding->register_name, "$r21", 4U) == 0)
        reg = 21U;
    else return false;
    if (write) {
        if (!loong_scalar_type(fn, binding->core_type_id,
                &bytes, &sign, &pointer) || bytes != 8U ||
            (reg == 2U && !pointer) ||
            (reg == 21U && (pointer || sign))) return false;
        return loong_load_value(sel, ins->value.fixed_register_write.source,
                   LOONG_T0, true) &&
               loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
                   reg, LOONG_T0);
    }
    return loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
               LOONG_T0, reg) &&
           loong_store_value(sel, ins, LOONG_T0, true);
}

static bool loong_select_variadic_address(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicMachineLP64FrameContract *frame = sel->ctx->machine_frame;
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreType *type = minic_core_type_get(fn->type_context,
        minic_core_type_unqualified(fn->type_context, ins->core_type_id));
    size_t offset;
    if (frame->variadic_count == 0U ||
        ins->value.variadic_address.copy_destination ||
        type == NULL || type->kind != MINIC_CORE_TYPE_POINTER ||
        frame->variadic_gp_count > (SIZE_MAX - frame->variadic_save_base) / 8U)
        return false;
    offset = frame->variadic_save_base + frame->variadic_gp_count * 8U;
    return loong_local_frame_address(sel, LOONG_T0, offset) &&
        loong_store_value(sel, ins, LOONG_T0, true);
}

/* The old LP64 lowering detects unsigned size+15 overflow before modifying
 * the stack pointer. Keep the check as machine CFG: a trap block is the
 * physical fallthrough of `beqz`, and the successful continuation is a real
 * branch target. Multiple allocas inside one Core block can chain these
 * disjoint auxiliary pairs without synthesizing textual local labels. */
static bool loong_select_dynamic_alloc(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreInstructionId id = (MinicCoreInstructionId)(
        ins - fn->instructions);
    const MinicMachineOperand branch[] = {
        loong_gp(LOONG_T2, MINIC_MACHINE_REGISTER_READ),
        {.kind = MINIC_MACHINE_OPERAND_BLOCK,
         .value.block_id = id < fn->instruction_count
             ? sel->alloc_continue[id] : SIZE_MAX}
    };
    MinicMachineBuilder *b = &sel->builder;
    const size_t trap = id < fn->instruction_count
        ? sel->alloc_trap[id] : SIZE_MAX;
    const size_t continuation = id < fn->instruction_count
        ? sel->alloc_continue[id] : SIZE_MAX;
    if (id >= fn->instruction_count || trap == SIZE_MAX ||
        continuation == SIZE_MAX ||
        !minic_multi_direct_dynamic_stack_alloc_shape(fn, ins,
            sel->ctx->machine_frame->has_dynamic_stack) ||
        !loong_load_value(sel, ins->value.operand, LOONG_T0, true) ||
        !loong_rri(b, MINIC_LOONG_MACHINE_ADDI_D,
            LOONG_T1, LOONG_T0, 15) ||
        !loong_rrr(b, MINIC_LOONG_MACHINE_SLTU,
            LOONG_T2, LOONG_T1, LOONG_T0) ||
        !loong_selected(b, MINIC_LOONG_MACHINE_BEQZ, branch, 2U) ||
        !minic_machine_builder_add_successor(b, continuation) ||
        !minic_machine_builder_add_successor(b, trap) ||
        !minic_machine_builder_set_fallthrough(b, trap) ||
        !minic_machine_builder_position_at(b, trap) ||
        !loong_selected(b, MINIC_LOONG_MACHINE_BREAK, NULL, 0U) ||
        !minic_machine_builder_position_at(b, continuation) ||
        !loong_ri(b, MINIC_LOONG_MACHINE_LI_D, LOONG_T2, -16) ||
        !loong_rrr(b, MINIC_LOONG_MACHINE_AND, LOONG_T1, LOONG_T1,
            LOONG_T2) ||
        !loong_rrr(b, MINIC_LOONG_MACHINE_SUB_D, LOONG_SP, LOONG_SP,
            LOONG_T1) ||
        !loong_rr(b, MINIC_LOONG_MACHINE_MOVE, LOONG_T0, LOONG_SP))
        return false;
    return loong_store_value(sel, ins, LOONG_T0, true);
}

static bool loong_branch(LoongSelector *sel, uint32_t opcode,
    size_t target_block, bool condition, bool condition_zero) {
    MinicMachineOperand operands[2] = {{0}};
    size_t count = 1U;
    if (condition) {
        operands[0] = loong_gp(LOONG_T0, MINIC_MACHINE_REGISTER_READ);
        count = 2U;
    }
    operands[count - 1U].kind = MINIC_MACHINE_OPERAND_BLOCK;
    operands[count - 1U].value.block_id = target_block;
    if ((condition && opcode != (condition_zero ?
            MINIC_LOONG_MACHINE_BEQZ : MINIC_LOONG_MACHINE_BNEZ)) ||
        !loong_selected(&sel->builder, opcode, operands, count)) return false;
    return minic_machine_builder_add_successor(&sel->builder, target_block);
}

static bool loong_local_frame_address(LoongSelector *sel,
    unsigned int reg, size_t offset) {
    const unsigned int base = sel->ctx->machine_frame->has_dynamic_stack
        ? LOONG_S0 : LOONG_SP;
    if (offset > INT64_MAX) return false;
    if (offset <= 2047U)
        return loong_rri(&sel->builder, MINIC_LOONG_MACHINE_ADDI_D,
            reg, base, (int64_t)offset);
    return loong_ri(&sel->builder, MINIC_LOONG_MACHINE_LI_D,
            reg, (int64_t)offset) &&
        loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_ADD_D,
            reg, base, reg);
}

static bool loong_source_symbol(LoongSelector *sel,
    const MinicCoreInstruction *ins) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const char *symbol = NULL;
    MinicElfModuleSymbolFacts facts;
    bool local;
    uint32_t opcode;
    MinicMachineOperand operands[2] = {{0}};
    if (ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS) {
        if (!minic_multi_direct_global_address_symbol(fn, ins, &symbol))
            return false;
        facts = minic_multi_elf_module_symbol_facts(sel->ctx->module,
            symbol, MINIC_ELF_MODULE_SYMBOL_OBJECT);
    } else if (ins->kind == MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS) {
        if (!minic_multi_direct_function_address_symbol(fn, ins, &symbol))
            return false;
        facts = minic_multi_elf_module_symbol_facts(sel->ctx->module,
            symbol, MINIC_ELF_MODULE_SYMBOL_FUNCTION);
    } else return false;
    local = facts.tu_local;
    opcode = local ? MINIC_LOONG_MACHINE_LA_LOCAL :
                     MINIC_LOONG_MACHINE_LA_GLOBAL;
    operands[0] = loong_gp(LOONG_T0, MINIC_MACHINE_REGISTER_WRITE);
    operands[1].kind = MINIC_MACHINE_OPERAND_SYMBOL;
    operands[1].value.symbol.name = (char *)symbol;
    operands[1].value.symbol.relocation = local ? MINIC_LOONG_RELOC_DIRECT :
        MINIC_LOONG_RELOC_GLOBAL;
    return loong_selected(&sel->builder, opcode, operands, 2U) &&
        loong_store_value(sel, ins, LOONG_T0, true);
}

static bool loong_select_core(LoongSelector *sel,
    const MinicCoreInstruction *ins, MinicCoreInstructionId ins_id) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    size_t offset, bytes;
    MinicCoreValueId base;
    switch (ins->kind) {
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER: {
        MinicDirectMachineInstructionShape shape = {0};
        return minic_multi_direct_machine_instruction_shape(fn, ins,
                   false, &shape) &&
            shape.family == MINIC_DIRECT_MACHINE_COMPILER_BARRIER &&
            minic_machine_builder_emit_compiler_barrier(&sel->builder);
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return minic_loongarch64_select_core_source_asm(
            &sel->builder, sel->ctx, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
        return loong_select_integer(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        return loong_select_integer_overflow(sel, ins);
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        return loong_select_memory(sel, ins);
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
    case MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY:
        return loong_select_record_transfer(sel, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return loong_select_parameter_object(sel, ins);
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return loong_select_parameter(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL:
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return loong_select_call(sel, ins);
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        return loong_select_float(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        return loong_select_float_integer_cast(sel, ins, ins_id);
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        return loong_source_symbol(sel, ins);
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS: {
        MinicCoreBlockId block;
        MinicMachineOperand operands[2] = {{0}};
        if (!minic_multi_direct_block_address_shape(fn, ins, &block) ||
            block >= fn->block_count || !sel->reachable[block]) return false;
        operands[0] = loong_gp(LOONG_T0, MINIC_MACHINE_REGISTER_WRITE);
        operands[1].kind = MINIC_MACHINE_OPERAND_BLOCK_ADDRESS;
        operands[1].value.block_id = sel->block_ids[block];
        return loong_selected(&sel->builder,
                MINIC_LOONG_MACHINE_LA_BLOCK_ADDRESS, operands, 2U) &&
            loong_store_value(sel, ins, LOONG_T0, true);
    }
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (!minic_multi_direct_object_address_location(fn, ins,
                sel->ctx->frame_plan, &offset) ||
            !loong_local_frame_address(sel, LOONG_T0, offset)) return false;
        return loong_store_value(sel, ins, LOONG_T0, true);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        if (!minic_multi_direct_field_address_shape(fn, ins,
                &base, &bytes) ||
            !loong_load_value(sel, base, LOONG_T0, true) ||
            bytes > INT64_MAX) return false;
        if (bytes > 0U) {
            if (bytes <= 2047U) {
                if (!loong_rri(&sel->builder, MINIC_LOONG_MACHINE_ADDI_D,
                        LOONG_T0, LOONG_T0, (int64_t)bytes)) return false;
            } else if (!loong_ri(&sel->builder, MINIC_LOONG_MACHINE_LI_D,
                    LOONG_T1, (int64_t)bytes) ||
                    !loong_rrr(&sel->builder, MINIC_LOONG_MACHINE_ADD_D,
                    LOONG_T0, LOONG_T0, LOONG_T1)) return false;
        }
        return loong_store_value(sel, ins, LOONG_T0, true);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        return loong_select_pointer_offset(sel, ins);
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
        return loong_select_integer_cast(sel, ins);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return loong_select_scalar_bitcast(sel, ins);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE:
        return loong_select_fixed_register(sel, ins);
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return loong_select_variadic_address(sel, ins);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        return loong_select_dynamic_alloc(sel, ins);
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        if (sel->ctx->machine_frame->has_dynamic_stack) return false;
        if (minic_multi_direct_call_frame_level0_shape(fn, ins,
                MINIC_CORE_CALL_FRAME_ADDRESS_FRAME)) {
            if (!loong_rr(&sel->builder, MINIC_LOONG_MACHINE_MOVE,
                    LOONG_T0, LOONG_SP)) return false;
        } else if (minic_multi_direct_call_frame_return_level0_shape(fn, ins)) {
            if (sel->ctx->machine_frame->has_calls) {
                if (!loong_frame_memory(&sel->builder,
                        MINIC_LOONG_MACHINE_LD_D, LOONG_T0,
                        sel->ctx->machine_frame->save_offset, false, true))
                    return false;
            } else if (!loong_rr(&sel->builder,
                    MINIC_LOONG_MACHINE_MOVE, LOONG_T0, LOONG_RA)) return false;
        } else return false;
        return loong_store_value(sel, ins, LOONG_T0, true);
    default:
        /* No text fallback. A semantic family cannot be declared migrated
         * until every native operand and effect has typed representation. */
        return false;
    }
}

static bool loong_select_terminator(LoongSelector *sel,
    size_t core_block_id) {
    const MinicCoreFunction *fn = sel->ctx->fn;
    const MinicCoreBlock *block = &fn->blocks[core_block_id];
    const MinicCoreTerminator *t = &block->terminator;
    const MinicMachineLP64FrameContract *frame = sel->ctx->machine_frame;
    MinicMachineBuilder *b = &sel->builder;
    size_t target;
    if (!block->has_terminator) return false;
    switch (t->kind) {
    case MINIC_CORE_TERMINATOR_RETURN: {
        const MinicCoreTypeId type = minic_core_function_return_type(fn);
        if (!minic_lp64_core_kind(fn->type_context, type,
                MINIC_CORE_TYPE_VOID)) {
            const MinicCoreType *core = minic_core_type_get(fn->type_context,
                minic_core_type_unqualified(fn->type_context, type));
            if (core == NULL) return false;
            if (core->kind == MINIC_CORE_TYPE_RECORD) {
                size_t offset, bytes, chunks, fp_elements, fp_width, chunk;
                const MinicCoreObjectId object = t->return_object;
                const bool indirect_return =
                    minic_multi_indirect_record_core(fn->type_context,
                        type, &bytes);
                if (object >= fn->object_count ||
                    frame->has_indirect_return != indirect_return ||
                    !minic_lp64_frame_plan_object(sel->ctx->frame_plan,
                        object, &offset)) return false;
                if (minic_multi_empty_record_core(fn->type_context, type)) {
                    /* Zero-sized aggregates have no physical return. */
                } else if (minic_multi_fp_return_record_core(
                        fn->type_context, type, &fp_elements, &fp_width)) {
                    for (chunk = 0U; chunk < fp_elements; ++chunk)
                        if (offset > SIZE_MAX - chunk * fp_width ||
                            !loong_frame_float(b, fp_width == 4U ?
                                MINIC_LOONG_MACHINE_FLD_S :
                                MINIC_LOONG_MACHINE_FLD_D,
                                (unsigned int)chunk,
                                (unsigned int)(fp_width * 8U),
                                offset + chunk * fp_width,
                                frame->has_dynamic_stack, true)) return false;
                } else if (indirect_return) {
                    if (!loong_local_frame_address(sel, LOONG_T2, offset) ||
                        !loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
                            LOONG_A0, frame->indirect_return_offset,
                            frame->has_dynamic_stack, true) ||
                        !loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                            LOONG_T1, LOONG_A0) ||
                        !loong_copy_record_bytes(sel, NULL, bytes,
                            false, false))
                        return false;
                } else {
                    if (!minic_multi_gp_record_core(fn->type_context,
                            type, &bytes, &chunks)) return false;
                    for (chunk = 0U; chunk < chunks; ++chunk) {
                        const size_t payload = bytes - chunk * 8U >= 8U
                            ? 8U : bytes - chunk * 8U;
                        size_t byte;
                        if (offset > SIZE_MAX - chunk * 8U) return false;
                        if (payload == 8U &&
                            ((offset + chunk * 8U) & 7U) == 0U) {
                            if (!loong_frame_memory(b,
                                    MINIC_LOONG_MACHINE_LD_D,
                                    LOONG_A0 + (unsigned int)chunk,
                                    offset + chunk * 8U,
                                    frame->has_dynamic_stack, true))
                                return false;
                            continue;
                        }
                        if (!loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                                LOONG_T1, LOONG_ZERO)) return false;
                        for (byte = 0U; byte < payload; ++byte)
                            if (offset + chunk * 8U > SIZE_MAX - byte ||
                                !loong_frame_memory(b,
                                    MINIC_LOONG_MACHINE_LD_BU, LOONG_T0,
                                    offset + chunk * 8U + byte,
                                    frame->has_dynamic_stack, true) ||
                                (byte != 0U && !loong_rri(b,
                                    MINIC_LOONG_MACHINE_SLLI_D, LOONG_T0,
                                    LOONG_T0, (int64_t)(8U * byte))) ||
                                !loong_rrr(b, MINIC_LOONG_MACHINE_OR,
                                    LOONG_T1, LOONG_T1, LOONG_T0))
                                return false;
                        if (!loong_rr(b, MINIC_LOONG_MACHINE_MOVE,
                                LOONG_A0 + (unsigned int)chunk,
                                LOONG_T1)) return false;
                    }
                }
            } else if (minic_core_abi_wide_scalar(fn, type)) {
                size_t offset;
                if (!minic_lp64_frame_plan_wide_slot(sel->ctx->frame_plan,
                        t->return_value, &offset) ||
                    offset > SIZE_MAX - 8U ||
                    !loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
                        LOONG_A0, offset, frame->has_dynamic_stack, true) ||
                    !loong_frame_memory(b, MINIC_LOONG_MACHINE_LD_D,
                        LOONG_A0 + 1U, offset + 8U,
                        frame->has_dynamic_stack, true)) return false;
            } else if ((core->kind == MINIC_CORE_TYPE_INTEGER ||
                        core->kind == MINIC_CORE_TYPE_FLOAT) &&
                       core->size > 8U) {
                return false;
            } else if (core->kind == MINIC_CORE_TYPE_FLOAT) {
                size_t offset;
                unsigned int width;
                uint32_t opcode;
                if (!loong_value_offset(sel, t->return_value, &offset))
                    return false;
                width = core->value.floating.format == MINIC_CORE_FLOAT_BINARY32
                    ? 32U : core->value.floating.format ==
                      MINIC_CORE_FLOAT_BINARY64 ? 64U : 0U;
                opcode = width == 32U ? MINIC_LOONG_MACHINE_FLD_S :
                    MINIC_LOONG_MACHINE_FLD_D;
                if (width == 0U || !loong_frame_float(b,
                        opcode, 0U, width, offset,
                        frame->has_dynamic_stack, true)) return false;
            } else if (!loong_load_value(sel, t->return_value,
                    LOONG_A0, false)) return false;
        }
        return minic_loongarch64_machine_select_epilogue(b, frame);
    }
    case MINIC_CORE_TERMINATOR_BRANCH:
        target = t->branch_target;
        return target < fn->block_count && sel->reachable[target] &&
            loong_branch(sel, MINIC_LOONG_MACHINE_B,
                sel->block_ids[target], false, false);
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
        const MinicCoreValueId cond = t->conditional.condition;
        const MinicCoreBlockId when_true = t->conditional.when_true;
        const MinicCoreBlockId when_false = t->conditional.when_false;
        bool big;
        size_t aux;
        if (cond >= fn->value_count ||
            when_true >= fn->block_count || when_false >= fn->block_count ||
            !sel->reachable[when_true] || !sel->reachable[when_false] ||
            !loong_load_value(sel, cond, LOONG_T0, false)) {
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                    "MINIC_LOONG_MIR_COND_REJECT function=%s block=%zu "
                    "stage=value cond=%u true=%u false=%u\n",
                    sel->ctx->name, core_block_id, (unsigned int)cond,
                    (unsigned int)when_true, (unsigned int)when_false);
            return false;
        }
        big = fn->block_count > 256U;
        aux = big ? sel->aux_true[core_block_id] :
                    sel->aux_false[core_block_id];
        if (aux == SIZE_MAX ||
            !loong_branch(sel, big ? MINIC_LOONG_MACHINE_BEQZ :
                    MINIC_LOONG_MACHINE_BNEZ,
                big ? sel->aux_false[core_block_id] :
                      sel->block_ids[when_true], true, big)) {
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                    "MINIC_LOONG_MIR_COND_REJECT function=%s block=%zu "
                    "stage=branch aux=%zu\n", sel->ctx->name,
                    core_block_id, aux);
            return false;
        }
        if (!minic_machine_builder_add_successor(b, aux) ||
            !minic_machine_builder_set_fallthrough(b, aux) ||
            !minic_machine_builder_position_at(b, aux) ||
            !loong_branch(sel, MINIC_LOONG_MACHINE_B,
                sel->block_ids[big ? when_true : when_false], false, false)) {
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                    "MINIC_LOONG_MIR_COND_REJECT function=%s block=%zu "
                    "stage=fallthrough aux=%zu\n", sel->ctx->name,
                    core_block_id, aux);
            return false;
        }
        if (big) {
            if (!minic_machine_builder_position_at(b,
                    sel->aux_false[core_block_id]) ||
                !loong_branch(sel, MINIC_LOONG_MACHINE_B,
                    sel->block_ids[when_false], false, false)) return false;
        }
        return true;
    }
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH: {
        size_t i;
        MinicMachineOperand operands[2] = {{0}};
        if (!loong_load_value(sel, t->indirect_target, LOONG_T0, true))
            return false;
        operands[0] = loong_gp(LOONG_T0, MINIC_MACHINE_REGISTER_READ);
        operands[1] = loong_imm(0);
        if (!loong_selected(b, MINIC_LOONG_MACHINE_JIRL_BRANCH,
                operands, 2U)) return false;
        /* Only opaque/indirect Core entries are machine CFG successors;
         * these are not invented from address-taken instruction text. */
        for (i = 0U; i < fn->block_count; ++i)
            if (sel->reachable[i] && fn->blocks[i].indirect_entry &&
                !minic_machine_builder_add_successor(b, sel->block_ids[i]))
                return false;
        return true;
    }
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return loong_selected(b, MINIC_LOONG_MACHINE_BREAK, NULL, 0U);
    default:
        return false;
    }
}

static bool loong_preplan_copy_blocks(
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    const MinicCoreType *type;
    if (ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
        const MinicLP64ArgumentLocation *location = NULL;
        size_t object_offset;
        return minic_multi_direct_parameter_object_location(ctx->fn, ins,
            ctx->incoming_plan, ctx->frame_plan,
            &location, &object_offset) &&
            location != NULL &&
            location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD &&
            location->size_bytes >= 8U;
    }
    if (ins->kind != MINIC_CORE_INSTRUCTION_RECORD_COPY &&
        ins->kind != MINIC_CORE_INSTRUCTION_RECORD_LOAD &&
        ins->kind != MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY)
        return false;
    type = minic_core_type_get(ctx->fn->type_context,
        minic_core_type_unqualified(ctx->fn->type_context,
            ins->core_type_id));
    return type != NULL && type->has_layout && type->size >= 8U;
}

bool minic_loongarch64_select_machine(MinicMachineFunction *machine,
    const MinicDirectLP64BlockContext *ctx) {
    LoongSelector sel = {0};
    const MinicCoreFunction *fn;
    const MinicMachineLP64FrameContract *frame;
    size_t prologue_id;
    size_t i, count;
    bool success = false;
    if (machine == NULL || ctx == NULL || ctx->module == NULL ||
        ctx->target == NULL || ctx->fn == NULL ||
        ctx->fn->type_context == NULL || ctx->machine_frame == NULL ||
        ctx->frame_plan == NULL || ctx->name == NULL ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64)
        return false;
    fn = ctx->fn;
    frame = ctx->machine_frame;
    count = fn->block_count;
    if (count == 0U || count > SIZE_MAX / sizeof(*sel.block_ids) ||
        fn->instruction_count > SIZE_MAX / sizeof(*sel.alloc_trap) ||
        !minic_machine_function_initialize(machine, ctx->target, ctx->name,
            minic_loongarch64_target_soft_float_abi(ctx->target)
                ? MINIC_LOONG_MACHINE_FEATURE_BASE
                : MINIC_LOONG_MACHINE_FEATURE_BASE |
                    MINIC_LOONG_MACHINE_FEATURE_FP))
        return false;
    machine->frame_size = frame->frame_size;
    machine->frame_alignment = frame->frame_alignment;
    machine->has_dynamic_stack = frame->has_dynamic_stack;
    machine->calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;
    sel.ctx = ctx;
    sel.block_ids = malloc(count * sizeof(*sel.block_ids));
    sel.aux_true = malloc(count * sizeof(*sel.aux_true));
    sel.aux_false = malloc(count * sizeof(*sel.aux_false));
    sel.alloc_trap = malloc((fn->instruction_count != 0U
        ? fn->instruction_count : 1U) * sizeof(*sel.alloc_trap));
    sel.alloc_continue = malloc((fn->instruction_count != 0U
        ? fn->instruction_count : 1U) * sizeof(*sel.alloc_continue));
    sel.u64_high = malloc((fn->instruction_count != 0U
        ? fn->instruction_count : 1U) * sizeof(*sel.u64_high));
    sel.u64_low = malloc((fn->instruction_count != 0U
        ? fn->instruction_count : 1U) * sizeof(*sel.u64_low));
    sel.u64_done = malloc((fn->instruction_count != 0U
        ? fn->instruction_count : 1U) * sizeof(*sel.u64_done));
    sel.copy_loop = malloc((fn->instruction_count != 0U
        ? fn->instruction_count : 1U) * sizeof(*sel.copy_loop));
    sel.copy_done = malloc((fn->instruction_count != 0U
        ? fn->instruction_count : 1U) * sizeof(*sel.copy_done));
    sel.reachable = calloc(count, sizeof(*sel.reachable));
    if (sel.block_ids == NULL || sel.aux_true == NULL ||
        sel.aux_false == NULL || sel.alloc_trap == NULL ||
        sel.alloc_continue == NULL || sel.u64_high == NULL ||
        sel.u64_low == NULL || sel.u64_done == NULL ||
        sel.copy_loop == NULL || sel.copy_done == NULL ||
        sel.reachable == NULL ||
        !minic_machine_builder_initialize(&sel.builder, machine))
        goto cleanup;
    for (i = 0U; i < count; ++i)
        sel.block_ids[i] = sel.aux_true[i] = sel.aux_false[i] = SIZE_MAX;
    for (i = 0U; i < fn->instruction_count; ++i) {
        sel.alloc_trap[i] = sel.alloc_continue[i] = SIZE_MAX;
        sel.u64_high[i] = sel.u64_low[i] = sel.u64_done[i] = SIZE_MAX;
        sel.copy_loop[i] = sel.copy_done[i] = SIZE_MAX;
    }
    if (minic_core_function_facts_verified(ctx->core_facts, fn) &&
        ctx->core_facts->reachable_blocks != NULL)
        memcpy(sel.reachable, ctx->core_facts->reachable_blocks,
               count * sizeof(*sel.reachable));
    else if (!minic_core_function_mark_reachable_blocks(fn,
                 sel.reachable, count)) goto cleanup;

    /* The first physical block is exclusively the function prologue. A
     * computed-goto to Core block0 must NEVER repeat callee saves / SP
     * adjustment. Preserve physical block adjacency for cond fallthrough. */
    if (!minic_machine_builder_create_block(&sel.builder,
            NULL, false, true, &prologue_id) || prologue_id != 0U)
        goto cleanup;
    for (i = 0U; i < count; ++i) {
        const MinicCoreBlock *block;
        size_t ordinal;
        if (!sel.reachable[i]) continue;
        block = &fn->blocks[i];
        if (block->local_label_symbol != NULL &&
            !minic_core_label_symbol_valid(block->local_label_symbol))
            goto cleanup;
        if (!minic_machine_builder_create_block(&sel.builder,
                block->local_label_symbol, block->indirect_entry, false,
                &sel.block_ids[i])) goto cleanup;
        for (ordinal = 0U; ordinal < block->instruction_count; ++ordinal) {
            MinicCoreInstructionId id;
            const MinicCoreInstruction *ins;
            if (!minic_core_block_instruction_at(fn, block, ordinal,
                    &id, &ins)) goto cleanup;
            if (id >= fn->instruction_count) goto cleanup;
            if (loong_preplan_copy_blocks(ctx, ins) &&
                (!minic_machine_builder_create_block(&sel.builder,
                     NULL, false, true, &sel.copy_loop[id]) ||
                 !minic_machine_builder_create_block(&sel.builder,
                     NULL, false, true, &sel.copy_done[id]))) goto cleanup;
            if (ins->kind == MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC) {
                if (!minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &sel.alloc_trap[id]) ||
                    !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &sel.alloc_continue[id]))
                    goto cleanup;
            } else if (ins->kind ==
                       MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER) {
                size_t width;
                bool sign;
                if (minic_multi_direct_floating_shape(
                        fn, ins, &width, &sign) && width == 8U && !sign &&
                    (!minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &sel.u64_high[id]) ||
                     !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &sel.u64_low[id]) ||
                     !minic_machine_builder_create_block(&sel.builder,
                        NULL, false, true, &sel.u64_done[id]))) goto cleanup;
            }
        }
        if (block->terminator.kind == MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH) {
            if (count > 256U &&
                !minic_machine_builder_create_block(&sel.builder,
                    NULL, false, true, &sel.aux_true[i])) goto cleanup;
            if (!minic_machine_builder_create_block(&sel.builder,
                NULL, false, true, &sel.aux_false[i])) goto cleanup;
        }
    }
    if (fn->entry_block >= count ||
        !sel.reachable[fn->entry_block] ||
        sel.block_ids[fn->entry_block] == SIZE_MAX ||
        !minic_machine_builder_position_at(&sel.builder, prologue_id) ||
        !minic_loongarch64_machine_select_prologue(&sel.builder, frame) ||
        !loong_branch(&sel, MINIC_LOONG_MACHINE_B,
            sel.block_ids[fn->entry_block], false, false)) goto cleanup;

    for (i = 0U; i < count; ++i) {
        const MinicCoreBlock *block;
        size_t ordinal;
        if (!sel.reachable[i]) continue;
        block = &fn->blocks[i];
        if (!minic_machine_builder_position_at(
                &sel.builder, sel.block_ids[i]) ||
            !minic_machine_builder_mark_core_block(&sel.builder, i))
            goto cleanup;
        for (ordinal = 0U; ordinal < block->instruction_count; ++ordinal) {
            MinicCoreInstructionId ins_id;
            const MinicCoreInstruction *ins;
            if (!minic_core_block_instruction_at(fn, block, ordinal,
                    &ins_id, &ins)) goto cleanup;
            minic_machine_builder_set_origin(&sel.builder, i, ins_id);
            if (!loong_select_core(&sel, ins, ins_id)) {
                if (getenv("MINIC_M1_TRACE") != NULL)
                    (void)fprintf(stderr,
                        "MINIC_LOONG_MIR_SELECT_REJECT function=%s block=%zu "
                        "instruction=%u kind=%u\n", ctx->name, i,
                        (unsigned int)ins_id, (unsigned int)ins->kind);
                goto cleanup;
            }
            minic_machine_builder_clear_origin(&sel.builder);
        }
        if (!loong_select_terminator(&sel, i)) {
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                    "MINIC_LOONG_MIR_TERMINATOR_REJECT function=%s block=%zu "
                    "kind=%u\n", ctx->name, i,
                    (unsigned int)block->terminator.kind);
            goto cleanup;
        }
    }
    success = true;
cleanup:
    free(sel.block_ids);
    free(sel.aux_true);
    free(sel.aux_false);
    free(sel.alloc_trap);
    free(sel.alloc_continue);
    free(sel.u64_high);
    free(sel.u64_low);
    free(sel.u64_done);
    free(sel.copy_loop);
    free(sel.copy_done);
    free(sel.reachable);
    return success;
}
