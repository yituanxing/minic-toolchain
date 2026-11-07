#include "target/x86_64/machine_frame_select.h"

#include "target/x86_64/codegen_options.h"
#include "target/x86_64/frame.h"
#include "target/x86_64/machine_opcode.h"
#include "target/x86_64/machine_target_state.h"

#include <limits.h>
#include <string.h>

static MinicMachineOperand x86_reg(MinicMachineRegisterBank bank,
    unsigned int number, unsigned int width) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_REGISTER;
    operand.value.physical.reg = (MinicMachineRegister){
        number, (uint16_t)width, bank};
    return operand;
}

static MinicMachineOperand x86_gp(unsigned int number, unsigned int width) {
    return x86_reg(MINIC_MACHINE_REGISTER_GENERAL, number, width);
}

static MinicMachineOperand x86_imm(int64_t value) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_INTEGER;
    operand.value.integer = value;
    return operand;
}

static MinicMachineOperand x86_block(size_t id) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_BLOCK;
    operand.value.block_id = id;
    return operand;
}

static MinicMachineOperand x86_symbol(const char *name, uint32_t relocation) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_SYMBOL;
    operand.value.symbol.name = (char *)name;
    operand.value.symbol.relocation = relocation;
    return operand;
}

static MinicMachineOperand x86_address(unsigned int base, int64_t offset) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    operand.value.address.has_base = true;
    operand.value.address.base = (MinicMachineRegister){
        base, 64U, MINIC_MACHINE_REGISTER_GENERAL};
    operand.value.address.scale = 1U;
    operand.value.address.displacement = offset;
    return operand;
}

static bool x86_emit(MinicMachineBuilder *builder,
    MinicX86MachineOpcode opcode, MinicMachineOperand *operands,
    size_t count) {
    MinicMachineOpcodeContract contract = {0};
    size_t i;
    if (builder == NULL || builder->function == NULL ||
        !minic_x86_64_machine_opcode_lookup(builder->function->target,
            opcode, &contract, NULL) || count != contract.operand_count)
        return false;
    for (i = 0U; i < count; ++i) {
        if (operands[i].kind != contract.operand_kinds[i]) return false;
        if (operands[i].kind == MINIC_MACHINE_OPERAND_REGISTER)
            operands[i].value.physical.access = contract.register_access[i];
    }
    return minic_machine_builder_emit_selected(builder, opcode,
        contract.required_effects, count != 0U ? operands : NULL, count,
        contract.implicit_count ? contract.implicit : NULL,
        contract.implicit_count);
}

static bool x86_emit0(MinicMachineBuilder *builder,
    MinicX86MachineOpcode opcode) {
    return x86_emit(builder, opcode, NULL, 0U);
}

static bool x86_emit1(MinicMachineBuilder *builder,
    MinicX86MachineOpcode opcode, MinicMachineOperand a) {
    return x86_emit(builder, opcode, &a, 1U);
}

static bool x86_emit2(MinicMachineBuilder *builder,
    MinicX86MachineOpcode opcode, MinicMachineOperand a,
    MinicMachineOperand b) {
    MinicMachineOperand operands[2] = {a, b};
    return x86_emit(builder, opcode, operands, 2U);
}

bool minic_x86_64_machine_frame_view(const MinicX86Frame *frame,
    MinicFunctionCallingConvention convention, MinicX86MachineFrameView *out) {
    if (frame == NULL || out == NULL || frame->frame_size > INT32_MAX ||
        frame->object_base_alignment > INT32_MAX ||
        (convention != MINIC_CALLING_CONVENTION_DEFAULT &&
         convention != MINIC_CALLING_CONVENTION_MS_X64)) return false;
    *out = (MinicX86MachineFrameView){
        .frame_size = frame->frame_size,
        .calling_convention = convention,
        .ms_gp_save_offset = frame->ms_nonvolatile_gp_save_offset,
        .ms_xmm_save_offset = frame->ms_nonvolatile_xmm_save_offset,
        .has_sret = frame->has_sret,
        .sret_save_offset = frame->sret_save_offset,
        .saves_incoming_gp = frame->saves_incoming_gp,
        .incoming_gp_save_offset = frame->incoming_gp_save_offset,
        .objects_from_aligned_rsp = frame->objects_from_aligned_rsp,
        .object_base_alignment = frame->object_base_alignment,
        .has_varargs = frame->varargs_count != 0U,
        .varargs_regsave_offset = frame->varargs_regsave_offset,
    };
    return true;
}

bool minic_x86_64_machine_frame_slot(MinicMachineBuilder *builder,
    MinicMachineFrameBase base, MinicMachineFrameArea area,
    size_t width, size_t alignment, int64_t offset,
    MinicMachineOperand *operand) {
    MinicMachineFrameObject object = {0};
    size_t index;
    if (builder == NULL || builder->function == NULL || operand == NULL ||
        width == 0U || alignment == 0U || width > INT64_MAX ||
        offset < INT32_MIN || offset > INT32_MAX) return false;
    object.size_bytes = width;
    object.alignment_bytes = alignment;
    object.area = area;
    object.base = base;
    object.has_fixed_offset = true;
    object.fixed_offset = offset;
    if (!minic_machine_function_add_frame_object(
            builder->function, &object, &index)) return false;
    *operand = (MinicMachineOperand){0};
    operand->kind = MINIC_MACHINE_OPERAND_FRAME;
    operand->value.frame.index = index;
    return true;
}

static bool x86_local(MinicMachineBuilder *builder, size_t width,
    size_t rbp_distance, MinicMachineOperand *operand) {
    if (rbp_distance == 0U || rbp_distance > INT32_MAX) return false;
    return minic_x86_64_machine_frame_slot(builder,
        MINIC_MACHINE_FRAME_BASE_FRAME_POINTER,
        MINIC_MACHINE_FRAME_LOCAL, width, 1U,
        -(int64_t)rbp_distance, operand);
}

static bool x86_save_gp(MinicMachineBuilder *builder,
    unsigned int reg, size_t rbp_distance) {
    MinicMachineOperand frame;
    return x86_local(builder, 8U, rbp_distance, &frame) &&
        x86_emit2(builder, MINIC_X86_MI_MOV64_R_FRBP,
            x86_gp(reg, 64U), frame);
}

static bool x86_restore_gp(MinicMachineBuilder *builder,
    unsigned int reg, size_t rbp_distance) {
    MinicMachineOperand frame;
    return x86_local(builder, 8U, rbp_distance, &frame) &&
        x86_emit2(builder, MINIC_X86_MI_MOV64_FRBP_R,
            frame, x86_gp(reg, 64U));
}

static bool x86_save_xmm(MinicMachineBuilder *builder,
    unsigned int reg, size_t rbp_distance) {
    MinicMachineOperand frame;
    return x86_local(builder, 16U, rbp_distance, &frame) &&
        x86_emit2(builder, MINIC_X86_MI_MOVDQU_XMM_FRAME_RBP,
            x86_reg(MINIC_MACHINE_REGISTER_VECTOR, reg, 128U), frame);
}

static bool x86_restore_xmm(MinicMachineBuilder *builder,
    unsigned int reg, size_t rbp_distance) {
    MinicMachineOperand frame;
    return x86_local(builder, 16U, rbp_distance, &frame) &&
        x86_emit2(builder, MINIC_X86_MI_MOVDQU_FRAME_RBP_XMM,
            frame, x86_reg(MINIC_MACHINE_REGISTER_VECTOR, reg, 128U));
}

/* SysV and MS x64 physical register IDs, not textual ABI register names. */
static const unsigned int x86_sysv_incoming_gp[] = {
    MINIC_X86_REG_RDI, MINIC_X86_REG_RSI, MINIC_X86_REG_RDX,
    MINIC_X86_REG_RCX, MINIC_X86_REG_R8, MINIC_X86_REG_R9
};
static const unsigned int x86_ms_nonvolatile_gp[] = {
    MINIC_X86_REG_RDI, MINIC_X86_REG_RSI
};

static bool x86_local_range(size_t frame_bytes, size_t rbp_distance,
    size_t width) {
    return width != 0U && rbp_distance >= width &&
        rbp_distance <= frame_bytes && rbp_distance <= INT32_MAX;
}

static bool x86_frame_view_valid(const MinicMachineFunction *function,
    const MinicX86MachineFrameView *frame) {
    if (function == NULL || frame == NULL ||
        function->architecture != MINIC_TARGET_ARCH_X86_64 ||
        function->calling_convention != frame->calling_convention ||
        function->frame_size != frame->frame_size ||
        function->frame_alignment != 16U ||
        frame->frame_size > INT32_MAX ||
        frame->object_base_alignment > INT32_MAX ||
        (frame->objects_from_aligned_rsp &&
         (frame->object_base_alignment <= 16U ||
          (frame->object_base_alignment &
           (frame->object_base_alignment - 1U)) != 0U)) ||
        (frame->has_sret &&
         !x86_local_range(frame->frame_size,
             frame->sret_save_offset, 8U)) ||
        (frame->saves_incoming_gp &&
         (frame->incoming_gp_save_offset > SIZE_MAX - 40U ||
          !x86_local_range(frame->frame_size,
              frame->incoming_gp_save_offset + 40U, 8U))) ||
        (frame->has_varargs &&
         !x86_local_range(frame->frame_size,
             frame->varargs_regsave_offset, 176U)) ||
        (frame->calling_convention == MINIC_CALLING_CONVENTION_MS_X64 &&
         (frame->has_varargs || frame->has_sret ||
          frame->saves_incoming_gp || frame->ms_gp_save_offset == 0U ||
          frame->ms_xmm_save_offset == 0U)) ||
        (frame->calling_convention != MINIC_CALLING_CONVENTION_MS_X64 &&
         (frame->ms_gp_save_offset != 0U ||
          frame->ms_xmm_save_offset != 0U)) ||
        (frame->ms_gp_save_offset != 0U &&
         (frame->ms_gp_save_offset > SIZE_MAX - 8U ||
          !x86_local_range(frame->frame_size,
              frame->ms_gp_save_offset + 8U, 8U) ||
          frame->ms_xmm_save_offset > SIZE_MAX - 9U * 16U ||
          !x86_local_range(frame->frame_size,
              frame->ms_xmm_save_offset + 9U * 16U, 16U))))
        return false;
    return true;
}

bool minic_x86_64_machine_select_prologue(MinicMachineBuilder *builder,
    const MinicX86MachineFrameView *frame, MinicX86MachinePrologue *out) {
    MinicMachineFunction *function;
    const MinicTargetInfo *target;
    size_t entry, spill = SIZE_MAX, ready, i;
    bool use_fp_spill, can_clobber_xmm;
    if (builder == NULL || out == NULL ||
        (function = builder->function) == NULL ||
        (target = function->target) == NULL ||
        !x86_frame_view_valid(function, frame) ||
        function->block_count != 0U ||
        !minic_x86_64_machine_validate_abi(target,
            frame->calling_convention, NULL)) return false;
    can_clobber_xmm = (minic_x86_64_target_codegen_options(target) &
        MINIC_X86_CODEGEN_NO_SSE) == 0U;
    use_fp_spill = frame->has_varargs && can_clobber_xmm;
    if (!minic_machine_builder_create_block(
            builder, NULL, false, true, &entry) || entry != 0U)
        return false;
    if (use_fp_spill &&
        !minic_machine_builder_create_block(
            builder, NULL, false, true, &spill)) return false;
    if (use_fp_spill &&
        !minic_machine_builder_create_block(
            builder, NULL, false, true, &ready)) return false;
    if (!minic_machine_builder_position_at(builder, entry)) return false;
    if ((minic_x86_64_target_codegen_options(target) &
         MINIC_X86_CODEGEN_IBT_ENDBR) != 0U &&
        !x86_emit0(builder, MINIC_X86_MI_ENDBR64)) return false;
    if (!x86_emit1(builder, MINIC_X86_MI_PUSH64_R,
            x86_gp(MINIC_X86_REG_RBP, 64U)) ||
        !x86_emit2(builder, MINIC_X86_MI_MOV64_RR,
            x86_gp(MINIC_X86_REG_RSP, 64U),
            x86_gp(MINIC_X86_REG_RBP, 64U))) return false;
    if (frame->frame_size != 0U &&
        !x86_emit2(builder, MINIC_X86_MI_SUB64_IR,
            x86_imm((int64_t)frame->frame_size),
            x86_gp(MINIC_X86_REG_RSP, 64U))) return false;
    if (frame->ms_gp_save_offset != 0U) {
        /* RDI/RSI are always nonvolatile. Under -mno-sse the physical
         * selector cannot generate an XMM clobber (and preflight rejects
         * SSE opcodes), so emitting MOVDQU merely to save unused XMM6..15
         * would violate the target ISA restriction. Preserve the normal
         * MS x64 vector save set when SSE generation is enabled. */
        for (i = 0U; i < 2U; ++i)
            if (!x86_save_gp(builder, x86_ms_nonvolatile_gp[i],
                    frame->ms_gp_save_offset + i * 8U)) return false;
        if (can_clobber_xmm)
            for (i = 0U; i < 10U; ++i)
                if (!x86_save_xmm(builder, (unsigned int)(i + 6U),
                        frame->ms_xmm_save_offset + i * 16U)) return false;
    }
    if (frame->has_sret &&
        !x86_save_gp(builder, MINIC_X86_REG_RDI,
            frame->sret_save_offset)) return false;
    if (frame->objects_from_aligned_rsp &&
        !x86_emit2(builder, MINIC_X86_MI_AND64_IR,
            x86_imm(-(int64_t)frame->object_base_alignment),
            x86_gp(MINIC_X86_REG_RSP, 64U))) return false;
    if (frame->saves_incoming_gp)
        for (i = 0U; i < 6U; ++i)
            if (!x86_save_gp(builder, x86_sysv_incoming_gp[i],
                    frame->incoming_gp_save_offset + i * 8U)) return false;
    if (frame->has_varargs) {
        MinicMachineOperand fp_save;
        if (frame->varargs_regsave_offset < 176U ||
            !x86_local(builder, 176U,
                frame->varargs_regsave_offset, &fp_save) ||
            !x86_emit2(builder, MINIC_X86_MI_LEA64_FRBP_R,
                fp_save, x86_gp(MINIC_X86_REG_R10, 64U))) return false;
        for (i = 0U; i < 6U; ++i)
            if (!x86_emit2(builder, MINIC_X86_MI_MOV64_RA,
                    x86_gp(x86_sysv_incoming_gp[i], 64U),
                    x86_address(MINIC_X86_REG_R10, (int64_t)(i * 8U))))
                return false;
        if (use_fp_spill) {
            if (!x86_emit2(builder, MINIC_X86_MI_TEST8_RR,
                    x86_gp(MINIC_X86_REG_RAX, 8U),
                    x86_gp(MINIC_X86_REG_RAX, 8U)) ||
                !x86_emit1(builder, MINIC_X86_MI_JZ_B,
                    x86_block(ready)) ||
                !minic_machine_builder_add_successor(builder, ready) ||
                !minic_machine_builder_add_successor(builder, spill) ||
                !minic_machine_builder_set_fallthrough(builder, spill) ||
                !minic_machine_builder_position_at(builder, spill)) return false;
            for (i = 0U; i < 8U; ++i)
                if (!x86_emit2(builder, MINIC_X86_MI_MOVDQU_XMM_ADDRESS,
                        x86_reg(MINIC_MACHINE_REGISTER_VECTOR,
                            (unsigned int)i, 128U),
                        x86_address(MINIC_X86_REG_R10,
                            (int64_t)(48U + i * 16U)))) return false;
            if (!x86_emit1(builder, MINIC_X86_MI_JMP_B,
                    x86_block(ready)) ||
                !minic_machine_builder_add_successor(builder, ready) ||
                !minic_machine_builder_position_at(builder, ready))
                return false;
        }
    }
    *out = (MinicX86MachinePrologue){
        .entry_block = entry,
        .continuation_block = use_fp_spill ? ready : entry,
        .has_variadic_fp_split = use_fp_spill};
    return true;
}

bool minic_x86_64_machine_select_epilogue(MinicMachineBuilder *builder,
    const MinicX86MachineFrameView *frame) {
    const MinicTargetInfo *target;
    bool can_clobber_xmm;
    size_t i;
    if (builder == NULL || builder->function == NULL ||
        !builder->has_position ||
        (target = builder->function->target) == NULL ||
        !x86_frame_view_valid(builder->function, frame)) return false;
    can_clobber_xmm = (minic_x86_64_target_codegen_options(target) &
        MINIC_X86_CODEGEN_NO_SSE) == 0U;
    if (frame->ms_gp_save_offset != 0U) {
        /* XMM0 and RAX contain the return value and must remain untouched. */
        if (can_clobber_xmm)
            for (i = 0U; i < 10U; ++i)
                if (!x86_restore_xmm(builder, (unsigned int)(6U + i),
                        frame->ms_xmm_save_offset + i * 16U)) return false;
        for (i = 0U; i < 2U; ++i)
            if (!x86_restore_gp(builder, x86_ms_nonvolatile_gp[i],
                    frame->ms_gp_save_offset + i * 8U)) return false;
    }
    if (!x86_emit0(builder, MINIC_X86_MI_LEAVE)) return false;
    if ((minic_x86_64_target_codegen_options(target) &
         MINIC_X86_CODEGEN_RETURN_EXTERNAL_THUNK) != 0U)
        return x86_emit1(builder, MINIC_X86_MI_JMP_RETURN_THUNK,
            x86_symbol("__x86_return_thunk", MINIC_X86_MACHINE_RELOC_PC32));
    return x86_emit0(builder, MINIC_X86_MI_RET);
}
