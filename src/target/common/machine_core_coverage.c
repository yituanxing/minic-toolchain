#include "target/common/machine_core_coverage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct CoreMachineDemand {
    bool recognized;
    bool requires_origin;
    unsigned int required_effects;
} CoreMachineDemand;

static CoreMachineDemand core_machine_demand(MinicCoreInstructionKind kind) {
    const CoreMachineDemand pure = {true, false, 0U};
    const CoreMachineDemand origin = {true, true, 0U};
    const CoreMachineDemand read = {true, true,
        MINIC_MACHINE_EFFECT_MEMORY_READ};
    const CoreMachineDemand write = {true, true,
        MINIC_MACHINE_EFFECT_MEMORY_WRITE};
    const CoreMachineDemand read_write = {true, true,
        MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE};
    switch (kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR:
        return pure;
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE:
        return origin;
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
    case MINIC_CORE_INSTRUCTION_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
    case MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_STORE:
        return write;
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_LOAD:
        return read;
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
    case MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY:
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
    case MINIC_CORE_INSTRUCTION_VARIADIC_COPY:
        return read_write;
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return write;
    case MINIC_CORE_INSTRUCTION_CALL:
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return (CoreMachineDemand){true, true, MINIC_MACHINE_EFFECT_CALL};
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return (CoreMachineDemand){true, true,
            MINIC_MACHINE_EFFECT_SOURCE_ASM};
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return (CoreMachineDemand){true, true,
            MINIC_MACHINE_EFFECT_COMPILER_BARRIER};
    default:
        /* Newly added Core kinds have no automatic machine admission. */
        return (CoreMachineDemand){0};
    }
}

static bool machine_source_asm_matches_core(
    const MinicCoreFunction *core, const MinicCoreInstruction *instruction,
    const MinicMachineInstruction *machine_instruction) {
    MinicCoreInlineAsmId source_id;
    const MinicCoreInlineAsm *source;
    if (instruction->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM)
        source_id = instruction->value.inline_asm_id;
    else if (instruction->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM)
        source_id = instruction->value.structured_inline_asm.inline_asm_id;
    else return false;
    if (source_id >= core->inline_asm_count || core->inline_asms == NULL ||
        machine_instruction->source_asm_template == NULL) return false;
    source = &core->inline_asms[source_id];
    return source->template_text != NULL &&
        (!source->is_volatile ||
         (machine_instruction->effects & MINIC_MACHINE_EFFECT_VOLATILE) != 0U) &&
        (!source->has_memory_clobber ||
         (machine_instruction->effects &
             MINIC_MACHINE_EFFECT_COMPILER_BARRIER) != 0U) &&
        source->template_length == machine_instruction->source_asm_length &&
        memcmp(source->template_text,
               machine_instruction->source_asm_template,
               source->template_length) == 0 &&
        source->target_machine_form_id ==
            machine_instruction->source_asm_machine_form_id &&
        source->target_machine_form_variant ==
            machine_instruction->source_asm_machine_form_variant &&
        machine_instruction->has_source_asm_unique_id ==
            (source->source_asm_unique_id != 0U) &&
        source->source_asm_unique_id ==
            machine_instruction->source_asm_unique_id;
}

/* A source asm-goto is a Core instruction, not the block terminator. Its
 * explicit label edges must survive independently of the ordinary Core
 * terminator. An opaque indirect branch targets all sealed indirect entries.
 * The comparison never reparses source or assembler syntax. */
static bool core_expected_successors(const MinicCoreFunction *core,
    const bool *reachable, size_t block_id, unsigned char *expected) {
    const MinicCoreBlock *block = &core->blocks[block_id];
    const MinicCoreTerminator *term = &block->terminator;
    size_t i;
    if (!block->has_terminator) return false;
    for (i = 0U; i < block->instruction_count; ++i) {
        const MinicCoreInstruction *ins;
        const MinicCoreInlineAsm *source;
        MinicCoreInlineAsmId source_id;
        if (block->instructions[i] >= core->instruction_count) return false;
        ins = &core->instructions[block->instructions[i]];
        if (ins->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM)
            source_id = ins->value.inline_asm_id;
        else if (ins->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM)
            source_id = ins->value.structured_inline_asm.inline_asm_id;
        else continue;
        if (source_id >= core->inline_asm_count || core->inline_asms == NULL)
            return false;
        source = &core->inline_asms[source_id];
        if (!source->is_goto) continue;
        if (source->goto_target >= core->block_count ||
            !reachable[source->goto_target]) return false;
        expected[source->goto_target] = 1U;
        if (source->goto_second_target != MINIC_CORE_BLOCK_INVALID) {
            if (source->goto_second_target >= core->block_count ||
                !reachable[source->goto_second_target]) return false;
            expected[source->goto_second_target] = 1U;
        }
    }
    switch (term->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return true;
    case MINIC_CORE_TERMINATOR_BRANCH:
        if (term->branch_target >= core->block_count ||
            !reachable[term->branch_target]) return false;
        expected[term->branch_target] = 1U;
        return true;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        if (term->conditional.when_true >= core->block_count ||
            term->conditional.when_false >= core->block_count ||
            !reachable[term->conditional.when_true] ||
            !reachable[term->conditional.when_false]) return false;
        expected[term->conditional.when_true] = 1U;
        expected[term->conditional.when_false] = 1U;
        return true;
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        for (i = 0U; i < core->block_count; ++i)
            if (reachable[i] && core->blocks[i].indirect_entry)
                expected[i] = 1U;
        return true;
    default:
        return false;
    }
}

/* Follow only target-created auxiliary blocks between primary Core blocks.
 * This admits branch islands, spill/record-copy loops and conditional false
 * islands, but rejects a wrong destination even if both Core blocks are
 * globally reachable from the function entry. */
static bool machine_core_edges_match(const MinicCoreFunction *core,
    const bool *reachable, const MinicMachineFunction *machine,
    const size_t *primary, size_t *queue, size_t *visit_stamps,
    unsigned char *expected, unsigned char *actual) {
    size_t core_id;
    for (core_id = 0U; core_id < core->block_count; ++core_id) {
        size_t head = 0U, tail = 1U;
        if (!reachable[core_id]) continue;
        memset(expected, 0, core->block_count * sizeof(*expected));
        memset(actual, 0, core->block_count * sizeof(*actual));
        if (!core_expected_successors(core, reachable, core_id, expected))
            return false;
        queue[0] = primary[core_id];
        visit_stamps[primary[core_id]] = core_id + 1U;
        while (head < tail) {
            const MinicMachineBlock *block = &machine->blocks[queue[head++]];
            const size_t edge_count = block->successor_count +
                (block->has_fallthrough ? 1U : 0U);
            size_t edge;
            if (edge_count < block->successor_count) return false;
            for (edge = 0U; edge < edge_count; ++edge) {
                const size_t to = edge < block->successor_count
                    ? block->successors[edge] : block->fallthrough_target;
                const MinicMachineBlock *next;
                if (to >= machine->block_count || to == 0U) return false;
                next = &machine->blocks[to];
                if (next->has_core_block_origin) {
                    if (next->core_block_id >= core->block_count) return false;
                    actual[next->core_block_id] = 1U;
                } else if (visit_stamps[to] != core_id + 1U) {
                    visit_stamps[to] = core_id + 1U;
                    queue[tail++] = to;
                }
            }
        }
        if (memcmp(expected, actual,
                   core->block_count * sizeof(*expected)) != 0)
            return false;
    }
    return true;
}

bool minic_machine_core_coverage_verify(const MinicCoreFunction *core,
                                         const bool *reachable_blocks,
                                         const MinicMachineFunction *machine) {
    size_t *owner = NULL;
    unsigned int *effects = NULL;
    unsigned char *seen = NULL;
    unsigned char *seen_source_asm = NULL;
    size_t *primary = NULL;
    size_t *queue = NULL;
    unsigned char *visited = NULL;
    size_t *visit_stamps = NULL;
    unsigned char *expected = NULL;
    unsigned char *actual = NULL;
    size_t i;
    bool valid = false;
    if (core == NULL || reachable_blocks == NULL || machine == NULL ||
        core->block_count == 0U ||
        core->blocks == NULL || machine->block_count == 0U ||
        core->entry_block >= core->block_count ||
        !reachable_blocks[core->entry_block] ||
        machine->blocks == NULL ||
        (core->instruction_count != 0U && core->instructions == NULL) ||
        core->instruction_count > SIZE_MAX / sizeof(*owner) ||
        core->instruction_count > SIZE_MAX / sizeof(*effects) ||
        core->block_count > SIZE_MAX / sizeof(*primary) ||
        machine->block_count > SIZE_MAX / sizeof(*queue) ||
        machine->block_count > SIZE_MAX / sizeof(*visit_stamps))
        return false;
    owner = malloc((core->instruction_count == 0U ? 1U :
                    core->instruction_count) * sizeof(*owner));
    effects = calloc(core->instruction_count == 0U ? 1U :
                     core->instruction_count, sizeof(*effects));
    seen = calloc(core->instruction_count == 0U ? 1U :
                  core->instruction_count, sizeof(*seen));
    seen_source_asm = calloc(core->instruction_count == 0U ? 1U :
                             core->instruction_count, sizeof(*seen_source_asm));
    primary = malloc(core->block_count * sizeof(*primary));
    queue = malloc(machine->block_count * sizeof(*queue));
    visited = calloc(machine->block_count, sizeof(*visited));
    visit_stamps = calloc(machine->block_count, sizeof(*visit_stamps));
    expected = calloc(core->block_count, sizeof(*expected));
    actual = calloc(core->block_count, sizeof(*actual));
    if (owner == NULL || effects == NULL || seen == NULL ||
        seen_source_asm == NULL || primary == NULL || queue == NULL ||
        visited == NULL || visit_stamps == NULL || expected == NULL ||
        actual == NULL) goto cleanup;
    for (i = 0U; i < core->instruction_count; ++i) owner[i] = SIZE_MAX;
    for (i = 0U; i < core->block_count; ++i) primary[i] = SIZE_MAX;
    for (i = 0U; i < core->block_count; ++i) {
        const MinicCoreBlock *block = &core->blocks[i];
        size_t j;
        if (block->instruction_count != 0U && block->instructions == NULL)
            goto cleanup;
        for (j = 0U; j < block->instruction_count; ++j) {
            const size_t id = block->instructions[j];
            if (id >= core->instruction_count || owner[id] != SIZE_MAX ||
                !core_machine_demand(core->instructions[id].kind).recognized)
                goto cleanup;
            owner[id] = i;
        }
    }
    for (i = 0U; i < machine->block_count; ++i) {
        const MinicMachineBlock *block = &machine->blocks[i];
        size_t j;
        if ((block->instruction_count != 0U && block->instructions == NULL) ||
            (block->successor_count != 0U && block->successors == NULL) ||
            (block->has_fallthrough &&
             block->fallthrough_target >= machine->block_count))
            goto cleanup;
        if (block->has_core_block_origin) {
            const size_t id = block->core_block_id;
            if (block->machine_auxiliary || id >= core->block_count ||
                !reachable_blocks[id] || primary[id] != SIZE_MAX ||
                block->indirect_entry != core->blocks[id].indirect_entry)
                goto cleanup;
            primary[id] = i;
        } else if (block->core_block_id != 0U) {
            goto cleanup;
        }
        for (j = 0U; j < block->instruction_count; ++j) {
            const MinicMachineInstruction *ins = &block->instructions[j];
            size_t id;
            if (!ins->has_core_origin) continue;
            id = ins->core_instruction_id;
            if (ins->core_block_id >= core->block_count ||
                !reachable_blocks[ins->core_block_id] ||
                (block->has_core_block_origin &&
                 ins->core_block_id != block->core_block_id) ||
                id >= core->instruction_count ||
                owner[id] != ins->core_block_id ||
                ins->kind == MINIC_MACHINE_INSTRUCTION_DIRECTIVE ||
                ins->kind == MINIC_MACHINE_INSTRUCTION_LOCAL_LABEL)
                goto cleanup;
            if (ins->kind == MINIC_MACHINE_INSTRUCTION_SOURCE_ASM) {
                if (!machine_source_asm_matches_core(
                        core, &core->instructions[id], ins)) goto cleanup;
                seen_source_asm[id] = 1U;
            }
            seen[id] = 1U;
            effects[id] |= ins->effects;
        }
    }
    /* The native function entry is ABI code, never a Core entry alias.  In
     * particular, a backedge to the first Core block must bypass prologue. */
    if (!machine->blocks[0].machine_auxiliary ||
        machine->blocks[0].has_core_block_origin) goto cleanup;
    for (i = 0U; i < core->block_count; ++i)
        if (reachable_blocks[i] && primary[i] == SIZE_MAX) goto cleanup;
    /* First Core destination reached by walking only ABI/branch-island
     * auxiliary blocks must be the actual Core entry (which need not be 0). */
    {
        size_t head = 0U, tail = 1U;
        bool reached_entry = false;
        queue[0] = 0U;
        visited[0] = 1U;
        while (head < tail) {
            const MinicMachineBlock *block = &machine->blocks[queue[head++]];
            const size_t edge_count = block->successor_count +
                (block->has_fallthrough ? 1U : 0U);
            size_t edge;
            if (edge_count < block->successor_count) goto cleanup;
            for (edge = 0U; edge < edge_count; ++edge) {
                const size_t to = edge < block->successor_count
                    ? block->successors[edge] : block->fallthrough_target;
                const MinicMachineBlock *next;
                if (to >= machine->block_count) goto cleanup;
                next = &machine->blocks[to];
                if (next->has_core_block_origin) {
                    if (next->core_block_id != core->entry_block) goto cleanup;
                    reached_entry = true;
                } else if (!visited[to]) {
                    visited[to] = 1U;
                    queue[tail++] = to;
                }
            }
        }
        if (!reached_entry) goto cleanup;
    }
    /* Every reachable Core block must also be physically reachable through
     * the selected Machine CFG, including the opaque indirect entries. */
    memset(visited, 0, machine->block_count * sizeof(*visited));
    {
        size_t head = 0U, tail = 1U;
        queue[0] = 0U;
        visited[0] = 1U;
        while (head < tail) {
            const MinicMachineBlock *block = &machine->blocks[queue[head++]];
            const size_t edge_count = block->successor_count +
                (block->has_fallthrough ? 1U : 0U);
            size_t edge;
            if (edge_count < block->successor_count) goto cleanup;
            for (edge = 0U; edge < edge_count; ++edge) {
                const size_t to = edge < block->successor_count
                    ? block->successors[edge] : block->fallthrough_target;
                if (to >= machine->block_count) goto cleanup;
                if (!visited[to]) {
                    visited[to] = 1U;
                    queue[tail++] = to;
                }
            }
        }
    }
    for (i = 0U; i < core->block_count; ++i)
        if (reachable_blocks[i] && !visited[primary[i]]) goto cleanup;
    for (i = 0U; i < machine->block_count; ++i) {
        const MinicMachineBlock *block = &machine->blocks[i];
        size_t j;
        if (visited[i]) continue;
        /* An unreachable physical island cannot prove a live Core effect. */
        for (j = 0U; j < block->instruction_count; ++j)
            if (block->instructions[j].has_core_origin) goto cleanup;
    }
    if (!machine_core_edges_match(core, reachable_blocks, machine, primary,
                                  queue, visit_stamps, expected, actual))
        goto cleanup;
    for (i = 0U; i < core->instruction_count; ++i) {
        CoreMachineDemand demand;
        if (owner[i] == SIZE_MAX || !reachable_blocks[owner[i]]) continue;
        demand = core_machine_demand(core->instructions[i].kind);
        if (demand.requires_origin &&
            (!seen[i] || (effects[i] & demand.required_effects) !=
                          demand.required_effects))
            goto cleanup;
        if ((core->instructions[i].kind ==
                MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
             core->instructions[i].kind ==
                MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) &&
            !seen_source_asm[i]) goto cleanup;
        /* Source volatility is semantic even for otherwise pure loads. */
        if (core->instructions[i].kind == MINIC_CORE_INSTRUCTION_LOAD &&
            core->instructions[i].value.load.is_volatile &&
            (effects[i] & MINIC_MACHINE_EFFECT_VOLATILE) == 0U)
            goto cleanup;
        if (core->instructions[i].kind == MINIC_CORE_INSTRUCTION_STORE &&
            core->instructions[i].value.store.is_volatile &&
            (effects[i] & MINIC_MACHINE_EFFECT_VOLATILE) == 0U)
            goto cleanup;
        if (core->instructions[i].kind == MINIC_CORE_INSTRUCTION_RECORD_LOAD &&
            core->instructions[i].value.record_load.is_volatile &&
            (effects[i] & MINIC_MACHINE_EFFECT_VOLATILE) == 0U)
            goto cleanup;
        if (core->instructions[i].kind == MINIC_CORE_INSTRUCTION_RECORD_COPY &&
            (core->instructions[i].value.record_copy.source_volatile ||
             core->instructions[i].value.record_copy.destination_volatile) &&
            (effects[i] & MINIC_MACHINE_EFFECT_VOLATILE) == 0U)
            goto cleanup;
        if (core->instructions[i].kind ==
                MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY &&
            (core->instructions[i].value.vector_storage_copy.source_volatile ||
             core->instructions[i].value.vector_storage_copy.destination_volatile) &&
            (effects[i] & MINIC_MACHINE_EFFECT_VOLATILE) == 0U)
            goto cleanup;
    }
    valid = true;
cleanup:
    free(owner);
    free(effects);
    free(seen);
    free(seen_source_asm);
    free(primary);
    free(queue);
    free(visited);
    free(visit_stamps);
    free(expected);
    free(actual);
    return valid;
}
