#include "target/riscv/machine_frame_select.h"
#include "target/riscv/machine_opcode.h"
#include "target/riscv/machine_target_state.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

enum {
    RV_ZERO = 0U, RV_RA = 1U, RV_SP = 2U,
    RV_T0 = 5U, RV_T1 = 6U, RV_T2 = 7U,
    RV_S0 = 8U, RV_A0 = 10U, RV_A7 = 17U,
    RV_T4 = 29U, RV_T6 = 31U
};

static bool rv_power_two(size_t number) {
    return number != 0U && (number & (number - 1U)) == 0U;
}

static unsigned rv_xlen(const MinicMachineBuilder *builder) {
    MinicRiscvIsa isa;
    if (builder == NULL || builder->function == NULL ||
        builder->function->target == NULL ||
        !minic_riscv_target_isa_load(builder->function->target, &isa) ||
        (isa.xlen != 32U && isa.xlen != 64U)) return 0U;
    return isa.xlen;
}

static MinicMachineOperand rv_register(unsigned xlen, unsigned number,
                                      unsigned access) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_REGISTER;
    result.value.physical.reg = (MinicMachineRegister){
        number, (uint16_t)xlen, MINIC_MACHINE_REGISTER_GENERAL};
    result.value.physical.access = access;
    return result;
}

static MinicMachineOperand rv_integer(int64_t number) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_INTEGER;
    result.value.integer = number;
    return result;
}

static MinicMachineOperand rv_address(unsigned xlen, unsigned base,
                                     int64_t displacement) {
    MinicMachineOperand result;
    /* Initializing a union's first member does not initialize its larger
     * address alternative. Zero the entire relocation-bearing operand. */
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    result.value.address.target_mode =
        MINIC_RISCV_MACHINE_ADDRESS_BASE_DISPLACEMENT;
    result.value.address.has_base = true;
    result.value.address.base = (MinicMachineRegister){
        base, (uint16_t)xlen, MINIC_MACHINE_REGISTER_GENERAL};
    result.value.address.scale = 1U;
    result.value.address.displacement = displacement;
    return result;
}

bool minic_riscv_machine_emit_selected_effects(MinicMachineBuilder *builder,
    uint32_t opcode, unsigned int extra_effects,
    const MinicMachineOperand *operands, size_t count) {
    MinicMachineOpcodeContract contract = {0};
    const MinicTargetInfo *target;
    size_t i;
    if (builder == NULL || builder->function == NULL ||
        (target = builder->function->target) == NULL ||
        !minic_riscv_machine_lookup_opcode_contract(
            target, opcode, &contract, NULL) ||
        (extra_effects & ~contract.permitted_effects) != 0U ||
        contract.operand_count != count ||
        (count != 0U && operands == NULL)) return false;
    for (i = 0U; i < count; ++i)
        if (operands[i].kind != contract.operand_kinds[i] ||
            (operands[i].kind == MINIC_MACHINE_OPERAND_REGISTER &&
             operands[i].value.physical.access != contract.register_access[i]))
            return false;
    return minic_machine_builder_emit_selected(builder, opcode,
        contract.required_effects | extra_effects, operands, count,
        contract.implicit_count != 0U ? contract.implicit : NULL,
        contract.implicit_count);
}

bool minic_riscv_machine_emit_selected(MinicMachineBuilder *builder,
    uint32_t opcode, const MinicMachineOperand *operands, size_t count) {
    return minic_riscv_machine_emit_selected_effects(
        builder, opcode, 0U, operands, count);
}

#define rv_emit minic_riscv_machine_emit_selected

static bool rv_li(MinicMachineBuilder *builder, unsigned dst, int64_t number) {
    const unsigned xlen = rv_xlen(builder);
    MinicMachineOperand args[2];
    if (xlen == 0U || dst >= 32U) return false;
    args[0] = rv_register(xlen, dst, MINIC_MACHINE_REGISTER_WRITE);
    args[1] = rv_integer(number);
    return rv_emit(builder, MINIC_RISCV_MACHINE_LI, args, 2U);
}

static bool rv_rr(MinicMachineBuilder *builder, uint32_t opcode,
                  unsigned dst, unsigned src) {
    const unsigned xlen = rv_xlen(builder);
    MinicMachineOperand args[2];
    if (xlen == 0U || dst >= 32U || src >= 32U) return false;
    args[0] = rv_register(xlen, dst, MINIC_MACHINE_REGISTER_WRITE);
    args[1] = rv_register(xlen, src, MINIC_MACHINE_REGISTER_READ);
    return rv_emit(builder, opcode, args, 2U);
}

static bool rv_rrr(MinicMachineBuilder *builder, uint32_t opcode,
                   unsigned dst, unsigned src1, unsigned src2) {
    const unsigned xlen = rv_xlen(builder);
    MinicMachineOperand args[3];
    if (xlen == 0U || dst >= 32U || src1 >= 32U || src2 >= 32U) return false;
    args[0] = rv_register(xlen, dst, MINIC_MACHINE_REGISTER_WRITE);
    args[1] = rv_register(xlen, src1, MINIC_MACHINE_REGISTER_READ);
    args[2] = rv_register(xlen, src2, MINIC_MACHINE_REGISTER_READ);
    return rv_emit(builder, opcode, args, 3U);
}

static bool rv_addi(MinicMachineBuilder *builder, unsigned dst, unsigned src,
                    int64_t number) {
    const unsigned xlen = rv_xlen(builder);
    MinicMachineOperand args[3];
    if (xlen == 0U || dst >= 32U || src >= 32U ||
        number < -2048 || number > 2047) return false;
    args[0] = rv_register(xlen, dst, MINIC_MACHINE_REGISTER_WRITE);
    args[1] = rv_register(xlen, src, MINIC_MACHINE_REGISTER_READ);
    args[2] = rv_integer(number);
    return rv_emit(builder, MINIC_RISCV_MACHINE_ADDI, args, 3U);
}

static uint32_t rv_memory_opcode(unsigned xlen, bool load, unsigned bytes,
                                 bool frame) {
    if (bytes == 1U) return load ? (frame ?
        MINIC_RISCV_MACHINE_LB_FRAME : MINIC_RISCV_MACHINE_LB) :
        (frame ? MINIC_RISCV_MACHINE_SB_FRAME : MINIC_RISCV_MACHINE_SB);
    if (bytes == 2U) return load ? (frame ?
        MINIC_RISCV_MACHINE_LH_FRAME : MINIC_RISCV_MACHINE_LH) :
        (frame ? MINIC_RISCV_MACHINE_SH_FRAME : MINIC_RISCV_MACHINE_SH);
    if (bytes == 4U) return load ? (frame ?
        MINIC_RISCV_MACHINE_LW_FRAME : MINIC_RISCV_MACHINE_LW) :
        (frame ? MINIC_RISCV_MACHINE_SW_FRAME : MINIC_RISCV_MACHINE_SW);
    if (bytes == 8U && xlen == 64U) return load ? (frame ?
        MINIC_RISCV_MACHINE_LD_FRAME : MINIC_RISCV_MACHINE_LD) :
        (frame ? MINIC_RISCV_MACHINE_SD_FRAME : MINIC_RISCV_MACHINE_SD);
    return MINIC_RISCV_MACHINE_INVALID;
}

bool minic_riscv_machine_select_frame_transfer(MinicMachineBuilder *builder,
    unsigned register_number, unsigned base_register, size_t offset,
    bool load, unsigned bytes) {
    const unsigned xlen = rv_xlen(builder);
    uint32_t opcode;
    MinicMachineOperand args[2];
    if (xlen == 0U || register_number >= 32U || base_register >= 32U ||
        offset > (size_t)INT64_MAX ||
        (xlen == 32U && offset > (size_t)INT32_MAX) ||
        (opcode = rv_memory_opcode(xlen, load, bytes, false)) == 0U ||
        offset % bytes != 0U) return false;
    if (offset > 2047U) {
        /* t6 is a temporary ADDRESS, not a special string-based offset.
         * Reject a store from t6 or address based on t6 before it clobbers
         * the value/base. Loads into t6 are legal. */
        if (base_register == RV_T6 ||
            (!load && register_number == RV_T6) ||
            !rv_li(builder, RV_T6, (int64_t)offset) ||
            !rv_rrr(builder, MINIC_RISCV_MACHINE_ADD,
                    RV_T6, base_register, RV_T6)) return false;
        base_register = RV_T6;
        offset = 0U;
    }
    args[0] = rv_register(xlen, register_number,
        load ? MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
    args[1] = rv_address(xlen, base_register, (int64_t)offset);
    return rv_emit(builder, opcode, args, 2U);
}

bool minic_riscv_machine_select_frame_object(MinicMachineBuilder *builder,
    size_t frame_index, size_t byte_displacement, unsigned register_number,
    bool load, unsigned bytes) {
    const unsigned xlen = rv_xlen(builder);
    uint32_t opcode;
    MinicMachineOperand args[2] = {{0}};
    if (xlen == 0U || register_number >= 32U ||
        byte_displacement > (size_t)INT64_MAX ||
        (opcode = rv_memory_opcode(xlen, load, bytes, true)) == 0U)
        return false;
    args[0] = rv_register(xlen, register_number,
        load ? MINIC_MACHINE_REGISTER_WRITE : MINIC_MACHINE_REGISTER_READ);
    args[1].kind = MINIC_MACHINE_OPERAND_FRAME;
    args[1].value.frame.index = frame_index;
    args[1].value.frame.displacement = (int64_t)byte_displacement;
    return rv_emit(builder, opcode, args, 2U);
}

static bool rv_delta_sp(MinicMachineBuilder *builder, size_t bytes,
                        bool allocate) {
    if (bytes == 0U) return true;
    if (bytes <= 2047U)
        return rv_addi(builder, RV_SP, RV_SP,
            allocate ? -(int64_t)bytes : (int64_t)bytes);
    if (bytes > (size_t)INT64_MAX ||
        !rv_li(builder, RV_T0, (int64_t)bytes)) return false;
    return rv_rrr(builder, allocate ? MINIC_RISCV_MACHINE_SUB :
        MINIC_RISCV_MACHINE_ADD, RV_SP, RV_SP, RV_T0);
}

static bool rv_slot_ok(const MinicRiscvPhysicalFrame *frame,
                       size_t offset, size_t word) {
    return offset % word == 0U && offset <= frame->frame_size &&
           frame->frame_size - offset >= word;
}

static bool rv_saved_gpr(unsigned id) {
    return id == 8U || id == 9U || (id >= 18U && id <= 27U);
}

static bool rv_slot_outside_regions(const MinicRiscvPhysicalFrame *frame,
                                    size_t slot, size_t word) {
    if (!rv_slot_ok(frame, slot, word) || slot < frame->outgoing_size ||
        (frame->variadic && frame->varargs_size != 0U &&
         slot < frame->varargs_offset + frame->varargs_size &&
         slot + word > frame->varargs_offset)) return false;
    return true;
}

static bool rv_check_frame(MinicMachineBuilder *builder,
                           const MinicRiscvPhysicalFrame *frame) {
    const unsigned xlen = rv_xlen(builder);
    const size_t word = xlen / 8U;
    size_t i;
    if (xlen == 0U || frame == NULL || !rv_power_two(frame->stack_alignment) ||
        frame->stack_alignment < 16U || frame->stack_alignment > (size_t)INT32_MAX ||
        frame->frame_size > (size_t)INT32_MAX ||
        frame->frame_size % 16U != 0U ||
        frame->outgoing_size > frame->frame_size ||
        frame->first_variadic_gpr > 8U ||
        frame->named_stack_argument_slots > (size_t)INT32_MAX / word ||
        frame->has_dynamic_stack_alignment !=
            (frame->stack_alignment > 16U) ||
        (frame->has_dynamic_stack_alignment &&
         !rv_slot_ok(frame, frame->entry_sp_offset, word)) ||
        (frame->saves_return_address &&
         !rv_slot_ok(frame, frame->return_address_offset, word)) ||
        (frame->has_hidden_result_pointer &&
         !rv_slot_ok(frame, frame->hidden_result_pointer_offset, word)) ||
        (frame->has_dynamic_stack &&
         !rv_slot_ok(frame, frame->saved_frame_base_offset, word)) ||
        (frame->dynamic_call_area_size != 0U &&
         (!frame->has_dynamic_stack ||
          frame->dynamic_call_area_size > (size_t)INT32_MAX ||
          frame->dynamic_call_area_size % 16U != 0U)) ||
        (!frame->variadic && frame->varargs_size != 0U) ||
        (frame->variadic &&
         frame->varargs_size != (8U - frame->first_variadic_gpr) * word) ||
        frame->varargs_offset > frame->frame_size ||
        frame->varargs_size > frame->frame_size - frame->varargs_offset ||
        frame->callee_saved_count > 12U ||
        (frame->callee_saved_count != 0U && frame->callee_saved == NULL))
        return false;
    for (i = 0U; i < frame->callee_saved_count; ++i) {
        size_t j;
        if (!rv_saved_gpr(frame->callee_saved[i].register_number) ||
            !rv_slot_outside_regions(frame,
                frame->callee_saved[i].offset, word))
            return false;
        for (j = 0U; j < i; ++j)
            if (frame->callee_saved[j].register_number ==
                frame->callee_saved[i].register_number ||
                frame->callee_saved[j].offset == frame->callee_saved[i].offset)
                return false;
    }
    /* Hidden sret/RA/original-SP/s0 all have independent ABI ownership.
     * Alias of two saved slots is a planning bug, not a legalization hint. */
    if ((frame->saves_return_address &&
         ((frame->has_hidden_result_pointer &&
           frame->return_address_offset == frame->hidden_result_pointer_offset) ||
          (frame->has_dynamic_stack &&
           frame->return_address_offset == frame->saved_frame_base_offset) ||
          (frame->has_dynamic_stack_alignment &&
           frame->return_address_offset == frame->entry_sp_offset))) ||
        (frame->has_hidden_result_pointer &&
         ((frame->has_dynamic_stack &&
           frame->hidden_result_pointer_offset == frame->saved_frame_base_offset) ||
          (frame->has_dynamic_stack_alignment &&
           frame->hidden_result_pointer_offset == frame->entry_sp_offset))) ||
        (frame->has_dynamic_stack && frame->has_dynamic_stack_alignment &&
         frame->saved_frame_base_offset == frame->entry_sp_offset)) return false;
    for (i = 0U; i < frame->callee_saved_count; ++i) {
        const size_t slot = frame->callee_saved[i].offset;
        if ((frame->saves_return_address &&
             slot == frame->return_address_offset) ||
            (frame->has_hidden_result_pointer &&
             slot == frame->hidden_result_pointer_offset) ||
            (frame->has_dynamic_stack &&
             slot == frame->saved_frame_base_offset) ||
            (frame->has_dynamic_stack_alignment &&
             slot == frame->entry_sp_offset)) return false;
    }
    /* All non-callee fixed reservations obey the same overlap boundary.
     * This avoids accidental overwrite by outgoing arguments/varargs. */
    {
        const size_t reserved[4] = {
            frame->return_address_offset,
            frame->hidden_result_pointer_offset,
            frame->saved_frame_base_offset,
            frame->entry_sp_offset
        };
        const bool active[4] = {
            frame->saves_return_address,
            frame->has_hidden_result_pointer,
            frame->has_dynamic_stack,
            frame->has_dynamic_stack_alignment
        };
        for (i = 0U; i < 4U; ++i)
            if (active[i] &&
                !rv_slot_outside_regions(frame, reserved[i], word)) return false;
    }
    return true;
}

static bool rv_fixed_transfer(MinicMachineBuilder *builder,
    const MinicRiscvPhysicalFrame *frame, unsigned reg, size_t offset,
    bool load) {
    const unsigned xlen = rv_xlen(builder);
    return minic_riscv_machine_select_frame_transfer(builder, reg,
        frame->has_dynamic_stack ? RV_S0 : RV_SP, offset,
        load, xlen / 8U);
}

bool minic_riscv_machine_select_prologue(MinicMachineBuilder *builder,
    const MinicRiscvPhysicalFrame *frame, size_t core_entry_block) {
    const unsigned xlen = rv_xlen(builder);
    const size_t word = xlen / 8U;
    size_t i;
    if (!rv_check_frame(builder, frame) ||
        builder->function->has_dynamic_stack != frame->has_dynamic_stack ||
        builder->function->frame_size != frame->frame_size ||
        builder->function->frame_alignment != 16U ||
        builder->function->block_count < 2U || core_entry_block != 1U ||
        !minic_machine_builder_position_at(builder, 0U)) return false;
    minic_machine_builder_clear_origin(builder);
    if (frame->has_dynamic_stack_alignment) {
        if (!rv_rr(builder, MINIC_RISCV_MACHINE_MV, RV_T2, RV_SP) ||
            !rv_delta_sp(builder, frame->frame_size, true) ||
            !rv_li(builder, RV_T1, -(int64_t)frame->stack_alignment) ||
            !rv_rrr(builder, MINIC_RISCV_MACHINE_AND,
                    RV_SP, RV_SP, RV_T1) ||
            !minic_riscv_machine_select_frame_transfer(builder,
                RV_T2, RV_SP, frame->entry_sp_offset, false, (unsigned)word))
            return false;
    } else if (!rv_delta_sp(builder, frame->frame_size, true)) return false;
    if (frame->has_dynamic_stack &&
        (!minic_riscv_machine_select_frame_transfer(builder, RV_S0, RV_SP,
            frame->saved_frame_base_offset, false, (unsigned)word) ||
         !rv_rr(builder, MINIC_RISCV_MACHINE_MV, RV_S0, RV_SP) ||
         !rv_delta_sp(builder, frame->dynamic_call_area_size, true)))
        return false;
    if ((frame->saves_return_address &&
         !rv_fixed_transfer(builder, frame,
             RV_RA, frame->return_address_offset, false)) ||
        (frame->has_hidden_result_pointer &&
         !rv_fixed_transfer(builder, frame,
             RV_A0, frame->hidden_result_pointer_offset, false))) return false;
    for (i = 0U; i < frame->callee_saved_count; ++i)
        if (!rv_fixed_transfer(builder, frame,
             frame->callee_saved[i].register_number,
             frame->callee_saved[i].offset, false)) return false;
    if (frame->variadic && frame->varargs_size != 0U) {
        if (frame->has_dynamic_stack_alignment &&
            !rv_fixed_transfer(builder, frame,
                RV_T0, frame->entry_sp_offset, true)) return false;
        for (i = frame->first_variadic_gpr; i < 8U; ++i) {
            const unsigned arg = (unsigned)(RV_A0 + i);
            const size_t offset = (i - frame->first_variadic_gpr) * word;
            if (frame->has_dynamic_stack_alignment) {
                /* Offset is deliberately negative relative to entry SP;
                 * encode only a provably bounded 64-byte vararg save. */
                const int64_t negative = -(int64_t)(frame->varargs_size - offset);
                MinicMachineOperand args[2];
                args[0] = rv_register(xlen, arg, MINIC_MACHINE_REGISTER_READ);
                args[1] = rv_address(xlen, RV_T0, negative);
                if (!rv_emit(builder, xlen == 32U ?
                    MINIC_RISCV_MACHINE_SW : MINIC_RISCV_MACHINE_SD,
                    args, 2U)) return false;
            } else if (!rv_fixed_transfer(builder, frame, arg,
                           frame->varargs_offset + offset, false)) return false;
        }
    }
    return minic_machine_builder_add_successor(builder, core_entry_block) &&
           minic_machine_builder_set_fallthrough(builder, core_entry_block);
}

bool minic_riscv_machine_select_epilogue(MinicMachineBuilder *builder,
    const MinicRiscvPhysicalFrame *frame, size_t exit_block) {
    const unsigned xlen = rv_xlen(builder);
    const size_t word = xlen / 8U;
    size_t i;
    if (!rv_check_frame(builder, frame) ||
        builder->function->has_dynamic_stack != frame->has_dynamic_stack ||
        builder->function->frame_size != frame->frame_size ||
        exit_block <= 1U || exit_block >= builder->function->block_count ||
        !minic_machine_builder_position_at(builder, exit_block)) return false;
    minic_machine_builder_clear_origin(builder);
    if (frame->saves_return_address &&
        !rv_fixed_transfer(builder, frame,
            RV_RA, frame->return_address_offset, true)) return false;
    for (i = frame->callee_saved_count; i != 0U; --i)
        if (!rv_fixed_transfer(builder, frame,
            frame->callee_saved[i - 1U].register_number,
            frame->callee_saved[i - 1U].offset, true)) return false;
    if (frame->has_dynamic_stack) {
        if (!rv_fixed_transfer(builder, frame,
            RV_T4, frame->saved_frame_base_offset, true)) return false;
        if (frame->has_dynamic_stack_alignment) {
            if (!rv_fixed_transfer(builder, frame,
                    RV_T0, frame->entry_sp_offset, true)) return false;
        } else if (frame->frame_size <= 2047U) {
            if (!rv_addi(builder, RV_T0, RV_S0,
                    (int64_t)frame->frame_size)) return false;
        } else if (!rv_li(builder, RV_T0, (int64_t)frame->frame_size) ||
                   !rv_rrr(builder, MINIC_RISCV_MACHINE_ADD,
                            RV_T0, RV_S0, RV_T0)) return false;
        if (!rv_rr(builder, MINIC_RISCV_MACHINE_MV, RV_SP, RV_T0) ||
            !rv_rr(builder, MINIC_RISCV_MACHINE_MV, RV_S0, RV_T4))
            return false;
    } else if (frame->has_dynamic_stack_alignment) {
        if (!minic_riscv_machine_select_frame_transfer(builder,
                RV_T0, RV_SP, frame->entry_sp_offset,
                true, (unsigned)word) ||
            !rv_rr(builder, MINIC_RISCV_MACHINE_MV, RV_SP, RV_T0))
            return false;
    } else if (!rv_delta_sp(builder, frame->frame_size, false)) return false;
    return rv_emit(builder, MINIC_RISCV_MACHINE_RET, NULL, 0U);
}
