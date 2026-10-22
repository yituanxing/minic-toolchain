#include "target/loongarch64/block_emit.h"
#include "target/loongarch64/residual_emit.h"
#include "target/loongarch64/memory_emit.h"
#include "target/loongarch64/call_emit.h"
#include "target/loongarch64/integer_emit.h"
#include "target/loongarch64/fp_emit.h"
#include "target/common/direct_instruction_semantics.h"
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
                                              bool dynamic_frame) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U) ||
        fprintf(out, "  li.d $t2, %zu\n"
                     "  add.d $t2, %s, $t2\n", offset,
                dynamic_frame ? "$s0" : "$sp") < 0 ||
        (in_reg
         ? fprintf(out, "  or $t0, $a%zu, $zero\n", gp) < 0
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
            for (size_t byte = 0U; byte < bytes; ++byte)
                if (fputs("  ld.bu $t0, $t2, 0\n"
                          "  st.b $t0, $t1, 0\n"
                          "  addi.d $t2, $t2, 1\n"
                          "  addi.d $t1, $t1, 1\n", out) == EOF)
                    return false;
            return true;
}

bool minic_loongarch64_emit_parameter_object(FILE *out,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming_plan,
    const MinicLP64FramePlan *frame_plan, size_t frame, bool dynamic_frame) {
const MinicLP64ArgumentLocation *location;
            size_t size, slots, object_offset, k, chunk;
            if (!minic_multi_direct_parameter_object_location(
                    fn, ins, incoming_plan, frame_plan, &location, &object_offset))
                return false;
            size = location->size_bytes;
            slots = location->kind == MINIC_LP64_ARGUMENT_RECORD
                ? location->chunks : 0U;
            if (location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
                if ((location->gp_registers != 0U
                     ? fprintf(out, "  move $t0, $a%zu\n", location->gp_first) < 0
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
                        frame + stack_slot * 8U,
                        dynamic_frame))
                    return false;
            }
            return true;
}

bool minic_loongarch64_emit_terminator(FILE *out,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreBlock *block,
    size_t id, size_t block_id, const MinicLP64FramePlan *frame_plan,
    size_t frame, size_t save_offset, bool has_direct_calls,
    bool dynamic_frame) {
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (minic_type_is_long_double(fn->return_type)) {
                size_t src;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, block->terminator.return_value, &src) ||
                    !emit_sp_mem(out, "ld.d", "$a0", src) ||
                    !emit_sp_mem(out, "ld.d", "$a1", src + 8U))
                    return false;
            }
            if ((!minic_type_is_void(fn->return_type) &&
                 !minic_type_is_long_double(fn->return_type) &&
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
                (dynamic_frame &&
                 (fputs("  move $sp, $s0\n", out) == EOF ||
                  !minic_loongarch64_emit_frame_mem(out, "ld.d", "$s0",
                                  save_offset + (has_direct_calls ? 8U : 0U),
                                  false))) ||
                (!minic_loongarch64_emit_sp_adjust(out, frame, true)) ||
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

bool minic_loongarch64_emit_core_blocks(FILE *out, const MinicDirectLP64BlockContext *ctx) {
    const MinicC0Program *program;
    const MinicTargetInfo *target;
    const MinicCoreFunction *fn;
    const MinicLP64FramePlan *frame_plan;
    const MinicLP64ArgumentPlan *incoming_plan;
    const char *name, *reject_stage;
    const MinicCoreBlock *block;
    size_t id, frame, save_offset, i;
    size_t reject_block = SIZE_MAX, reject_ins = SIZE_MAX;
    bool has_direct_calls, dynamic_frame;
    if (out == NULL || ctx == NULL || ctx->program == NULL ||
        ctx->target == NULL || ctx->fn == NULL || ctx->frame_plan == NULL ||
        ctx->incoming_plan == NULL || ctx->name == NULL) return false;
    program = ctx->program;
    target = ctx->target;
    fn = ctx->fn;
    frame_plan = ctx->frame_plan;
    incoming_plan = ctx->incoming_plan;
    name = ctx->name;
    id = ctx->id;
    frame = ctx->frame;
    save_offset = ctx->save_offset;
    has_direct_calls = ctx->has_direct_calls;
    dynamic_frame = ctx->dynamic_frame;
    reject_stage = "machine_emit";
    for (size_t block_id = 0U; block_id < fn->block_count; ++block_id) {
        reject_block = block_id;
        reject_ins = SIZE_MAX;
        reject_stage = "machine_emit";
        block = &fn->blocks[block_id];
        if (fprintf(out, ".Lmini_loong_f%zu_b%zu:\n", id, block_id) < 0 ||
            (block->source_label_id != SIZE_MAX &&
             !fn->suppress_source_label_aliases &&
             fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0))
            return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        for (i = 0U; i < block->instruction_count; ++i) {
        MinicCoreInstructionId ins_id = block->instructions[i];
        const MinicCoreInstruction *ins;
        size_t offset;
        MinicDirectMachineInstructionShape shape;
        if (ins_id >= fn->instruction_count) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        ins = &fn->instructions[ins_id];
        reject_ins = (size_t)ins_id;
        if (!minic_multi_direct_machine_instruction_shape(
                fn, ins, false, &shape))
            return minic_multi_direct_machine_reject(
                fn, "loongarch64", name, reject_stage,
                reject_block, reject_ins, __LINE__);
        offset = shape.result_offset;
        /* A Core compiler barrier is an effect-only ordering constraint
         * (GNU empty asm with a memory clobber), not a hardware fence and
         * never an SSA value. Like the existing x86/RV64 backends, preserve
         * source instruction order and emit no machine instruction. The
         * previous result-slot gate rejected this valid Core kind in the
         * original BusyBox test/gzip/netstat/top applets on both ISAs. */
        if (shape.family == MINIC_DIRECT_MACHINE_COMPILER_BARRIER)
            continue;
        /* M23: copy the *exact* target-sized aggregate bytes between Core
         * addresses, preserving padding and nested hooks (no re-layout).
         * An effect-only record copy has no SSA result slot. */
        if (shape.family == MINIC_DIRECT_MACHINE_RECORD_COPY) {
            if (!minic_loongarch64_emit_record_copy(out, program, target, fn, ins, dynamic_frame))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        if (shape.family == MINIC_DIRECT_MACHINE_PARAMETER_OBJECT) {
            if (!minic_loongarch64_emit_parameter_object(out, fn, ins,
                    incoming_plan, frame_plan, frame, dynamic_frame))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        /* Store both 64-bit limbs, with no hidden 128-bit truncation. */
        /* Memory effects precede the ordinary SSA gate: stores are effect
         * only, loads either produce a scalar GP value or a wide slot. */
        if (shape.family == MINIC_DIRECT_MACHINE_MEMORY) {
            bool needs_gp_store = false;
            if (!minic_loongarch64_emit_memory_instruction(
                    out, program, target, fn, ins, frame_plan,
                    dynamic_frame, &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage, reject_block, reject_ins, __LINE__);
            if (needs_gp_store && !loongarch64_emit_gp_result(
                    out, program, target, ins, offset, true, dynamic_frame))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            continue;
        }
        /* The Core result slot and family have already been admitted by
         * minic_multi_direct_machine_instruction_shape(). */
        /* The native FP instruction family is a single target-owned
         * emission unit; Core semantics are shared, machine opcodes are not.
         * GP-producing comparisons/conversions reuse normal writeback. */
        if (shape.family == MINIC_DIRECT_MACHINE_FP) {
            bool needs_gp_store = false;
            if (!minic_loongarch64_emit_fp_instruction(
                    out, program, target, fn, ins, offset,
                    id, dynamic_frame, frame_plan,
                    emit_integer_normalize,
                    &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (needs_gp_store && !loongarch64_emit_gp_result(
                    out, program, target, ins, offset, true, dynamic_frame))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            continue;
        }
        /* Integer and pointer ALU is a complete native instruction family.
         * Shared Core widths/shapes; ISA-dependent flags and bit arithmetic. */
        if (shape.family == MINIC_DIRECT_MACHINE_INTEGER) {
            bool needs_gp_store = false;
            if (!minic_loongarch64_emit_integer_instruction(
                    out, program, target, fn, ins, frame_plan,
                    dynamic_frame, emit_integer_normalize, &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (needs_gp_store && !loongarch64_emit_gp_result(
                    out, program, target, ins, offset, false, dynamic_frame))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            continue;
        }
        /* Calls and callee ingress form a single ABI transfer family.
         * The native emitter consumes the common argument location plan. */
        if (shape.family == MINIC_DIRECT_MACHINE_ABI) {
            bool needs_gp_store = false;
            if (!minic_loongarch64_emit_abi_instruction(
                    out, ctx, ins, offset, emit_integer_normalize,
                    &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (needs_gp_store && !loongarch64_emit_gp_result(
                    out, program, target, ins, offset, true, dynamic_frame))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            continue;
        }
        {
            bool needs_writeback = false;
            if (!minic_loongarch64_emit_residual_instruction(out, ctx, ins_id, block_id,
                    offset, &needs_writeback))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            if (!needs_writeback)
                continue;
        }
        if (!loongarch64_emit_gp_result(out, program, target, ins, offset, true, dynamic_frame))
            return minic_multi_direct_machine_reject(
                fn, "loongarch64", name, reject_stage,
                reject_block, reject_ins, __LINE__);
    }
        reject_stage = "terminator_emit";
        reject_ins = SIZE_MAX;
        if (!minic_loongarch64_emit_terminator(out, program, target, fn, block,
                id, block_id, frame_plan, frame, save_offset,
                has_direct_calls, dynamic_frame))
            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                reject_stage, reject_block, reject_ins, __LINE__);
    }
    return true;
}

#undef emit_sp_mem
