#include "compiler/core_reachability.h"
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Preserve original CFG-root and specialization pruning behavior,
 * including non-Core relocation roots and allocation-failure fallback. */
static MinicFunctionId minic_core_named_function_id(
    const MinicC0Program *program, const char *name, size_t name_length) {
    size_t function_index;

    if (program == NULL || name == NULL || name_length == 0U) {
        return MINIC_FUNCTION_INVALID;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        const char *symbol_name = minic_c0_function_symbol_name(function);
        size_t symbol_name_length;

        if (symbol_name == NULL) {
            continue;
        }
        symbol_name_length = function->assembler_name != NULL
                                 ? function->assembler_name_length
                                 : function->name_length;
        if (symbol_name_length == name_length &&
            memcmp(symbol_name, name, name_length) == 0) {
            return function_index;
        }
    }
    return MINIC_FUNCTION_INVALID;
}

static bool minic_enqueue_core_named_function(
    const MinicC0Program *program,
    const char *name,
    size_t name_length,
    bool *reachable,
    MinicFunctionId *queue,
    size_t *queue_count) {
    MinicFunctionId target;

    if (program == NULL || reachable == NULL || queue == NULL || queue_count == NULL) {
        return false;
    }
    target = minic_core_named_function_id(program, name, name_length);
    if (target == MINIC_FUNCTION_INVALID || target >= program->function_count ||
        !program->functions[target].is_defined || reachable[target]) {
        return true;
    }
    if (*queue_count >= program->function_count) {
        return false;
    }
    reachable[target] = true;
    queue[(*queue_count)++] = target;
    return true;
}

static bool minic_enqueue_core_block(MinicCoreBlockId block_id,
                                     size_t block_count,
                                     bool *reachable,
                                     MinicCoreBlockId *queue,
                                     size_t *queue_count) {
    if (reachable == NULL || queue == NULL || queue_count == NULL ||
        block_id >= block_count) {
        return false;
    }
    if (reachable[block_id]) {
        return true;
    }
    if (*queue_count >= block_count) {
        return false;
    }
    reachable[block_id] = true;
    queue[(*queue_count)++] = block_id;
    return true;
}

static bool minic_walk_reachable_core_edges(const MinicC0Program *program,
                                            const MinicCoreFunction *core,
                                            bool *function_reachable,
                                            MinicFunctionId *function_queue,
                                            size_t *function_queue_count,
                                            bool emit_reachable_core_blocks_only) {
    bool *block_reachable = NULL;
    MinicCoreBlockId *block_queue = NULL;
    size_t block_queue_count = 0U;
    size_t block_queue_cursor = 0U;
    bool success = false;

    if (program == NULL || core == NULL || function_reachable == NULL ||
        function_queue == NULL || function_queue_count == NULL) {
        return false;
    }
    if (core->block_count == 0U) {
        return true;
    }
    if (core->blocks == NULL ||
        (core->instruction_count != 0U && core->instructions == NULL) ||
        core->entry_block >= core->block_count) {
        return false;
    }
    block_reachable = (bool *)calloc(core->block_count, sizeof(*block_reachable));
    block_queue = (MinicCoreBlockId *)malloc(core->block_count * sizeof(*block_queue));
    if (block_reachable == NULL || block_queue == NULL) {
        goto done;
    }
    /* Match the selected target's actual ELF reference universe. The two
     * RISC-V emitters filter Core blocks with the shared CFG root algorithm,
     * including source labels. Other backends still write every block; keep
     * their conservative all-block reference traversal unchanged. */
    if (emit_reachable_core_blocks_only) {
        size_t block_index;
        if (!minic_core_function_mark_reachable_blocks(
                core, block_reachable, core->block_count)) {
            goto done;
        }
        for (block_index = 0U; block_index < core->block_count; ++block_index) {
            if (block_reachable[block_index]) {
                block_queue[block_queue_count++] = (MinicCoreBlockId)block_index;
            }
        }
    } else {
        size_t root_block;
        for (root_block = 0U; root_block < core->block_count; ++root_block) {
            if (!minic_enqueue_core_block((MinicCoreBlockId)root_block,
                                          core->block_count,
                                          block_reachable,
                                          block_queue,
                                          &block_queue_count)) {
                goto done;
            }
        }
    }

    while (block_queue_cursor < block_queue_count) {
        MinicCoreBlockId block_id = block_queue[block_queue_cursor++];
        const MinicCoreBlock *block = &core->blocks[block_id];
        size_t block_instruction_index;

        /* A valid block may contain only a terminator and therefore own no
         * instruction-id array.  Require storage only when the block actually
         * contains instructions. */
        if (block->instruction_count != 0U && block->instructions == NULL) {
            goto done;
        }

        for (block_instruction_index = 0U;
             block_instruction_index < block->instruction_count;
             ++block_instruction_index) {
            MinicCoreInstructionId instruction_id =
                block->instructions[block_instruction_index];
            const MinicCoreInstruction *instruction;

            if (instruction_id >= core->instruction_count) {
                goto done;
            }
            instruction = &core->instructions[instruction_id];
            /* FIXME(MC-064): opaque inline ASM may refer to a static
             * function solely through a GNU `i` symbolic operand; lowering
             * preserves only text, so no FUNCTION_ADDRESS edge is visited. */
            if (instruction->kind == MINIC_CORE_INSTRUCTION_CALL) {
                MinicCoreCalleeId callee_id = instruction->value.call.callee_id;
                const MinicCoreCallee *callee;
                if (callee_id >= core->callee_count) {
                    goto done;
                }
                callee = &core->callees[callee_id];
                if (!minic_enqueue_core_named_function(program,
                                                       callee->name,
                                                       callee->name_length,
                                                       function_reachable,
                                                       function_queue,
                                                       function_queue_count)) {
                    goto done;
                }
            } else if (instruction->kind == MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS) {
                MinicCoreFunctionSymbolId symbol_id = instruction->value.function_symbol_id;
                const MinicCoreFunctionSymbol *symbol;
                if (symbol_id >= core->function_symbol_count) {
                    goto done;
                }
                symbol = &core->function_symbols[symbol_id];
                if (!minic_enqueue_core_named_function(program,
                                                       symbol->name,
                                                       symbol->name_length,
                                                       function_reachable,
                                                       function_queue,
                                                       function_queue_count)) {
                    goto done;
                }
            }
        }

        if (!block->has_terminator) {
            continue;
        }
        switch (block->terminator.kind) {
            case MINIC_CORE_TERMINATOR_RETURN:
            case MINIC_CORE_TERMINATOR_UNREACHABLE:
                break;
            case MINIC_CORE_TERMINATOR_BRANCH:
                if (!minic_enqueue_core_block(block->terminator.branch_target,
                                             core->block_count,
                                             block_reachable,
                                             block_queue,
                                             &block_queue_count)) {
                    goto done;
                }
                break;
            case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
                if (!minic_enqueue_core_block(block->terminator.conditional.when_true,
                                             core->block_count,
                                             block_reachable,
                                             block_queue,
                                             &block_queue_count) ||
                    !minic_enqueue_core_block(block->terminator.conditional.when_false,
                                             core->block_count,
                                             block_reachable,
                                             block_queue,
                                             &block_queue_count)) {
                    goto done;
                }
                break;
            case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH: {
                size_t target_block;
                /* The Core value feeding a computed goto can select any
                 * address-taken label. Conservatively retaining all blocks is
                 * correct; ordinary direct/conditional CFG stays precise. */
                for (target_block = 0U; target_block < core->block_count; ++target_block) {
                    if (!minic_enqueue_core_block((MinicCoreBlockId)target_block,
                                                 core->block_count,
                                                 block_reachable,
                                                 block_queue,
                                                 &block_queue_count)) {
                        goto done;
                    }
                }
                break;
            }
            default:
                goto done;
        }
    }
    success = true;

done:
    free(block_queue);
    free(block_reachable);
    return success;
}


static bool minic_inline_source_has_noncore_root(
    const MinicC0Program *program, MinicFunctionId source_id) {
    size_t function_index;
    size_t object_index;

    if (program == NULL || source_id >= program->function_count) {
        return true;
    }
    if (program->entry_function == source_id || program->functions[source_id].force_emit) {
        return true;
    }
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        if (program->functions[function_index].alias_target == source_id) {
            return true;
        }
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        if (!minic_c0_program_global_object_emittable(program, object))
            continue;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                (MinicFunctionId)relocation->target_id == source_id) {
                return true;
            }
        }
    }
    return false;
}

/* Core pruning asks the same non-Core-root question for many functions.
 * Collect the immutable entry/force-emit/alias/relocation facts once for this
 * pass. Core pruning only changes is_referenced, not these root inputs.
 * Allocation failure retains the previous per-function scan. */
static bool *minic_build_noncore_root_index(const MinicC0Program *program) {
    bool *roots;
    bool *owner_live;
    size_t function_index;
    size_t object_index;

    if (program == NULL) {
        return NULL;
    }
    roots = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                           sizeof(*roots));
    if (roots == NULL) {
        return NULL;
    }
    owner_live = minic_c0_program_build_global_owner_live_map(program);
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunctionId alias_target = program->functions[function_index].alias_target;
        if ((MinicFunctionId)function_index == program->entry_function ||
            program->functions[function_index].force_emit) {
            roots[function_index] = true;
        }
        if (alias_target < program->function_count) {
            roots[alias_target] = true;
        }
    }
    for (object_index = 0U; object_index < program->global_object_count; ++object_index) {
        const MinicGlobalObject *object = &program->global_objects[object_index];
        size_t relocation_index;
        if (!minic_c0_program_global_object_emittable_with_map(
                program, object, owner_live))
            continue;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            const MinicGlobalRelocation *relocation = &object->relocations[relocation_index];
            MinicFunctionId target = (MinicFunctionId)relocation->target_id;
            if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                target < program->function_count) {
                roots[target] = true;
            }
        }
    }
    free(owner_live);
    return roots;
}

static bool minic_inline_source_is_core_reachable_only(
    const MinicC0Program *program, MinicFunctionId source_id,
    const bool *noncore_roots) {
    const MinicFunction *source;

    if (program == NULL || source_id >= program->function_count) {
        return false;
    }
    source = &program->functions[source_id];
    /* Internal function bodies are implementation details, not emission roots.
     * Parser-level is_referenced can stay set after every executable call was
     * rewritten, specialized, or folded away.  Let the lowered Core graph pull
     * the body back in when a reachable CALL or FUNCTION_ADDRESS still needs
     * it; preserve only entry/force-emit/alias/global-relocation roots that live
     * outside that graph.  This applies to ordinary static functions as well as
     * static inline helpers so dead CONFIG-disabled helper subgraphs disappear. */
    return source->is_defined && source->is_internal &&
           !(noncore_roots != NULL
                 ? noncore_roots[source_id]
                 : minic_inline_source_has_noncore_root(program, source_id));
}

bool minic_prune_inline_specializations_from_core(
    MinicC0Program *program, const MinicCoreFunctionSet *set,
    bool emit_reachable_core_blocks_only) {
    bool *reachable = NULL;
    bool *processed = NULL;
    bool *noncore_roots = NULL;
    MinicFunctionId *queue = NULL;
    size_t queue_count = 0U;
    size_t queue_cursor = 0U;
    size_t function_index;
    size_t kept = 0U;
    size_t pruned = 0U;
    bool success = false;

    if (program == NULL || set == NULL || set->function_count != program->function_count ||
        (set->function_count != 0U &&
         (set->functions == NULL || set->statuses == NULL))) {
        return false;
    }
    reachable = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                               sizeof(*reachable));
    processed = (bool *)calloc(program->function_count == 0U ? 1U : program->function_count,
                               sizeof(*processed));
    queue = (MinicFunctionId *)malloc(
        (program->function_count == 0U ? 1U : program->function_count) * sizeof(*queue));
    if (reachable == NULL || processed == NULL || queue == NULL) {
        goto done;
    }

    /* All inputs to non-Core-root classification are stable throughout this
     * pass: only is_referenced changes below. If this small index cannot be
     * allocated, preserve the previous conservative per-query scan. */
    noncore_roots = minic_build_noncore_root_index(program);

    /* Preserve the established root policy for ordinary functions. Synthetic
     * specialization clones are never roots: they survive only through an
     * actual instruction in a CFG-reachable lowered Core block. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization || !function->is_defined ||
            (function->is_internal && !function->is_referenced)) {
            continue;
        }
        /* Parser-level references are only a discovery hint for internal
         * functions.  The final lowered Core graph is the authoritative
         * executable reachability source; non-Core roots stay conservative. */
        if (minic_inline_source_is_core_reachable_only(
                program, (MinicFunctionId)function_index, noncore_roots)) {
            continue;
        }
        reachable[function_index] = true;
        queue[queue_count++] = function_index;
    }

    while (queue_cursor < queue_count) {
        MinicFunctionId caller_id = queue[queue_cursor++];

        if (caller_id >= set->function_count || processed[caller_id]) {
            continue;
        }
        processed[caller_id] = true;
        if (set->statuses[caller_id] != MINIC_CORE_LOWER_OK) {
            continue;
        }
        if (!minic_walk_reachable_core_edges(program,
                                             &set->functions[caller_id],
                                             reachable,
                                             queue,
                                             &queue_count,
                                             emit_reachable_core_blocks_only)) {
            goto done;
        }
    }

    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (!function->is_integer_specialization) {
            if (minic_inline_source_is_core_reachable_only(
                    program, (MinicFunctionId)function_index, noncore_roots)) {
                function->is_referenced = reachable[function_index];
            }
            continue;
        }
        function->is_referenced = reachable[function_index];
        if (function->is_referenced) {
            kept += 1U;
        } else {
            pruned += 1U;
        }
    }
    if (kept != 0U || pruned != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_SPECIALIZATION_CORE_REACHABILITY_V0 kept=%zu pruned=%zu\n",
                      kept,
                      pruned);
    }
    success = true;

done:
    free(noncore_roots);
    free(queue);
    free(processed);
    free(reachable);
    return success;
}

