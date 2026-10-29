#include "target/loongarch64/block_emit.h"
#include "target/loongarch64/scalar_normalize.h"
#include "target/loongarch64/residual_emit.h"
#include "target/loongarch64/memory_emit.h"
#include "target/loongarch64/call_emit.h"
#include "target/loongarch64/integer_emit.h"
#include "target/loongarch64/fp_emit.h"
#include "target/common/core_block_walk.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_machine_dispatch.h"
#include "target/common/direct_residual_plan.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"
#include "target/loongarch64/frame_emit.h"
#include "target/data_layout.h"
#include "target/common/direct_program_emit.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

/* The instruction-owned aggregate ingress and CFG terminator cases are
 * independent of the function-level preflight and frame lifetime. Machine
 * register syntax remains strictly owned by this ISA. */

bool minic_loongarch64_emit_lp64_record_chunk_parameter(FILE *out, size_t offset,
                                              size_t bytes, bool in_reg,
                                              size_t gp, size_t incoming_offset,
                                              bool dynamic_frame,
                                              size_t saved_entry_sp_offset) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U) ||
        fprintf(out, "  li.d $t2, %zu\n"
                     "  add.d $t2, %s, $t2\n", offset,
                dynamic_frame ? "$s0" : "$sp") < 0 ||
        (in_reg
         ? fprintf(out, "  or $t0, $a%zu, $zero\n", gp) < 0
         : saved_entry_sp_offset != SIZE_MAX
             ? !minic_loongarch64_emit_incoming_stack_mem(
                   out, "ld.d", "$t0", incoming_offset,
                   saved_entry_sp_offset)
             : !minic_loongarch64_emit_frame_mem(out, "ld.d", "$t0",
                           incoming_offset, dynamic_frame)))
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  st.b $t0, $t2, %zu\n", b) < 0 ||
            (b + 1U < bytes && fputs("  srli.d $t0, $t0, 8\n", out) == EOF))
            return false;
    return true;
}

bool minic_loongarch64_emit_record_copy(FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreInstruction *ins, bool dynamic_frame) {
const MinicCoreValueId dst = ins->value.record_copy.destination_address;
            const MinicCoreValueId src = ins->value.record_copy.source_address;
            size_t bytes;
            if (!minic_multi_direct_record_copy_extent(program, target, fn, ins, &bytes))
                return false;
            if (!emit_sp_mem(out, "ld.d", "$t1", (size_t)dst * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t2", (size_t)src * 8U))
                return false;
            if (bytes > 64U) {
                if (fprintf(out, "  li.d $t3, %zu\n", bytes) < 0 ||
                    fputs("1:\n"
                          "  ld.bu $t0, $t2, 0\n"
                          "  st.b $t0, $t1, 0\n"
                          "  addi.d $t2, $t2, 1\n"
                          "  addi.d $t1, $t1, 1\n"
                          "  addi.d $t3, $t3, -1\n"
                          "  bnez $t3, 1b\n", out) == EOF)
                    return false;
            } else {
                for (size_t byte = 0U; byte < bytes; ++byte)
                    if (fputs("  ld.bu $t0, $t2, 0\n"
                              "  st.b $t0, $t1, 0\n"
                              "  addi.d $t2, $t2, 1\n"
                              "  addi.d $t1, $t1, 1\n", out) == EOF)
                        return false;
            }
            return true;
}

/* Native LoongArch64 byte transport for the Common typed record
 * snapshot; +Q/volatile reads are never collapsed to a scalar load. */
static bool minic_loongarch64_emit_record_load(FILE *out,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins) {
    MinicCoreValueId source;
    size_t dest, bytes, byte;
    if (!minic_multi_direct_record_load_extent(
            ctx->program, ctx->target, ctx->fn, ins, ctx->frame_plan,
            &source, &dest, &bytes) ||
        !minic_loongarch64_emit_frame_mem(out, "ld.d", "$t2",
            (size_t)source * 8U, ctx->dynamic_frame) ||
        fprintf(out, "  li.d $t1, %zu\n  add.d $t1, %s, $t1\n",
            dest, ctx->dynamic_frame ? "$s0" : "$sp") < 0)
        return false;
    if (bytes > 64U) {
        if (fprintf(out, "  li.d $t3, %zu\n", bytes) < 0 ||
            fputs("1:\n"
                  "  ld.bu $t0, $t2, 0\n"
                  "  st.b $t0, $t1, 0\n"
                  "  addi.d $t2, $t2, 1\n"
                  "  addi.d $t1, $t1, 1\n"
                  "  addi.d $t3, $t3, -1\n"
                  "  bnez $t3, 1b\n", out) == EOF)
            return false;
    } else {
        for (byte = 0U; byte < bytes; ++byte)
            if (fputs("  ld.bu $t0, $t2, 0\n"
                      "  st.b $t0, $t1, 0\n"
                      "  addi.d $t2, $t2, 1\n"
                      "  addi.d $t1, $t1, 1\n", out) == EOF)
                return false;
    }
    return true;
}

bool minic_loongarch64_emit_parameter_object(FILE *out,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming_plan,
    const MinicLP64FramePlan *frame_plan, size_t frame, bool dynamic_frame,
    size_t saved_entry_sp_offset) {
const MinicLP64ArgumentLocation *location;
            size_t size, slots, object_offset, k, chunk;
            if (!minic_multi_direct_parameter_object_location(
                    fn, ins, incoming_plan, frame_plan, &location, &object_offset))
                return false;
            if (location->kind == MINIC_LP64_ARGUMENT_EMPTY_RECORD) {
                /* No incoming machine bytes for GNU zero-size aggregates. */
                return location->size_bytes == 0U && location->chunks == 0U &&
                       location->gp_registers == 0U &&
                       location->fp_registers == 0U &&
                       location->stack_slots == 0U;
            }
            size = location->size_bytes;
            slots = location->kind == MINIC_LP64_ARGUMENT_RECORD
                ? location->chunks : 0U;
            if (location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
                if ((location->gp_registers != 0U
                     ? fprintf(out, "  move $t0, $a%zu\n", location->gp_first) < 0
                     : saved_entry_sp_offset != SIZE_MAX
                         ? !minic_loongarch64_emit_incoming_stack_mem(
                               out, "ld.d", "$t0",
                               location->stack_first * 8U,
                               saved_entry_sp_offset)
                         : !emit_sp_mem(out, "ld.d", "$t0",
                                    frame + location->stack_first * 8U)) ||
                    fprintf(out, "  li.d $t1, %zu\n"
                                 "  add.d $t1, %s, $t1\n",
                            object_offset, dynamic_frame ? "$s0" : "$sp") < 0)
                    return false;
                for (k = 0U; k < size; ++k) {
                    if (fputs("  ld.bu $t2, $t0, 0\n"
                              "  st.b $t2, $t1, 0\n"
                              "  addi.d $t0, $t0, 1\n"
                              "  addi.d $t1, $t1, 1\n", out) == EOF)
                        return false;
                }
                return true;
            }
            for (chunk = 0U; chunk < slots; ++chunk) {
                const bool in_reg = chunk < location->gp_registers;
                const size_t stack_slot = in_reg ? 0U :
                    location->stack_first + chunk - location->gp_registers;
                const size_t payload = size - chunk * 8U >= 8U
                    ? 8U : size - chunk * 8U;
                if (!minic_loongarch64_emit_lp64_record_chunk_parameter(out,
                        object_offset + chunk * 8U, payload,
                        in_reg, in_reg ? location->gp_first + chunk : 0U,
                        saved_entry_sp_offset != SIZE_MAX
                            ? stack_slot * 8U : frame + stack_slot * 8U,
                        dynamic_frame, saved_entry_sp_offset))
                    return false;
            }
            return true;
}

/* LoongArch LP64D integer-class record returns use a0/a1. Assemble each
 * payload byte to avoid unsafe partial-eightbyte reads; do not conflate
 * GNU HFA or indirect aggregate return ABI with this class. */
static bool loongarch64_return_gp_record(
    FILE *out, size_t src, size_t bytes, bool dynamic_frame) {
    size_t chunk, byte;
    if (out == NULL || bytes == 0U || bytes > 16U) return false;
    for (chunk = 0U; chunk < (bytes + 7U) / 8U; ++chunk) {
        const size_t count = bytes - chunk * 8U >= 8U ? 8U : bytes - chunk * 8U;
        if (fputs("  move $t1, $zero\n", out) == EOF) return false;
        for (byte = 0U; byte < count; ++byte) {
            if (!minic_loongarch64_emit_frame_mem(
                    out, "ld.bu", "$t0", src + chunk * 8U + byte,
                    dynamic_frame) ||
                (byte != 0U && fprintf(
                    out, "  slli.d $t0, $t0, %zu\n", byte * 8U) < 0) ||
                fputs("  or $t1, $t1, $t0\n", out) == EOF)
                return false;
        }
        if (fprintf(out, "  or $a%zu, $t1, $zero\n", chunk) < 0)
            return false;
    }
    return true;
}

/* LoongArch ELF psABI LP64D: objects larger than 2*GRLEN return through
 * the caller-allocated buffer in the HIDDEN FIRST GAR ($a0); the callee
 * must also return that same pointer in $a0. This is NOT AAPCS64 x8.
 * The saved incoming pointer survives nested calls and dynamic alloca.
 * Emit a counted, exact-byte copy instead of O(size) textual instructions. */
static bool loongarch64_return_indirect_record(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicLP64FramePlan *frame_plan,
    MinicCoreObjectId object_id, size_t return_slot, bool dynamic_frame) {
    size_t src, bytes;
    if (out == NULL ||
        !minic_multi_indirect_record(program, target, fn->return_type,
                                     &bytes) ||
        bytes <= 16U || bytes > 2048U ||
        !minic_lp64_frame_plan_object(frame_plan, object_id, &src) ||
        fprintf(out, "  li.d $t1, %zu\n"
                     "  add.d $t1, %s, $t1\n",
                src, dynamic_frame ? "$s0" : "$sp") < 0 ||
        !minic_loongarch64_emit_frame_mem(
            out, "ld.d", "$a0", return_slot, dynamic_frame) ||
        fprintf(out, "  move $t2, $a0\n"
                     "  li.d $t0, %zu\n"
                     "1:\n"
                     "  ld.bu $t4, $t1, 0\n"
                     "  st.b $t4, $t2, 0\n"
                     "  addi.d $t1, $t1, 1\n"
                     "  addi.d $t2, $t2, 1\n"
                     "  addi.d $t0, $t0, -1\n"
                     "  bnez $t0, 1b\n", bytes) < 0)
        return false;
    return true;
}

bool minic_loongarch64_emit_terminator(FILE *out,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreBlock *block,
    size_t id, size_t block_id, const MinicLP64FramePlan *frame_plan,
    size_t frame, size_t save_offset, bool has_direct_calls,
    bool dynamic_frame, bool has_indirect_return,
    size_t indirect_return_offset, size_t frame_alignment,
    size_t saved_entry_sp_offset) {
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (minic_type_is_record(fn->return_type)) {
                size_t src, bytes, slots, fp_elements, fp_width, fp;
                const MinicCoreObjectId object_id = block->terminator.return_object;
                if (object_id >= fn->object_count ||
                    !minic_lp64_frame_plan_object(frame_plan, object_id, &src))
                    return false;
                if (minic_multi_empty_record(program, target,
                                             fn->return_type)) {
                    /* Complete zero-byte record returns have no payload. */
                } else if (minic_multi_fp_return_record(program, target,
                        fn->return_type, &fp_elements, &fp_width)) {
                    for (fp = 0U; fp < fp_elements; ++fp)
                        if (!minic_loongarch64_emit_frame_mem(
                                out, fp_width == 4U ? "fld.s" : "fld.d",
                                fp == 0U ? "$fa0" : "$fa1",
                                src + fp * fp_width, dynamic_frame))
                            return false;
                } else if (has_indirect_return) {
                    if (!loongarch64_return_indirect_record(
                            out, program, target, fn, frame_plan, object_id,
                            indirect_return_offset, dynamic_frame))
                        return false;
                } else if (!minic_multi_gp_record(
                               program, target, fn->return_type,
                               &bytes, &slots) ||
                           !loongarch64_return_gp_record(out, src, bytes,
                                                         dynamic_frame))
                    return false;
            }
            if (minic_type_is_long_double(fn->return_type) ||
                minic_type_is_int128_integer(fn->return_type)) {
                size_t src;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, block->terminator.return_value, &src) ||
                    !emit_sp_mem(out, "ld.d", "$a0", src) ||
                    !emit_sp_mem(out, "ld.d", "$a1", src + 8U))
                    return false;
            }
            if ((!minic_type_is_void(fn->return_type) &&
                 !minic_type_is_record(fn->return_type) &&
                 !minic_type_is_long_double(fn->return_type) &&
                 !minic_type_is_int128_integer(fn->return_type) &&
                 !((minic_type_is_double(fn->return_type) ||
                  minic_type_is_float(fn->return_type))
                       ? emit_sp_mem(out,
                                     minic_type_is_float(fn->return_type)
                                         ? "fld.s" : "fld.d", "$fa0",
                                     (size_t)block->terminator.return_value * 8U)
                       : emit_sp_mem(out, minic_lp64_register64_type(program, target, fn->return_type)
                                          ? "ld.d" : "ld.w", "$a0",
                                     (size_t)block->terminator.return_value * 8U))) ||
                (has_direct_calls && !emit_sp_mem(out, "ld.d", "$ra", save_offset)) ||
                (frame_alignment > 16U &&
                 (saved_entry_sp_offset == SIZE_MAX ||
                  !minic_loongarch64_emit_frame_mem(out, "ld.d", "$t0",
                      saved_entry_sp_offset, true) ||
                  !minic_loongarch64_emit_frame_mem(out, "ld.d", "$t1",
                      save_offset + (has_direct_calls ? 8U : 0U), true) ||
                  fputs("  move $sp, $t0\n  move $s0, $t1\n", out) == EOF)) ||
                (frame_alignment <= 16U && dynamic_frame &&
                 (fputs("  move $sp, $s0\n", out) == EOF ||
                  !minic_loongarch64_emit_frame_mem(out, "ld.d", "$s0",
                      save_offset + (has_direct_calls ? 8U : 0U), false))) ||
                (frame_alignment <= 16U &&
                 !minic_loongarch64_emit_sp_adjust(out, frame, true)) ||
                fputs("  ret\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            if (!emit_sp_mem(out, "ld.d", "$t0",
                             (size_t)block->terminator.indirect_target * 8U) ||
                fputs("  jirl $zero, $t0, 0\n", out) == EOF)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Preserve Core CFG semantics: trap instead of falling through. */
            if (fputs("  break 0\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
            const MinicCoreValueId cond = block->terminator.conditional.condition;
            const bool wide = minic_lp64_register64_type(program, target, fn->values[cond].type);
            /* The short LoongArch conditional jump must remain nearby. */
            if (fn->block_count > 256U) {
                if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                                 (size_t)cond * 8U) ||
                    fprintf(out,
                            "  beqz $t0, .Lmini_loong_f%zu_false_%zu\n"
                            "  b .Lmini_loong_f%zu_b%u\n"
                            ".Lmini_loong_f%zu_false_%zu:\n"
                            "  b .Lmini_loong_f%zu_b%u\n",
                            id, block_id,
                            id, (unsigned int)block->terminator.conditional.when_true,
                            id, block_id,
                            id, (unsigned int)block->terminator.conditional.when_false) < 0)
                    return false;
                break;
            }
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)cond * 8U) ||
                fprintf(out,
                        "  bnez $t0, .Lmini_loong_f%zu_b%u\n"
                        "  b .Lmini_loong_f%zu_b%u\n",
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

/* The fixed-frame register is target-specific; one machine writeback
 * owner prevents memory/FP/ALU/ABI lowering from diverging on slot widths. */
static bool loongarch64_emit_gp_result(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreInstruction *ins, size_t offset, bool include_double,
    bool dynamic_frame) {
    const bool wide = minic_lp64_register64_type(program, target, ins->type) ||
                      (include_double && minic_type_is_double(ins->type));
    return minic_loongarch64_emit_frame_mem(
        out, wide ? "st.d" : "st.w", "$t0", offset, dynamic_frame);
}

/* Common dispatch owns Core block ordering and semantic family routing,
 * but this adapter owns LP64D registers, stack transport and native opcodes. */
static bool loongarch64_machine_emit_family(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, MinicCoreInstructionId ins_id,
    size_t block_id, const MinicDirectMachineInstructionShape *shape,
    bool *needs_gp_store) {
    const MinicC0Program *program = ctx->program;
    const MinicTargetInfo *target = ctx->target;
    const MinicCoreFunction *fn = ctx->fn;
    if (shape == NULL || needs_gp_store == NULL) return false;
    *needs_gp_store = false;
    switch (shape->family) {
    case MINIC_DIRECT_MACHINE_RECORD_LOAD:
        return minic_loongarch64_emit_record_load(out, ctx, ins);
    case MINIC_DIRECT_MACHINE_RECORD_COPY:
        return minic_loongarch64_emit_record_copy(
            out, program, target, fn, ins, ctx->dynamic_frame);
    case MINIC_DIRECT_MACHINE_PARAMETER_OBJECT:
        return minic_loongarch64_emit_parameter_object(
            out, fn, ins, ctx->incoming_plan, ctx->frame_plan,
            ctx->frame, ctx->dynamic_frame,
            ctx->saved_entry_sp_offset);
    case MINIC_DIRECT_MACHINE_MEMORY:
        return minic_loongarch64_emit_memory_instruction(
            out, program, target, fn, ins, ctx->frame_plan,
            ctx->dynamic_frame, needs_gp_store);
    case MINIC_DIRECT_MACHINE_FP:
        return minic_loongarch64_emit_fp_instruction(
            out, program, target, fn, ins, shape->result_offset,
            ctx->id, ctx->dynamic_frame, ctx->frame_plan, needs_gp_store);
    case MINIC_DIRECT_MACHINE_INTEGER:
        return minic_loongarch64_emit_integer_instruction(
            out, program, target, fn, ins, ctx->frame_plan,
            ctx->dynamic_frame, needs_gp_store);
    case MINIC_DIRECT_MACHINE_ABI:
        return minic_loongarch64_emit_abi_instruction(
            out, ctx, ins, shape->result_offset, needs_gp_store);
    case MINIC_DIRECT_MACHINE_RESIDUAL:
        return minic_loongarch64_emit_residual_instruction(
            out, ctx, ins_id, block_id, shape->result_offset, needs_gp_store);
    case MINIC_DIRECT_MACHINE_COMPILER_BARRIER:
    default:
        return false; /* Common visitor handles compiler barriers. */
    }
}

static bool loongarch64_machine_store_gp(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins, size_t offset, bool include_double) {
    return loongarch64_emit_gp_result(
        out, ctx->program, ctx->target, ins, offset, include_double,
        ctx->dynamic_frame);
}

static bool loongarch64_machine_emit_terminator(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreBlock *block, size_t block_id) {
    return minic_loongarch64_emit_terminator(
        out, ctx->program, ctx->target, ctx->fn,
        block, ctx->id, block_id, ctx->frame_plan, ctx->frame,
        ctx->save_offset, ctx->has_direct_calls, ctx->dynamic_frame,
        ctx->has_indirect_return, ctx->indirect_return_offset,
        ctx->frame_alignment, ctx->saved_entry_sp_offset);
}

bool minic_loongarch64_emit_core_blocks(
    FILE *out, const MinicDirectLP64BlockContext *ctx) {
    static const MinicDirectMachineDispatch dispatch = {
        "loongarch64", "mini_loong", false,
        loongarch64_machine_emit_family,
        loongarch64_machine_store_gp,
        loongarch64_machine_emit_terminator
    };
    return minic_multi_direct_machine_dispatch(out, ctx, &dispatch);
}

#undef emit_sp_mem
