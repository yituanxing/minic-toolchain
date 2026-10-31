#ifndef MINIC_TARGET_COMMON_DIRECT_MACHINE_DISPATCH_H
#define MINIC_TARGET_COMMON_DIRECT_MACHINE_DISPATCH_H

#include "target/common/core_block_walk.h"
#include "target/common/direct_block_context.h"
#include "target/common/direct_block_register.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/direct_program_emit.h"
#include "core/value_uses.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
typedef bool (*MinicDirectMachineNoNativeOpcode)(
    const MinicDirectLP64BlockContext *, const MinicCoreInstruction *);
typedef bool (*MinicDirectMachineImmediateRHS)(
    const MinicDirectLP64BlockContext *, const MinicCoreInstruction *,
    MinicCoreValueId *);

typedef struct MinicDirectMachineDispatch {
    const char *architecture;
    const char *label_prefix;
    bool allow_variadic_double_result;
    bool allow_adjacent_gp_forward;
    MinicDirectMachineEmitFamily emit_family;
    MinicDirectMachineStoreGP store_gp;
    MinicDirectMachineEmitTerminator emit_terminator;
    MinicDirectMachineNoNativeOpcode no_native_opcode;
    /* An ISA-only proof that this consumer reads its constant RHS directly
     * from the Core defining instruction as a machine immediate. */
    MinicDirectMachineImmediateRHS immediate_rhs;
} MinicDirectMachineDispatch;

static inline bool minic_multi_direct_machine_dispatch(
    FILE *out, const MinicDirectLP64BlockContext *ctx,
    const MinicDirectMachineDispatch *dispatch) {
    MinicCoreBlockWalk walk = {0};
    const MinicCoreFunction *fn;
    const MinicCoreBlock *block;
    bool *reachable;
    bool *folded_constants = NULL;
    size_t *value_use_counts = NULL;
    bool *transparent = NULL;
    size_t transparent_capacity = 0U;
    bool have_value_use_counts = false;
    bool success = false;
    size_t block_id;
    if (out == NULL || ctx == NULL || ctx->program == NULL ||
        ctx->target == NULL || ctx->fn == NULL || ctx->frame_plan == NULL ||
        ctx->incoming_plan == NULL || ctx->name == NULL ||
        dispatch == NULL || dispatch->architecture == NULL ||
        dispatch->label_prefix == NULL || dispatch->emit_family == NULL ||
        dispatch->store_gp == NULL || dispatch->emit_terminator == NULL)
        return false;
    fn = ctx->fn;
    /* A Core result without any semantic use needs no machine frame
     * writeback. Preserve the actual instruction and all memory/volatile/
     * atomic effects; this only discards an unused SSA copy. The same Core
     * use visitor already owns duplicate operands and terminator uses for
     * the stack-slot reuse planner, so targets must not guess liveness.
     * If the optional census cannot be allocated, retain legacy writebacks. */
    if (fn->value_count != 0U &&
        fn->value_count <= SIZE_MAX / sizeof(*value_use_counts)) {
        value_use_counts = (size_t *)malloc(
            fn->value_count * sizeof(*value_use_counts));
        if (value_use_counts != NULL)
            have_value_use_counts = minic_core_function_value_use_counts(
                fn, value_use_counts, fn->value_count);
    }
    /* The same source-label-aware CFG roots as RV32/RV64: ordinary entry,
     * externally entered labels, asm-goto and computed-goto destinations.
     * A GNU jump-table relocation can name a source label even when no
     * ordinary branch reaches it, so never use entry-only traversal here.
     * Keep machine branch lowering/ABI ownership entirely in the target. */
    reachable = (bool *)calloc(fn->block_count == 0U ? 1U : fn->block_count,
                                sizeof(*reachable));
    if (reachable == NULL || !minic_core_function_mark_reachable_blocks(
            fn, reachable, fn->block_count)) {
        free(reachable);
        free(value_use_counts);
        return minic_multi_direct_machine_reject(
            fn, dispatch->architecture, ctx->name,
            "cfg_reachability", SIZE_MAX, SIZE_MAX, __LINE__);
    }
    /* O(instructions) preflight using the existing exact semantic use counts.
     * When the ONLY use of an INTEGER_CONSTANT is a target-certified
     * immediate RHS, the pure producer needs neither a native `li` nor an
     * SSA frame writeback. All other constants keep their original emission.
     * This does not rearrange side effects, calls, inline asm, or CFG edges. */
    if (dispatch->immediate_rhs != NULL && have_value_use_counts &&
        fn->value_count != 0U) {
        folded_constants = (bool *)calloc(fn->value_count,
                                           sizeof(*folded_constants));
        if (folded_constants != NULL) {
            size_t candidate_block;
            for (candidate_block = 0U;
                 candidate_block < fn->block_count; ++candidate_block) {
                const MinicCoreBlock *candidate;
                size_t candidate_ordinal;
                if (!reachable[candidate_block]) continue;
                candidate = &fn->blocks[candidate_block];
                for (candidate_ordinal = 0U;
                     candidate_ordinal < candidate->instruction_count;
                     ++candidate_ordinal) {
                    MinicCoreInstructionId candidate_id;
                    const MinicCoreInstruction *consumer;
                    const MinicCoreInstruction *def;
                    MinicCoreValueId rhs;
                    if (!minic_core_block_instruction_at(
                            fn, candidate, candidate_ordinal,
                            &candidate_id, &consumer) ||
                        !dispatch->immediate_rhs(ctx, consumer, &rhs) ||
                        rhs >= fn->value_count ||
                        value_use_counts[rhs] != 1U ||
                        fn->values[rhs].definition >= fn->instruction_count)
                        continue;
                    def = &fn->instructions[fn->values[rhs].definition];
                    if (def->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT &&
                        def->result == rhs)
                        folded_constants[rhs] = true;
                }
            }
        }
    }
    minic_core_block_walk_begin(&walk, fn, reachable);
    while (minic_core_block_walk_next(&walk, &block_id, &block)) {
        size_t ordinal;
        bool have_transparency = false;
        /* The target can certify a pure Core instruction emitted *without*
         * any native opcodes, commonly a folded local OBJECT_ADDRESS. Such
         * source-level instructions need not break the physical GP lifetime.
         * A failed optional census falls back to exact adjacency. */
        if (dispatch->allow_adjacent_gp_forward &&
            have_value_use_counts &&
            dispatch->no_native_opcode != NULL &&
            block->instruction_count != 0U) {
            if (block->instruction_count > transparent_capacity) {
                bool *expanded = (bool *)realloc(
                    transparent, block->instruction_count * sizeof(*transparent));
                if (expanded != NULL) {
                    transparent = expanded;
                    transparent_capacity = block->instruction_count;
                }
            }
            if (transparent_capacity >= block->instruction_count) {
                have_transparency = true;
                for (ordinal = 0U; ordinal < block->instruction_count;
                     ++ordinal) {
                    MinicCoreInstructionId candidate_id;
                    const MinicCoreInstruction *candidate;
                    transparent[ordinal] =
                        minic_core_block_instruction_at(
                            fn, block, ordinal, &candidate_id, &candidate) &&
                        ((folded_constants != NULL &&
                          candidate->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT &&
                          candidate->result != MINIC_CORE_VALUE_INVALID &&
                          candidate->result < fn->value_count &&
                          folded_constants[candidate->result]) ||
                         dispatch->no_native_opcode(ctx, candidate));
                }
            }
        }
        if (fprintf(out, ".L%s_f%zu_b%zu:\n", dispatch->label_prefix,
                    ctx->id, block_id) < 0 ||
            (block->source_label_id != SIZE_MAX &&
             !fn->suppress_source_label_aliases &&
             fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0)) {
            success = minic_multi_direct_machine_reject(
                fn, dispatch->architecture, ctx->name,
                "machine_emit", block_id, SIZE_MAX, __LINE__);
            goto done;
        }
        for (ordinal = 0U; ordinal < block->instruction_count; ++ordinal) {
            MinicCoreInstructionId ins_id = 0U;
            const MinicCoreInstruction *ins = NULL;
            MinicDirectMachineInstructionShape shape;
            bool writeback = false;
            bool forward_to_successor = false;
            if (!minic_core_block_instruction_at(
                    fn, block, ordinal, &ins_id, &ins)) {
                success = minic_multi_direct_machine_reject(
                    fn, dispatch->architecture, ctx->name,
                    "machine_emit", block_id, SIZE_MAX, __LINE__);
                goto done;
            }
            /* Materializing an unused immutable Core constant cannot have
             * an observable effect. The use census already determines which
             * scalar result writebacks are dead; extend the same proof to
             * the corresponding no-effect constant machine instruction. */
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT &&
                ins->result != MINIC_CORE_VALUE_INVALID &&
                ins->result < fn->value_count &&
                ((have_value_use_counts &&
                  value_use_counts[ins->result] == 0U) ||
                 (folded_constants != NULL &&
                  folded_constants[ins->result])))
                continue;
            if (!minic_multi_direct_machine_instruction_shape(
                    fn, ins, dispatch->allow_variadic_double_result, &shape)) {
                success = minic_multi_direct_machine_reject(
                    fn, dispatch->architecture, ctx->name,
                    "machine_emit", block_id, (size_t)ins_id, __LINE__);
                goto done;
            }
            /* Shared Core liveness selects the location pair; a backend
             * opts in only for opcode families it can emit with matching
             * physical result/lhs registers. The producer's only use is
             * the next instruction, so no frame writeback is necessary. */
            shape.lhs_in_previous_gp = false;
            shape.native_opcode_presence_checked = have_transparency;
            shape.emits_no_native_opcode =
                have_transparency && transparent[ordinal];
            if (dispatch->allow_adjacent_gp_forward &&
                have_value_use_counts) {
                MinicCoreInstructionId neighbor_id;
                const MinicCoreInstruction *neighbor;
                size_t previous = ordinal;
                size_t next = ordinal + 1U;
                while (previous > 0U && have_transparency &&
                       transparent[previous - 1U])
                    --previous;
                if (previous > 0U &&
                    minic_core_block_instruction_at(fn, block, previous - 1U,
                        &neighbor_id, &neighbor))
                    shape.lhs_in_previous_gp =
                        minic_multi_direct_gp_adjacent_forward(
                            fn, neighbor, ins, value_use_counts);
                while (next < block->instruction_count &&
                       have_transparency && transparent[next])
                    ++next;
                if (next < block->instruction_count &&
                    minic_core_block_instruction_at(fn, block, next,
                        &neighbor_id, &neighbor))
                    forward_to_successor =
                        minic_multi_direct_gp_adjacent_forward(
                            fn, ins, neighbor, value_use_counts);
            }
            /* Semantic classification validates the Core result identity;
             * physical SSA slots belong to the once-built LP64 FramePlan.
             * No machine family may silently return to value_id * 8. */
            if (ins->result != MINIC_CORE_VALUE_INVALID &&
                !minic_lp64_frame_plan_value_slot(
                    ctx->frame_plan, ins->result, &shape.result_offset)) {
                success = minic_multi_direct_machine_reject(
                    fn, dispatch->architecture, ctx->name,
                    "ssa_frame_slot", block_id, (size_t)ins_id, __LINE__);
                goto done;
            }
            /* A compiler-only barrier emits no native opcode and must not
             * request a value slot or target register writeback. */
            if (shape.family == MINIC_DIRECT_MACHINE_COMPILER_BARRIER)
                continue;
            if (!dispatch->emit_family(
                    out, ctx, ins, ins_id, block_id, &shape, &writeback)) {
                success = minic_multi_direct_machine_reject(
                    fn, dispatch->architecture, ctx->name,
                    "machine_emit", block_id, (size_t)ins_id, __LINE__);
                goto done;
            }
            /* Integer family results use only C integer/pointer width.
             * FP, ABI, memory and residual may produce double-width GP
             * transport, matching both prior target-local dispatchers. */
            if (writeback && have_value_use_counts &&
                ins->result != MINIC_CORE_VALUE_INVALID &&
                value_use_counts[ins->result] == 0U)
                writeback = false;
            if (forward_to_successor)
                writeback = false;
            if (writeback && !dispatch->store_gp(
                    out, ctx, ins, shape.result_offset,
                    shape.family != MINIC_DIRECT_MACHINE_INTEGER)) {
                success = minic_multi_direct_machine_reject(
                    fn, dispatch->architecture, ctx->name,
                    "machine_emit", block_id, (size_t)ins_id, __LINE__);
                goto done;
            }
        }
        if (!dispatch->emit_terminator(out, ctx, block, block_id)) {
            success = minic_multi_direct_machine_reject(
                fn, dispatch->architecture, ctx->name,
                "terminator_emit", block_id, SIZE_MAX, __LINE__);
            goto done;
        }
    }
    success = true;
done:
    free(reachable);
    free(folded_constants);
    free(transparent);
    free(value_use_counts);
    return success;
}
#endif
