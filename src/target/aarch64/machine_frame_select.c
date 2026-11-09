#include "target/aarch64/machine_frame_select.h"

#include "target/aarch64/machine_opcode.h"
#include "target/common/lp64_frame.h"
#include <limits.h>
#include <string.h>

/* Physical register ownership, operand access and implicit effects come from
 * the AArch64 opcode contract. The frame selector supplies only identities,
 * legal displacements and physical instruction order. */
static MinicMachineOperand a64_register(MinicMachineRegisterBank bank,
    unsigned int reg, unsigned int width) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_REGISTER;
    result.value.physical.reg = (MinicMachineRegister){reg, (uint16_t)width, bank};
    return result;
}

static MinicMachineOperand a64_gp(unsigned int reg) {
    return a64_register(MINIC_MACHINE_REGISTER_GENERAL, reg, 64U);
}

static MinicMachineOperand a64_immediate(int64_t value) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_INTEGER;
    result.value.integer = value;
    return result;
}

static MinicMachineOperand a64_memory(unsigned int base, size_t offset,
    MinicA64MachineAddressMode mode) {
    MinicMachineOperand result;
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    result.value.address.target_mode = mode;
    result.value.address.has_base = true;
    result.value.address.base = (MinicMachineRegister){
        base, 64U, MINIC_MACHINE_REGISTER_GENERAL};
    result.value.address.displacement = (int64_t)offset;
    return result;
}

static bool a64_selected(MinicMachineBuilder *builder,
    MinicA64MachineOpcode opcode, MinicMachineOperand *operands, size_t count) {
    MinicMachineGNUAsmPolicy policy;
    MinicMachineOpcodeContract contract = {0};
    size_t i;
    if (builder == NULL || builder->function == NULL ||
        !minic_aarch64_machine_opcode_initialize(builder->function->target,
            &policy) ||
        !policy.verify.lookup_opcode(builder->function->target, opcode,
            &contract, policy.verify.context) ||
        count != contract.operand_count) return false;
    for (i = 0U; i < count; ++i) {
        if (operands[i].kind != contract.operand_kinds[i]) return false;
        if (operands[i].kind == MINIC_MACHINE_OPERAND_REGISTER)
            operands[i].value.physical.access = contract.register_access[i];
    }
    return minic_machine_builder_emit_selected(builder, opcode,
        contract.required_effects, count != 0U ? operands : NULL, count,
        contract.implicit_count != 0U ? contract.implicit : NULL,
        contract.implicit_count);
}

static bool a64_rr(MinicMachineBuilder *b, MinicA64MachineOpcode opcode,
    unsigned int dst, unsigned int src) {
    MinicMachineOperand ops[] = {a64_gp(dst), a64_gp(src)};
    return a64_selected(b, opcode, ops, 2U);
}

static bool a64_rrr(MinicMachineBuilder *b, MinicA64MachineOpcode opcode,
    unsigned int dst, unsigned int lhs, unsigned int rhs) {
    MinicMachineOperand ops[] = {a64_gp(dst), a64_gp(lhs), a64_gp(rhs)};
    return a64_selected(b, opcode, ops, 3U);
}

static bool a64_rri(MinicMachineBuilder *b, MinicA64MachineOpcode opcode,
    unsigned int dst, unsigned int src, int64_t imm) {
    MinicMachineOperand ops[] = {a64_gp(dst), a64_gp(src), a64_immediate(imm)};
    return a64_selected(b, opcode, ops, 3U);
}

static bool a64_movwide(MinicMachineBuilder *b,
    MinicA64MachineOpcode opcode, unsigned int dst,
    uint16_t chunk, unsigned int shift) {
    MinicMachineOperand ops[] = {
        a64_gp(dst), a64_immediate(chunk), a64_immediate(shift)};
    return a64_selected(b, opcode, ops, 3U);
}

static bool a64_materialize(MinicMachineBuilder *b,
    unsigned int dst, size_t value) {
    const uint64_t bits = (uint64_t)value;
    unsigned int shift;
    if (dst > 30U || (size_t)bits != value ||
        !a64_movwide(b, MINIC_A64_MACHINE_MOVZ, dst,
            (uint16_t)(bits & UINT64_C(0xffff)), 0U)) return false;
    for (shift = 16U; shift < 64U; shift += 16U)
        if (((bits >> shift) & UINT64_C(0xffff)) != 0U &&
            !a64_movwide(b, MINIC_A64_MACHINE_MOVK, dst,
                (uint16_t)((bits >> shift) & UINT64_C(0xffff)), shift))
            return false;
    return true;
}

static bool a64_address_from_base(MinicMachineBuilder *b,
    unsigned int dst, unsigned int base, size_t offset) {
    if (dst > 30U || base > MINIC_A64_MACHINE_SP ||
        (dst == base && offset > 4095U)) return false;
    if (offset == 0U) return a64_rr(b, MINIC_A64_MACHINE_MOV, dst, base);
    if (offset <= 4095U)
        return a64_rri(b, MINIC_A64_MACHINE_ADD_IMM, dst,
            base, (int64_t)offset);
    return a64_materialize(b, dst, offset) &&
        a64_rrr(b, MINIC_A64_MACHINE_ADD, dst, base, dst);
}

static bool a64_memop(MinicMachineBuilder *b, bool store,
    MinicA64FrameRegister reg, unsigned int base, size_t offset) {
    MinicA64MachineOpcode opcode;
    MinicMachineRegisterBank bank;
    unsigned int bits;
    MinicMachineOperand ops[2];
    if (reg.number > 31U || base > MINIC_A64_MACHINE_SP ||
        offset > 65520U) return false;
    switch (reg.kind) {
    case MINIC_A64_REG_W:
        if (reg.number == 31U) return false;
        opcode = store ? MINIC_A64_MACHINE_STR_W : MINIC_A64_MACHINE_LDR_W;
        bank = MINIC_MACHINE_REGISTER_GENERAL; bits = 32U; break;
    case MINIC_A64_REG_X:
        if (reg.number == 31U) return false;
        opcode = store ? MINIC_A64_MACHINE_STR_X : MINIC_A64_MACHINE_LDR_X;
        bank = MINIC_MACHINE_REGISTER_GENERAL; bits = 64U; break;
    case MINIC_A64_REG_S:
        opcode = store ? MINIC_A64_MACHINE_STR_FP_S : MINIC_A64_MACHINE_LDR_FP_S;
        bank = MINIC_MACHINE_REGISTER_FLOAT; bits = 32U; break;
    case MINIC_A64_REG_D:
        opcode = store ? MINIC_A64_MACHINE_STR_FP_D : MINIC_A64_MACHINE_LDR_FP_D;
        bank = MINIC_MACHINE_REGISTER_FLOAT; bits = 64U; break;
    case MINIC_A64_REG_Q:
        opcode = store ? MINIC_A64_MACHINE_STR_FP_Q : MINIC_A64_MACHINE_LDR_FP_Q;
        bank = MINIC_MACHINE_REGISTER_VECTOR; bits = 128U; break;
    default: return false;
    }
    if ((offset % (bits / 8U)) != 0U ||
        offset > (size_t)(4095U * (bits / 8U))) return false;
    ops[0] = a64_register(bank, reg.number, bits);
    ops[1] = a64_memory(base, offset, MINIC_A64_ADDRESS_UNSIGNED);
    return a64_selected(b, opcode, ops, 2U);
}

static unsigned int a64_scratch(MinicA64ScratchPolicy policy,
    uint32_t excluded) {
    const uint32_t unavailable = policy.forbidden_gp_mask | excluded;
    unsigned int preferred = policy.preferred_scratch;
    if (preferred != 0U && (preferred < 9U || preferred > 17U)) return 0U;
    if (preferred == 0U) preferred = 17U;
    if ((unavailable & (UINT32_C(1) << preferred)) == 0U) return preferred;
    if (policy.preferred_scratch != 0U) return 0U;
    preferred = preferred == 17U ? 16U : 17U;
    return (unavailable & (UINT32_C(1) << preferred)) == 0U ? preferred : 0U;
}

static bool a64_entry_sp(MinicMachineBuilder *b,
    const MinicA64FrameView *view, unsigned int dst, unsigned int address) {
    const size_t slot = view->saved_entry_sp_offset;
    if ((slot % 8U) == 0U && slot <= 4095U)
        return a64_memop(b, false, (MinicA64FrameRegister){
            MINIC_A64_REG_X, dst}, 19U, slot);
    if (dst == address ||
        !a64_address_from_base(b, address, 19U, slot)) return false;
    return a64_memop(b, false, (MinicA64FrameRegister){
        MINIC_A64_REG_X, dst}, address, 0U);
}

static bool a64_transfer(MinicMachineBuilder *b,
    const MinicA64FrameView *view, MinicA64FrameRef frame,
    MinicA64FrameRegister reg, MinicA64ScratchPolicy policy, bool store) {
    MinicA64ResolvedFrame resolved;
    size_t width;
    uint32_t excluded = 0U;
    unsigned int scratch, secondary;
    if (!minic_a64_resolve_frame(view, frame, &resolved)) return false;
    switch (reg.kind) {
    case MINIC_A64_REG_W: case MINIC_A64_REG_S: width = 4U; break;
    case MINIC_A64_REG_X: case MINIC_A64_REG_D: width = 8U; break;
    case MINIC_A64_REG_Q: width = 16U; break;
    default: return false;
    }
    if (reg.kind == MINIC_A64_REG_W || reg.kind == MINIC_A64_REG_X) {
        if (reg.number > 30U) return false;
        excluded = UINT32_C(1) << reg.number;
    }
    if (!resolved.saved_entry_frame_sp &&
        (resolved.displacement % width) == 0U &&
        resolved.displacement <= 4095U)
        return a64_memop(b, store, reg,
            resolved.base_register, resolved.displacement);
    scratch = a64_scratch(policy, excluded);
    if (scratch == 0U) return false;
    if (resolved.saved_entry_frame_sp) {
        MinicA64ScratchPolicy second_policy = policy;
        second_policy.preferred_scratch = 0U;
        secondary = a64_scratch(second_policy,
            excluded | (UINT32_C(1) << scratch));
        if (resolved.displacement % width == 0U &&
            resolved.displacement <= 4095U &&
            view->saved_entry_sp_offset % 8U == 0U &&
            view->saved_entry_sp_offset <= 4095U)
            return a64_entry_sp(b, view, scratch, 0U) &&
                a64_memop(b, store, reg, scratch, resolved.displacement);
        if (secondary == 0U ||
            !a64_entry_sp(b, view, scratch, secondary) ||
            !a64_address_from_base(b, secondary, scratch,
                resolved.displacement)) return false;
        return a64_memop(b, store, reg, secondary, 0U);
    }
    return a64_address_from_base(b, scratch,
        resolved.base_register, resolved.displacement) &&
        a64_memop(b, store, reg, scratch, 0U);
}

bool minic_a64_machine_emit_frame_load(MinicMachineBuilder *b,
    const MinicA64FrameView *view, MinicA64FrameRef frame,
    MinicA64FrameRegister reg, MinicA64ScratchPolicy scratch) {
    return a64_transfer(b, view, frame, reg, scratch, false);
}

bool minic_a64_machine_emit_frame_store(MinicMachineBuilder *b,
    const MinicA64FrameView *view, MinicA64FrameRef frame,
    MinicA64FrameRegister reg, MinicA64ScratchPolicy scratch) {
    return a64_transfer(b, view, frame, reg, scratch, true);
}

bool minic_a64_machine_emit_frame_address(MinicMachineBuilder *b,
    const MinicA64FrameView *view, MinicA64FrameRef frame,
    unsigned int dst, MinicA64ScratchPolicy policy) {
    MinicA64ResolvedFrame resolved;
    unsigned int scratch;
    if (dst > 30U || (view != NULL && view->dynamic_frame && dst == 19U) ||
        !minic_a64_resolve_frame(view, frame, &resolved)) return false;
    if (!resolved.saved_entry_frame_sp)
        return a64_address_from_base(b, dst,
            resolved.base_register, resolved.displacement);
    scratch = a64_scratch(policy, UINT32_C(1) << dst);
    return scratch != 0U && a64_entry_sp(b, view, scratch, dst) &&
        a64_address_from_base(b, dst, scratch, resolved.displacement);
}

static bool a64_owned(MinicMachineBuilder *b, const MinicA64FrameView *v,
    bool store, MinicA64FrameRegisterClass kind,
    unsigned int reg, size_t offset) {
    const MinicA64FrameRegister value = {kind, reg};
    const MinicA64ScratchPolicy scratch = {0U, 0U};
    return a64_transfer(b, v, minic_a64_owned_slot(offset), value,
        scratch, store);
}

static bool a64_pair(MinicMachineBuilder *b, bool prologue) {
    MinicMachineOperand ops[] = {
        a64_gp(29U), a64_gp(30U),
        a64_memory(MINIC_A64_MACHINE_SP, 0U,
            prologue ? MINIC_A64_ADDRESS_PRE_INDEX :
                       MINIC_A64_ADDRESS_POST_INDEX)};
    ops[2].value.address.displacement = prologue ? -16 : 16;
    return a64_selected(b,
        prologue ? MINIC_A64_MACHINE_STP_PRE_X :
                   MINIC_A64_MACHINE_LDP_POST_X, ops, 3U);
}

static bool a64_sp_frame_delta(MinicMachineBuilder *b,
    size_t frame, bool allocate) {
    if (frame == 0U) return true;
    /* Match the established direct-AAPCS64 two-halfword frame admission.
     * The shared LP64 frame bound is smaller than 1 MiB. */
    if (frame > UINT32_MAX ||
        !a64_materialize(b, 12U, frame)) return false;
    return a64_rrr(b, allocate ? MINIC_A64_MACHINE_SUB :
        MINIC_A64_MACHINE_ADD, MINIC_A64_MACHINE_SP,
        MINIC_A64_MACHINE_SP, 12U);
}

static bool a64_frame_check(const MinicMachineLP64FrameContract *f) {
    const bool realigned = f != NULL && f->frame_alignment > 16U;
    return f != NULL && f->frame_alignment >= 16U &&
        f->frame_size <= MINIC_LARGE_FRAME_MAX &&
        f->frame_alignment <= 256U &&
        (f->frame_alignment & (f->frame_alignment - 1U)) == 0U &&
        ((f->saved_entry_sp_offset != SIZE_MAX) == realigned) &&
        (!realigned || (f->has_dynamic_stack &&
            f->saved_entry_sp_offset != SIZE_MAX));
}

bool minic_a64_machine_emit_prologue(MinicMachineBuilder *b,
    const MinicMachineLP64FrameContract *f) {
    const MinicA64ScratchPolicy scratch = {0U, 0U};
    MinicA64FrameView v;
    const bool realigned = f != NULL && f->frame_alignment > 16U;
    size_t i;
    if (!a64_frame_check(f)) return false;
    v = (MinicA64FrameView){f->has_dynamic_stack, f->frame_size,
        0U, f->saved_entry_sp_offset};
    if ((realigned && !a64_rr(b, MINIC_A64_MACHINE_MOV, 14U, 19U)) ||
        !a64_pair(b, true) ||
        !a64_rr(b, MINIC_A64_MACHINE_MOV, 29U, MINIC_A64_MACHINE_SP) ||
        (realigned && !a64_rr(b, MINIC_A64_MACHINE_MOV, 15U,
            MINIC_A64_MACHINE_SP)) ||
        !a64_sp_frame_delta(b, f->frame_size, true)) return false;
    if (realigned) {
        if (!a64_rri(b, MINIC_A64_MACHINE_ADD_IMM, 9U,
                MINIC_A64_MACHINE_SP, (int64_t)(f->frame_alignment - 1U)) ||
            !a64_rri(b, MINIC_A64_MACHINE_AND_ALIGN_IMM, 9U,
                9U, -(int64_t)f->frame_alignment) ||
            !a64_rr(b, MINIC_A64_MACHINE_MOV, 19U, 9U) ||
            !a64_owned(b, &v, true, MINIC_A64_REG_X, 14U,
                f->save_offset + (f->has_calls ? 8U : 0U)) ||
            !a64_owned(b, &v, true, MINIC_A64_REG_X, 15U,
                f->saved_entry_sp_offset) ||
            (f->has_calls && !a64_owned(b, &v, true,
                MINIC_A64_REG_X, 30U, f->save_offset))) return false;
    } else if (f->has_calls && !minic_a64_machine_emit_frame_store(
        b, &v, (MinicA64FrameRef){MINIC_A64_FRAME_PHYSICAL_SP,
            f->save_offset},
        (MinicA64FrameRegister){MINIC_A64_REG_X, 30U}, scratch))
        return false;
    if (f->has_dynamic_stack && !realigned &&
        (!minic_a64_machine_emit_frame_store(b, &v,
            (MinicA64FrameRef){MINIC_A64_FRAME_PHYSICAL_SP,
                f->save_offset + (f->has_calls ? 8U : 0U)},
            (MinicA64FrameRegister){MINIC_A64_REG_X, 19U}, scratch) ||
        !a64_rr(b, MINIC_A64_MACHINE_MOV, 19U, MINIC_A64_MACHINE_SP)))
        return false;
    if (f->variadic_count != 0U)
        for (i = 0U; i < 8U; ++i) {
            if (!a64_owned(b, &v, true, MINIC_A64_REG_X,
                    (unsigned int)i, f->variadic_save_base + 32U + i * 8U) ||
                !a64_owned(b, &v, true, MINIC_A64_REG_Q,
                    (unsigned int)i, f->variadic_save_base + 96U + i * 16U))
                return false;
        }
    return !f->has_indirect_return || a64_owned(b, &v, true,
        MINIC_A64_REG_X, 8U, f->indirect_return_offset);
}

bool minic_a64_machine_emit_epilogue(MinicMachineBuilder *b,
    const MinicMachineLP64FrameContract *f) {
    MinicA64FrameView v;
    MinicMachineOperand none[1] = {{0}};
    if (!a64_frame_check(f)) return false;
    v = (MinicA64FrameView){f->has_dynamic_stack, f->frame_size,
        0U, f->saved_entry_sp_offset};
    if (f->saved_entry_sp_offset != SIZE_MAX) {
        if (!a64_owned(b, &v, false, MINIC_A64_REG_X, 9U,
                f->saved_entry_sp_offset) ||
            !a64_owned(b, &v, false, MINIC_A64_REG_X, 10U,
                f->save_offset + (f->has_calls ? 8U : 0U)) ||
            (f->has_calls && !a64_owned(b, &v, false,
                MINIC_A64_REG_X, 30U, f->save_offset)) ||
            !a64_rr(b, MINIC_A64_MACHINE_MOV, MINIC_A64_MACHINE_SP, 9U) ||
            !a64_rr(b, MINIC_A64_MACHINE_MOV, 19U, 10U)) return false;
    } else {
        if ((f->has_calls && !a64_owned(b, &v, false,
                MINIC_A64_REG_X, 30U, f->save_offset)) ||
            (f->has_dynamic_stack &&
                (!a64_rr(b, MINIC_A64_MACHINE_MOV,
                    MINIC_A64_MACHINE_SP, 19U) ||
                 !a64_owned(b, &v, false, MINIC_A64_REG_X, 19U,
                    f->save_offset + (f->has_calls ? 8U : 0U)))) ||
            !a64_sp_frame_delta(b, f->frame_size, false)) return false;
    }
    return a64_pair(b, false) &&
        a64_selected(b, MINIC_A64_MACHINE_RET, none, 0U);
}
