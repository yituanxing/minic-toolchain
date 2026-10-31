#include "target/aarch64/block_emit.h"
#include "target/aarch64/scalar_normalize.h"
#include "target/aarch64/residual_emit.h"
#include "target/aarch64/memory_emit.h"
#include "target/aarch64/call_emit.h"
#include "target/aarch64/integer_emit.h"
#include "target/aarch64/fp_emit.h"
#include "target/aarch64/frame_emit.h"
#include "target/common/core_block_walk.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_machine_dispatch.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"
#include "target/aarch64/abi.h"
#include "target/data_layout.h"
#include "target/common/direct_program_emit.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The instruction-owned aggregate ingress and CFG terminator cases are
 * independent of the function-level preflight and frame lifetime. Machine
 * register syntax remains strictly owned by this ISA. */

bool minic_aarch64_emit_lp64_record_chunk_parameter(FILE *out,
    const MinicA64FrameView *view, size_t offset, size_t bytes, bool in_reg,
    size_t gp, size_t incoming_stack_offset) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U) ||
        !minic_a64_emit_owned_address(out, view, offset, 11U) ||
        (in_reg
         ? fprintf(out, "  mov x9, x%zu\n", gp) < 0
         : !minic_a64_emit_frame_load(out, view,
                 (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING,
                                    incoming_stack_offset},
                 (MinicA64FrameRegister){MINIC_A64_REG_X, 9U},
                 (MinicA64ScratchPolicy){0U, 0U})))
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  strb w9, [x11, #%zu]\n", b) < 0 ||
            (b + 1U < bytes && fputs("  lsr x9, x9, #8\n", out) == EOF))
            return false;
    return true;
}

bool minic_aarch64_emit_record_copy(FILE *out,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
            const MinicLP64FramePlan *frame_plan = ctx->frame_plan;
            const MinicCoreValueId dst = ins->value.record_copy.destination_address;
            const MinicCoreValueId src = ins->value.record_copy.source_address;
            const MinicA64FrameView view = {
                ctx->dynamic_frame, ctx->frame, 0U,
                ctx->saved_entry_sp_offset
            };
            size_t bytes;
            if (!minic_multi_direct_record_copy_extent(
                    ctx->program, ctx->target, ctx->fn, ins, &bytes))
                return false;
            if (!minic_a64_emit_owned_slot(out, &view, false,
                    MINIC_A64_REG_X, 10U, minic_lp64_frame_value_offset(frame_plan, dst)) ||
                !minic_a64_emit_owned_slot(out, &view, false,
                    MINIC_A64_REG_X, 11U, minic_lp64_frame_value_offset(frame_plan, src)))
                return false;
            if (bytes > 64U) {
                if (fprintf(out, "  movz x12, #%zu\n", bytes & 65535U) < 0 ||
                    (bytes > 65535U &&
                     fprintf(out, "  movk x12, #%zu, lsl #16\n",
                             bytes >> 16U) < 0) ||
                    fputs("1:\n"
                          "  ldrb w9, [x11], #1\n"
                          "  strb w9, [x10], #1\n"
                          "  subs x12, x12, #1\n"
                          "  b.ne 1b\n", out) == EOF)
                    return false;
            } else {
                for (size_t byte = 0U; byte < bytes; ++byte)
                    if (fputs("  ldrb w9, [x11]\n"
                              "  strb w9, [x10]\n"
                              "  add x11, x11, #1\n"
                              "  add x10, x10, #1\n", out) == EOF)
                        return false;
            }
            return true;
}

/* Source lvalue -> owned local record snapshot, retaining each byte
 * read (including volatile) in source order. The destination is a Core
 * DataLayout object, not a synthetic pointer SSA value. */
static bool minic_aarch64_emit_record_load(FILE *out,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    const MinicLP64FramePlan *frame_plan = ctx->frame_plan;
    MinicCoreValueId source;
    size_t dest, bytes, byte;
    const MinicA64FrameView view = {
        ctx->dynamic_frame, ctx->frame, 0U, ctx->saved_entry_sp_offset
    };
    if (!minic_multi_direct_record_load_extent(
            ctx->program, ctx->target, ctx->fn, ins, ctx->frame_plan,
            &source, &dest, &bytes) ||
        !minic_a64_emit_owned_slot(out, &view, false,
            MINIC_A64_REG_X, 10U, minic_lp64_frame_value_offset(frame_plan, source)) ||
        !minic_a64_emit_owned_address(out, &view, dest, 11U))
        return false;
    if (bytes > 64U) {
        if (fprintf(out, "  movz x12, #%zu\n", bytes & 65535U) < 0 ||
            (bytes > 65535U &&
             fprintf(out, "  movk x12, #%zu, lsl #16\n",
                     bytes >> 16U) < 0) ||
            fputs("1:\n"
                  "  ldrb w9, [x10], #1\n"
                  "  strb w9, [x11], #1\n"
                  "  subs x12, x12, #1\n"
                  "  b.ne 1b\n", out) == EOF)
            return false;
    } else {
        for (byte = 0U; byte < bytes; ++byte)
            if (fputs("  ldrb w9, [x10], #1\n"
                      "  strb w9, [x11], #1\n", out) == EOF)
                return false;
    }
    return true;
}

bool minic_aarch64_emit_parameter_object(FILE *out,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
            const MinicC0Program *program = ctx->program;
            const MinicTargetInfo *target = ctx->target;
            const MinicCoreFunction *fn = ctx->fn;
            const MinicA64FrameView view = {
                ctx->dynamic_frame, ctx->frame, 0U,
                ctx->saved_entry_sp_offset
            };
            const MinicLP64ArgumentLocation *location;
            size_t size, slots, object_offset, k, chunk;
            if (!minic_multi_direct_parameter_object_location(
                    fn, ins, ctx->incoming_plan, ctx->frame_plan,
                    &location, &object_offset))
                return false;
            /* An AAPCS64 empty C composite consumes no xN/stack argument.
             * The Core object retains its exact type, with no machine bytes. */
            if (location->kind == MINIC_LP64_ARGUMENT_EMPTY_RECORD) {
                const MinicCoreObjectId object_id =
                    ins->value.parameter_object.object_id;
                return location->size_bytes == 0U && location->chunks == 0U &&
                    location->gp_registers == 0U &&
                    location->fp_registers == 0U &&
                    location->stack_slots == 0U &&
                    minic_multi_empty_record(program, target,
                        fn->objects[object_id].type);
            }
            size = location->size_bytes;
            slots = location->kind == MINIC_LP64_ARGUMENT_RECORD
                ? location->chunks : 0U;
            if (location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
                /* Composite by-reference parameter: copy the caller-owned
                 * bytes to this callee's independently addressable object. */
                if ((location->gp_registers != 0U
                     ? fprintf(out, "  mov x10, x%zu\n", location->gp_first) < 0
                     : !minic_a64_emit_frame_load(out, &view,
                         (MinicA64FrameRef){MINIC_A64_FRAME_INCOMING,
                             location->stack_first * 8U},
                         (MinicA64FrameRegister){MINIC_A64_REG_X, 10U},
                         (MinicA64ScratchPolicy){0U, 0U})) ||
                    !minic_a64_emit_owned_address(out, &view,
                        object_offset, 11U))
                    return false;
                for (k = 0U; k < size; ++k) {
                    if (fputs("  ldrb w9, [x10], #1\n"
                              "  strb w9, [x11], #1\n", out) == EOF)
                        return false;
                }
                return true;
            }
            for (chunk = 0U; chunk < slots; ++chunk) {
                const bool in_reg = chunk < location->gp_registers;
                const size_t payload = size - chunk * 8U >= 8U
                    ? 8U : size - chunk * 8U;
                if (!minic_aarch64_emit_lp64_record_chunk_parameter(out, &view,
                        object_offset + chunk * 8U, payload,
                        in_reg, in_reg ? location->gp_first + chunk : 0U,
                        in_reg ? 0U : (location->stack_first + chunk) * 8U))
                    return false;
            }
            return true;
}

/* Return a 1..16 byte integral-class record through x0/x1. Pack only
 * meaningful payload bytes: never read past the last partial eightbyte.
 * The frame may exceed scaled LDR's immediate range and dynamic alloca
 * switches the stable object base from SP to x19. */
static bool aarch64_return_gp_record(
    FILE *out, size_t src, size_t bytes, const MinicA64FrameView *view) {
    size_t chunk, byte;
    if (out == NULL || bytes == 0U || bytes > 16U) return false;
    for (chunk = 0U; chunk < (bytes + 7U) / 8U; ++chunk) {
        const size_t count = bytes - chunk * 8U >= 8U ? 8U : bytes - chunk * 8U;
        if (fprintf(out, "  mov x%zu, xzr\n", chunk) < 0)
            return false;
        for (byte = 0U; byte < count; ++byte) {
            const size_t offset = src + chunk * 8U + byte;
            if (offset > UINT32_MAX ||
                !minic_a64_emit_owned_address(out, view, offset, 11U) ||
                fprintf(out, "  ldrb w9, [x11]\n"
                             "  orr x%zu, x%zu, x9, lsl #%zu\n",
                        chunk, chunk, byte * 8U) < 0)
                return false;
        }
    }
    return true;
}

/* AAPCS64 returns non-HFA records over 16 bytes into the caller-owned
 * address originally passed in x8. The function may have called another
 * routine (which freely clobbers x8), so load the preserved pointer from
 * our own frame. A single counted copy avoids emitting thousands of
 * machine instructions for a Linux-size record and copies exactly the
 * DataLayout size, including padding bytes. */
static bool aarch64_emit_indirect_record_return(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicLP64FramePlan *frame_plan,
    MinicCoreObjectId object_id, size_t pointer_offset,
    const MinicA64FrameView *view) {
    size_t src, bytes;
    if (!minic_aarch64_indirect_record(program, target, fn->return_type, &bytes) ||
        bytes <= 16U || bytes > 2048U ||
        !minic_lp64_frame_plan_object(frame_plan, object_id, &src) ||
        !minic_a64_emit_owned_address(out, view, src, 11U) ||
        !minic_a64_emit_owned_address(out, view, pointer_offset, 10U) ||
        fprintf(out, "  ldr x10, [x10]\n  movz x12, #%zu\n", bytes) < 0 ||
        fputs("1:\n"
              "  ldrb w9, [x11], #1\n"
              "  strb w9, [x10], #1\n"
              "  subs x12, x12, #1\n"
              "  b.ne 1b\n", out) == EOF)
        return false;
    return true;
}

bool minic_aarch64_emit_terminator(FILE *out,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreBlock *block, size_t block_id) {
        const MinicC0Program *program = ctx->program;
        const MinicTargetInfo *target = ctx->target;
        const MinicCoreFunction *fn = ctx->fn;
        const MinicLP64FramePlan *frame_plan = ctx->frame_plan;
        const size_t id = ctx->id, frame = ctx->frame;
        const size_t save_offset = ctx->save_offset;
        const size_t saved_entry_sp_offset = ctx->saved_entry_sp_offset;
        const bool has_direct_calls = ctx->has_direct_calls;
        const bool dynamic_frame = ctx->dynamic_frame;
        const bool has_indirect_return = ctx->has_indirect_return;
        const MinicA64FrameView view = {
            dynamic_frame, frame, 0U, saved_entry_sp_offset
        };
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (minic_type_is_record(fn->return_type)) {
                size_t src, bytes, slots, fp_elements, fp_width, fp;
                const MinicCoreObjectId object_id = block->terminator.return_object;
                if (object_id >= fn->object_count)
                    return false;
                if (minic_multi_empty_record(program, target, fn->return_type)) {
                    if (!minic_multi_empty_record(program, target,
                            fn->objects[object_id].type))
                        return false;
                } else if (!minic_lp64_frame_plan_object(
                        frame_plan, object_id, &src))
                    return false;
                else if (minic_multi_fp_return_record(program, target,
                       fn->return_type, &fp_elements, &fp_width)) {
                    for (fp = 0U; fp < fp_elements; ++fp) {
                        size_t off = src + fp * fp_width;
                        if (off > UINT32_MAX ||
                            !minic_a64_emit_owned_address(out, &view, off, 11U) ||
                            fprintf(out, "  ldr %c%zu, [x11]\n",
                                    fp_width == 4U ? 's' : 'd', fp) < 0)
                            return false;
                    }
                } else if (has_indirect_return) {
                    if (!aarch64_emit_indirect_record_return(
                            out, program, target, fn, frame_plan,
                            object_id, ctx->indirect_return_offset, &view))
                        return false;
                } else if (!minic_multi_gp_record(
                               program, target, fn->return_type,
                               &bytes, &slots) ||
                           !aarch64_return_gp_record(out, src, bytes,
                                                      &view))
                    return false;
            }
            if (minic_type_is_int128_integer(fn->return_type)) {
                size_t src;
                if (!minic_lp64_frame_plan_wide_slot(
                        frame_plan, block->terminator.return_value, &src) ||
                    !minic_a64_emit_owned_slot(out, &view, false,
                        MINIC_A64_REG_X, 0U, src) ||
                    !minic_a64_emit_owned_slot(out, &view, false,
                        MINIC_A64_REG_X, 1U, src + 8U))
                    return false;
            }
            if (minic_type_is_long_double(fn->return_type)) {
                size_t src;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, block->terminator.return_value, &src) ||
                    !minic_a64_emit_owned_slot(out, &view, false,
                        MINIC_A64_REG_Q, 0U, src))
                    return false;
            }
            if (!minic_type_is_void(fn->return_type) &&
                !minic_type_is_record(fn->return_type) &&
                !minic_type_is_long_double(fn->return_type) &&
                !minic_type_is_int128_integer(fn->return_type)) {
                const MinicA64FrameRegisterClass kind =
                    minic_type_is_double(fn->return_type) ? MINIC_A64_REG_D
                    : minic_type_is_float(fn->return_type) ? MINIC_A64_REG_S
                    : minic_lp64_register64_type(program, target,
                        fn->return_type) ? MINIC_A64_REG_X : MINIC_A64_REG_W;
                if (!minic_a64_emit_owned_slot(out, &view, false, kind, 0U,
                        minic_lp64_frame_value_offset(frame_plan, block->terminator.return_value)))
                    return false;
            }
            if (saved_entry_sp_offset != SIZE_MAX) {
                if (!minic_a64_emit_owned_slot(out, &view, false,
                        MINIC_A64_REG_X, 9U, saved_entry_sp_offset) ||
                    !minic_a64_emit_owned_slot(out, &view, false,
                        MINIC_A64_REG_X, 10U,
                        save_offset + (has_direct_calls ? 8U : 0U)) ||
                    (has_direct_calls &&
                     !minic_a64_emit_owned_slot(out, &view, false,
                        MINIC_A64_REG_X, 30U, save_offset)) ||
                    fputs("  mov sp, x9\n  mov x19, x10\n", out) == EOF)
                    return false;
            } else {
                if (has_direct_calls &&
                    !minic_a64_emit_owned_slot(out, &view, false,
                        MINIC_A64_REG_X, 30U, save_offset))
                    return false;
                if (dynamic_frame &&
                    (fputs("  mov sp, x19\n", out) == EOF ||
                     !minic_a64_emit_owned_slot(out, &view, false,
                        MINIC_A64_REG_X, 19U,
                        save_offset + (has_direct_calls ? 8U : 0U))))
                    return false;
                if (frame != 0U &&
                    (fprintf(out, "  movz x12, #%zu\n", frame & 65535U) < 0 ||
                     (frame > 65535U &&
                      fprintf(out, "  movk x12, #%zu, lsl #16\n",
                              frame >> 16U) < 0) ||
                     fputs("  add sp, sp, x12\n", out) == EOF))
                    return false;
            }
            if (fputs("  ldp x29, x30, [sp], #16\n  ret\n", out) == EOF)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            if (!minic_a64_emit_owned_slot(out, &view, false,
                    MINIC_A64_REG_X, 9U,
                    minic_lp64_frame_value_offset(frame_plan, block->terminator.indirect_target)) ||
                fputs("  br x9\n", out) == EOF)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Preserve Core CFG semantics: trap instead of falling through. */
            if (fputs("  brk #0\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_arm_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
            const MinicCoreValueId cond = block->terminator.conditional.condition;
            const bool wide = minic_lp64_register64_type(program, target, fn->values[cond].type);
            /* Large computed-goto VM CFGs exceed cbnz's branch reach. */
            if (fn->block_count > 256U) {
                if (!minic_a64_emit_owned_slot(out, &view, false,
                        wide ? MINIC_A64_REG_X : MINIC_A64_REG_W,
                        9U, minic_lp64_frame_value_offset(frame_plan, cond)) ||
                    fprintf(out, wide
                            ? "  cbz x9, .Lmini_arm_f%zu_false_%zu\n"
                            : "  cbz w9, .Lmini_arm_f%zu_false_%zu\n",
                            id, block_id) < 0 ||
                    fprintf(out, "  b .Lmini_arm_f%zu_b%u\n"
                                 ".Lmini_arm_f%zu_false_%zu:\n"
                                 "  b .Lmini_arm_f%zu_b%u\n",
                            id, (unsigned int)block->terminator.conditional.when_true,
                            id, block_id,
                            id, (unsigned int)block->terminator.conditional.when_false) < 0)
                    return false;
                break;
            }
            if (!minic_a64_emit_owned_slot(out, &view, false,
                    wide ? MINIC_A64_REG_X : MINIC_A64_REG_W,
                    9U, minic_lp64_frame_value_offset(frame_plan, cond)) ||
                fprintf(out, wide
                            ? "  cbnz x9, .Lmini_arm_f%zu_b%u\n"
                              "  b .Lmini_arm_f%zu_b%u\n"
                            : "  cbnz w9, .Lmini_arm_f%zu_b%u\n"
                              "  b .Lmini_arm_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.conditional.when_true,
                        id, (unsigned int)block->terminator.conditional.when_false) < 0)
                return false;
            break;
        }
        default: return false;
        }
    return true;
}

/* All Core blocks and their machine instruction cases are target-owned. */

/* All Core GP-producing instruction families share this final slot
 * transport. The F/D arithmetic and ABI remain native and may opt into
 * binary64-width writeback without affecting integer ALU. */
static bool aarch64_emit_gp_result(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreInstruction *ins, size_t offset, bool include_double,
    const MinicA64FrameView *view) {
    const bool wide = minic_lp64_register64_type(program, target, ins->type) ||
                      (include_double && minic_type_is_double(ins->type));
    return minic_a64_emit_owned_slot(out, view, true,
        wide ? MINIC_A64_REG_X : MINIC_A64_REG_W, 9U, offset);
}

/* Shared Common dispatcher owns Core order, machine family routing and
 * writeback policy; AAPCS64 still owns every native opcode/ABI transfer. */
static bool aarch64_machine_emit_family(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, MinicCoreInstructionId ins_id,
    size_t block_id, const MinicDirectMachineInstructionShape *shape,
    bool *needs_gp_store) {
    const MinicC0Program *program = ctx->program;
    const MinicTargetInfo *target = ctx->target;
    const MinicCoreFunction *fn = ctx->fn;
    const MinicA64FrameView frame_view = {
        ctx->dynamic_frame, ctx->frame, 0U, ctx->saved_entry_sp_offset
    };
    if (shape == NULL || needs_gp_store == NULL) return false;
    *needs_gp_store = false;
    switch (shape->family) {
    case MINIC_DIRECT_MACHINE_RECORD_LOAD:
        return minic_aarch64_emit_record_load(out, ctx, ins);
    case MINIC_DIRECT_MACHINE_RECORD_COPY:
        return minic_aarch64_emit_record_copy(out, ctx, ins);
    case MINIC_DIRECT_MACHINE_PARAMETER_OBJECT:
        return minic_aarch64_emit_parameter_object(out, ctx, ins);
    case MINIC_DIRECT_MACHINE_MEMORY:
        return minic_aarch64_emit_memory_instruction(
            out, program, target, fn, ins, ctx->frame_plan,
            &frame_view, needs_gp_store);
    case MINIC_DIRECT_MACHINE_FP:
        return minic_aarch64_emit_fp_instruction(
            out, program, target, fn, ins, shape->result_offset,
            ctx->frame_plan, &frame_view, needs_gp_store);
    case MINIC_DIRECT_MACHINE_INTEGER:
        return minic_aarch64_emit_integer_instruction(
            out, program, target, fn, ins, ctx->frame_plan,
            &frame_view, needs_gp_store);
    case MINIC_DIRECT_MACHINE_ABI:
        return minic_aarch64_emit_abi_instruction(
            out, ctx, ins, shape->result_offset, needs_gp_store);
    case MINIC_DIRECT_MACHINE_RESIDUAL:
        return minic_aarch64_emit_residual_instruction(
            out, ctx, ins_id, block_id, shape->result_offset, needs_gp_store);
    case MINIC_DIRECT_MACHINE_COMPILER_BARRIER:
    default:
        return false; /* The common visitor consumes compiler barriers. */
    }
}

static bool aarch64_machine_store_gp(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, size_t offset, bool include_double) {
    const MinicA64FrameView view = {
        ctx->dynamic_frame, ctx->frame, 0U, ctx->saved_entry_sp_offset
    };
    return aarch64_emit_gp_result(
        out, ctx->program, ctx->target, ins, offset, include_double, &view);
}

static bool aarch64_machine_emit_terminator(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreBlock *block, size_t block_id) {
    return minic_aarch64_emit_terminator(out, ctx, block, block_id);
}

bool minic_aarch64_emit_core_blocks(
    FILE *out, const MinicDirectLP64BlockContext *ctx) {
    static const MinicDirectMachineDispatch dispatch = {
        "aarch64", "mini_arm", true, false,
        aarch64_machine_emit_family,
        aarch64_machine_store_gp,
        aarch64_machine_emit_terminator,
        NULL,
        NULL
    };
    return minic_multi_direct_machine_dispatch(out, ctx, &dispatch);
}
