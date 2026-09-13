#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

anchor = '''static bool minic_validate_core_functions(const char *input_path,
'''
helper = r'''
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
                                            size_t *function_queue_count) {
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
    if (core->blocks == NULL || core->instructions == NULL ||
        core->entry_block >= core->block_count) {
        return false;
    }
    block_reachable = (bool *)calloc(core->block_count, sizeof(*block_reachable));
    block_queue = (MinicCoreBlockId *)malloc(core->block_count * sizeof(*block_queue));
    if (block_reachable == NULL || block_queue == NULL ||
        !minic_enqueue_core_block(core->entry_block,
                                 core->block_count,
                                 block_reachable,
                                 block_queue,
                                 &block_queue_count)) {
        goto done;
    }

    while (block_queue_cursor < block_queue_count) {
        MinicCoreBlockId block_id = block_queue[block_queue_cursor++];
        const MinicCoreBlock *block = &core->blocks[block_id];
        size_t block_instruction_index;

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

static bool minic_prune_inline_specializations_from_core(
    MinicC0Program *program, const MinicCoreFunctionSet *set) {
    bool *reachable = NULL;
    bool *processed = NULL;
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

    /* Preserve the established root policy for ordinary functions. Synthetic
     * specialization clones are never roots: they survive only through an
     * actual instruction in a CFG-reachable lowered Core block. */
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function = &program->functions[function_index];
        if (function->is_integer_specialization || !function->is_defined ||
            (function->is_internal && !function->is_referenced)) {
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
                                             &queue_count)) {
            goto done;
        }
    }

    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        MinicFunction *function = &program->functions[function_index];
        if (!function->is_integer_specialization) {
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
    free(queue);
    free(processed);
    free(reachable);
    return success;
}

'''
count = text.count(anchor)
if count != 1:
    raise SystemExit(f"expected one core validation anchor, found {count}")
text = text.replace(anchor, helper + anchor, 1)

old = '''    if (minic_bootstrap_trace_enabled()) {
        minic_bootstrap_trace_stage(input_path,
                                    "core-set",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "core-validate", "begin", program.function_count);
'''
new = '''    if (minic_bootstrap_trace_enabled()) {
        minic_bootstrap_trace_stage(input_path,
                                    "core-set",
                                    success ? "end-ok" : "end-fail",
                                    program.function_count);
    }
    if (success && !minic_prune_inline_specializations_from_core(&program, &core_set)) {
        minic_set_diagnostic(diagnostic,
                             input_path,
                             1U,
                             1U,
                             "cannot prune inline specialization Core reachability");
        success = false;
    }
    if (success) {
        minic_bootstrap_trace_stage(input_path, "core-validate", "begin", program.function_count);
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one core-set/validate anchor, found {count}")
text = text.replace(old, new, 1)
p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_CORE_REACHABILITY_V0=APPLIED")
