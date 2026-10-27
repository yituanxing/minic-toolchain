#ifndef MINIC_TARGET_COMMON_DIRECT_MACHINE_DISPATCH_H
#define MINIC_TARGET_COMMON_DIRECT_MACHINE_DISPATCH_H

#include "target/common/core_block_walk.h"
#include "target/common/direct_block_context.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_program_emit.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* One Common dispatch owner for the two direct-LP64 machine emitters.
 * Core order, label ownership and GP-result policy live here; *all*
 * machine operations and psABI placement remain in native callbacks.
 * This is a control-plane adapter, NOT a universal ISA instruction IR. */
typedef bool (*MinicDirectMachineEmitFamily)(
    FILE *, const MinicDirectLP64BlockContext *, const MinicCoreInstruction *,
    MinicCoreInstructionId, size_t block_id,
    const MinicDirectMachineInstructionShape *, bool *needs_gp_store);
typedef bool (*MinicDirectMachineStoreGP)(
    FILE *, const MinicDirectLP64BlockContext *,
    const MinicCoreInstruction *, size_t result_offset,
    bool include_double);
typedef bool (*MinicDirectMachineEmitTerminator)(
    FILE *, const MinicDirectLP64BlockContext *,
    const MinicCoreBlock *, size_t block_id);

typedef struct MinicDirectMachineDispatch {
    const char *architecture;
    const char *label_prefix;
    bool allow_variadic_double_result;
    MinicDirectMachineEmitFamily emit_family;
    MinicDirectMachineStoreGP store_gp;
    MinicDirectMachineEmitTerminator emit_terminator;
} MinicDirectMachineDispatch;

static inline bool minic_multi_direct_machine_dispatch(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicDirectMachineDispatch *dispatch) {
    MinicCoreBlockWalk walk = {0};
    const MinicCoreFunction *fn;
    const MinicCoreBlock *block;
    size_t block_id;
    if (out == NULL || ctx == NULL || ctx->program == NULL ||
        ctx->target == NULL || ctx->fn == NULL || ctx->frame_plan == NULL ||
        ctx->incoming_plan == NULL || ctx->name == NULL ||
        dispatch == NULL || dispatch->architecture == NULL ||
        dispatch->label_prefix == NULL || dispatch->emit_family == NULL ||
        dispatch->store_gp == NULL || dispatch->emit_terminator == NULL)
        return false;
    fn = ctx->fn;
    minic_core_block_walk_begin(&walk, fn, NULL);
    while (minic_core_block_walk_next(&walk, &block_id, &block)) {
        size_t ordinal;
        if (fprintf(out, ".L%s_f%zu_b%zu:\n", dispatch->label_prefix,
                    ctx->id, block_id) < 0 ||
            (block->source_label_id != SIZE_MAX &&
             !fn->suppress_source_label_aliases &&
             fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0))
            return minic_multi_direct_machine_reject(
                fn, dispatch->architecture, ctx->name,
                "machine_emit", block_id, SIZE_MAX, __LINE__);
        for (ordinal = 0U; ordinal < block->instruction_count; ++ordinal) {
            MinicCoreInstructionId ins_id = 0U;
            const MinicCoreInstruction *ins = NULL;
            MinicDirectMachineInstructionShape shape;
            bool writeback = false;
            if (!minic_core_block_instruction_at(
                    fn, block, ordinal, &ins_id, &ins))
                return minic_multi_direct_machine_reject(
                    fn, dispatch->architecture, ctx->name,
                    "machine_emit", block_id, SIZE_MAX, __LINE__);
            if (!minic_multi_direct_machine_instruction_shape(
                    fn, ins, dispatch->allow_variadic_double_result, &shape))
                return minic_multi_direct_machine_reject(
                    fn, dispatch->architecture, ctx->name,
                    "machine_emit", block_id, (size_t)ins_id, __LINE__);
            /* A compiler-only barrier emits no native opcode and must not
             * request a value slot or target register writeback. */
            if (shape.family == MINIC_DIRECT_MACHINE_COMPILER_BARRIER)
                continue;
            if (!dispatch->emit_family(
                    out, ctx, ins, ins_id, block_id, &shape, &writeback))
                return minic_multi_direct_machine_reject(
                    fn, dispatch->architecture, ctx->name,
                    "machine_emit", block_id, (size_t)ins_id, __LINE__);
            /* Integer family results use only C integer/pointer width.
             * FP, ABI, memory and residual may produce double-width GP
             * transport, matching both prior target-local dispatchers. */
            if (writeback && !dispatch->store_gp(
                    out, ctx, ins, shape.result_offset,
                    shape.family != MINIC_DIRECT_MACHINE_INTEGER))
                return minic_multi_direct_machine_reject(
                    fn, dispatch->architecture, ctx->name,
                    "machine_emit", block_id, (size_t)ins_id, __LINE__);
        }
        if (!dispatch->emit_terminator(out, ctx, block, block_id))
            return minic_multi_direct_machine_reject(
                fn, dispatch->architecture, ctx->name,
                "terminator_emit", block_id, SIZE_MAX, __LINE__);
    }
    return true;
}
#endif
