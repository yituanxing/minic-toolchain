#include "target/common/machine_core_coverage.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CoreMachineDemand {
    bool recognized;
    bool requires_origin;
    unsigned int required_effects;
} CoreMachineDemand;

static CoreMachineDemand core_machine_demand(
    const MinicCoreInstruction *instruction) {
    const CoreMachineDemand pure = {true, false, 0U};
    const CoreMachineDemand origin = {true, true, 0U};
    const CoreMachineDemand read = {true, true,
        MINIC_MACHINE_EFFECT_MEMORY_READ};
    const CoreMachineDemand write = {true, true,
        MINIC_MACHINE_EFFECT_MEMORY_WRITE};
    const CoreMachineDemand read_write = {true, true,
        MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE};
    if (instruction == NULL) return (CoreMachineDemand){0};
    switch (instruction->kind) {
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
        /* Core represents a validated extended-float numerical intrinsic
         * using CALL's operand/signature carrier. Physical x87 or target
         * native arithmetic is not a machine CALL. Demand provenance so
         * the operation cannot disappear, but never fabricate CALL effects
         * for instructions that do not perform a branch or runtime call.
         * Plain calls and all indirect calls still require real CALL. */
        if (instruction->value.call.extended_float_operation !=
            MINIC_CORE_EXT_FLOAT_NONE)
            return origin;
        return (CoreMachineDemand){true, true, MINIC_MACHINE_EFFECT_CALL};
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
    size_t i;
    if (instruction->kind == MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM)
        source_id = instruction->value.inline_asm_id;
    else if (instruction->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM)
        source_id = instruction->value.structured_inline_asm.inline_asm_id;
    else return false;
    if (source_id >= core->inline_asm_count || core->inline_asms == NULL ||
        machine_instruction->source_asm_template == NULL) return false;
    source = &core->inline_asms[source_id];
    /* A generic source-only scalar input is a physical ASM operand even if
     * the template never prints %N (e.g. asm volatile("nop" : : eight r's)).
     * Do not permit a selector to erase input bindings while retaining the
     * source-text/effect seal. All architectures share this Core invariant;
     * each target still validates exact constraint/register legality. */
    if (instruction->kind == MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM &&
        source->operand_plan_owner == MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED) {
        const size_t count = instruction->value.structured_inline_asm.operand_count;
        bool all_scalar_inputs = count != 0U;
        if (count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT)
            return false;
        for (i = 0U; i < count && all_scalar_inputs; ++i) {
            const MinicCoreStructuredInlineAsmOperand *binding =
                &instruction->value.structured_inline_asm.operands[i];
            if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT ||
                binding->operand_index != i) all_scalar_inputs = false;
        }
        if (all_scalar_inputs) {
            if (machine_instruction->operand_count != count ||
                machine_instruction->operands == NULL) return false;
            for (i = 0U; i < count; ++i) {
                const MinicMachineOperand *operand =
                    &machine_instruction->operands[i];
                if (operand->asm_constraint == NULL ||
                    operand->asm_constraint[0] == '\0' ||
                    (strcmp(operand->asm_constraint, "r") == 0 &&
                     operand->kind != MINIC_MACHINE_OPERAND_REGISTER) ||
                    (operand->kind == MINIC_MACHINE_OPERAND_REGISTER &&
                     (operand->value.physical.access &
                         MINIC_MACHINE_REGISTER_READ) == 0U)) return false;
            }
        }
    }
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

/* A target-qualified source ASM may be fully selected into real physical
 * opcodes. Exactly one effect-bearing SELECTED instruction seals the Core
 * occurrence; the remaining 1:N transport instructions retain ordinary
 * Core origin. Source text is never copied into an opcode. Target-specific
 * MachineVerifier independently proves opcode/form/operands/effects, while
 * this gate proves the identity and source-mandated observable effects. */
static bool machine_selected_asm_matches_core(
    const MinicCoreFunction *core, const MinicCoreInstruction *instruction,
    const MinicMachineFunction *machine,
    const MinicMachineInstruction *selected,
    const MinicMachineCoreCoveragePolicy *policy) {
    const MinicCoreInlineAsm *source;
    MinicCoreInlineAsmId id;
    if (instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        !selected->has_selected_source_asm_seal ||
        selected->kind != MINIC_MACHINE_INSTRUCTION_SELECTED ||
        selected->source_asm_template != NULL ||
        selected->source_asm_length != 0U)
        return false;
    id = instruction->value.structured_inline_asm.inline_asm_id;
    if (id >= core->inline_asm_count || core->inline_asms == NULL)
        return false;
    source = &core->inline_asms[id];
    return policy != NULL && policy->prove_selected_source_asm != NULL &&
        source->template_text != NULL &&
        source->operand_plan_owner == MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED &&
        source->target_machine_form_id != 0U &&
        !source->is_goto &&
        /* Clobber and immediate role mappings are target-dependent. The
         * mandatory independent proof callback receives the full Core
         * descriptor and must verify them against physical registers and
         * relocations instead of Common guessing an ISA contract. */
        source->deferred_immediate_symbol == NULL &&
        instruction->value.structured_inline_asm.operand_count <=
            MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT &&
        source->target_machine_form_id ==
            selected->source_asm_machine_form_id &&
        source->target_machine_form_variant ==
            selected->source_asm_machine_form_variant &&
        selected->has_source_asm_unique_id ==
            (source->source_asm_unique_id != 0U) &&
        source->source_asm_unique_id == selected->source_asm_unique_id &&
        /* Register-only compiler-selected ASM (e.g. A64 rbit) cannot carry
         * the memory-only VOLATILE flag in physical opcode contracts. Its
         * mandatory source seal itself is an observable scheduling effect.
         * Actual memory operations must retain hardware volatility. */
        (!source->is_volatile ||
         (selected->effects & (MINIC_MACHINE_EFFECT_MEMORY_READ |
                               MINIC_MACHINE_EFFECT_MEMORY_WRITE)) == 0U ||
         (selected->effects & MINIC_MACHINE_EFFECT_VOLATILE) != 0U) &&
        (!source->has_memory_clobber ||
         (selected->effects & MINIC_MACHINE_EFFECT_COMPILER_BARRIER) != 0U) &&
        policy->prove_selected_source_asm(core, instruction, source, machine,
            selected, policy->context);
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
        if (!core_expected_successors(core, reachable, core_id, expected)) {
            if (getenv("MINIC_RV_MACHINE_TRACE") != NULL)
                (void)fprintf(stderr,
                    "MACHINE_CORE_COVERAGE_EDGE_REJECT function=%s "
                    "core_block=%zu stage=core_expected\n",
                    core->name, core_id);
            return false;
        }
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
                if (to >= machine->block_count || to == 0U) {
                    if (getenv("MINIC_RV_MACHINE_TRACE") != NULL)
                        (void)fprintf(stderr,
                            "MACHINE_CORE_COVERAGE_EDGE_REJECT function=%s "
                            "core_block=%zu stage=invalid_machine_edge "
                            "machine_from=%zu machine_to=%zu\n",
                            core->name, core_id, queue[head - 1U], to);
                    return false;
                }
                next = &machine->blocks[to];
                if (next->has_core_block_origin) {
                    if (next->core_block_id >= core->block_count) {
                        if (getenv("MINIC_RV_MACHINE_TRACE") != NULL)
                            (void)fprintf(stderr,
                                "MACHINE_CORE_COVERAGE_EDGE_REJECT function=%s "
                                "core_block=%zu stage=invalid_core_owner "
                                "machine_to=%zu\n",
                                core->name, core_id, to);
                        return false;
                    }
                    actual[next->core_block_id] = 1U;
                } else if (visit_stamps[to] != core_id + 1U) {
                    visit_stamps[to] = core_id + 1U;
                    queue[tail++] = to;
                }
            }
        }
        if (memcmp(expected, actual,
                   core->block_count * sizeof(*expected)) != 0) {
            if (getenv("MINIC_RV_MACHINE_TRACE") != NULL)
                for (size_t dest = 0U; dest < core->block_count; ++dest)
                    if (expected[dest] != actual[dest]) {
                        (void)fprintf(stderr,
                            "MACHINE_CORE_COVERAGE_EDGE_REJECT function=%s "
                            "core_block=%zu stage=successor_mismatch "
                            "core_dest=%zu expected=%u actual=%u\n",
                            core->name, core_id, dest,
                            (unsigned)expected[dest], (unsigned)actual[dest]);
                        break;
                    }
            return false;
        }
    }
    return true;
}

bool minic_machine_core_coverage_verify_with_policy(
    const MinicCoreFunction *core, const bool *reachable_blocks,
    const MinicMachineFunction *machine,
    const MinicMachineCoreCoveragePolicy *policy) {
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
    const char *failure_stage = "input";
    size_t failure_core_block = SIZE_MAX;
    size_t failure_core_instruction = SIZE_MAX;
    size_t failure_machine_block = SIZE_MAX;
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
    failure_stage = "core_instruction_ownership";
    for (i = 0U; i < core->instruction_count; ++i) owner[i] = SIZE_MAX;
    for (i = 0U; i < core->block_count; ++i) primary[i] = SIZE_MAX;
    for (i = 0U; i < core->block_count; ++i) {
        const MinicCoreBlock *block = &core->blocks[i];
        size_t j;
        if (block->instruction_count != 0U && block->instructions == NULL)
            goto cleanup;
        failure_core_block = i;
        for (j = 0U; j < block->instruction_count; ++j) {
            const size_t id = block->instructions[j];
            failure_core_instruction = id;
            if (id >= core->instruction_count || owner[id] != SIZE_MAX ||
                !core_machine_demand(&core->instructions[id]).recognized)
                goto cleanup;
            owner[id] = i;
        }
    }
    failure_stage = "machine_instruction_identity_effects";
    failure_core_block = SIZE_MAX;
    failure_core_instruction = SIZE_MAX;
    for (i = 0U; i < machine->block_count; ++i) {
        const MinicMachineBlock *block = &machine->blocks[i];
        size_t j;
        failure_machine_block = i;
        if ((block->instruction_count != 0U && block->instructions == NULL) ||
            (block->successor_count != 0U && block->successors == NULL) ||
            (block->has_fallthrough &&
             block->fallthrough_target >= machine->block_count))
            goto cleanup;
        if (block->has_core_block_origin) {
            const size_t id = block->core_block_id;
            failure_core_block = id;
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
            failure_core_instruction = id;
            failure_core_block = ins->core_block_id;
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
                        core, &core->instructions[id], ins) ||
                    seen_source_asm[id]) goto cleanup;
                seen_source_asm[id] = 1U;
            }
            if (ins->has_selected_source_asm_seal) {
                if (seen_source_asm[id] ||
                    !machine_selected_asm_matches_core(
                        core, &core->instructions[id], machine, ins,
                        policy)) goto cleanup;
                seen_source_asm[id] = 1U;
            }
            if (ins->kind == MINIC_MACHINE_INSTRUCTION_COMPILER_BARRIER &&
                core->instructions[id].kind !=
                    MINIC_CORE_INSTRUCTION_COMPILER_BARRIER) goto cleanup;
            seen[id] = 1U;
            effects[id] |= ins->effects;
            if (ins->has_selected_source_asm_seal)
                effects[id] |= MINIC_MACHINE_EFFECT_SOURCE_ASM;
        }
    }
    /* The native function entry is ABI code, never a Core entry alias.  In
     * particular, a backedge to the first Core block must bypass prologue. */
    failure_stage = "machine_entry_not_core_alias";
    failure_core_block = SIZE_MAX;
    failure_core_instruction = SIZE_MAX;
    failure_machine_block = 0U;
    if (!machine->blocks[0].machine_auxiliary ||
        machine->blocks[0].has_core_block_origin) goto cleanup;
    failure_stage = "missing_primary_core_block";
    for (i = 0U; i < core->block_count; ++i)
        if (reachable_blocks[i] && primary[i] == SIZE_MAX) {
            failure_core_block = i;
            goto cleanup;
        }
    failure_stage = "entry_auxiliary_closure";
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
    /* The Core reachability contract has more than one control-flow root:
     * entry_block, assembler-visible local labels, indirect entries, and
     * address-taken blocks.  An indirect/local entry is a genuine physical
     * entry point, not a fabricated edge from the ABI prologue.  Walk the
     * native entry first, then seed only independently reachable Core roots.
     * Ordinary Core successors must still have real Machine CFG edges. */
    failure_stage = "physical_machine_reachability";
    memset(visited, 0, machine->block_count * sizeof(*visited));
    /* Reuse the destination bitmap before exact-edge comparison, where it
     * is cleared for every Core block.  Scan reachable Core instructions,
     * including address values the target legitimately folds away. */
    for (i = 0U; i < core->instruction_count; ++i)
        if (owner[i] != SIZE_MAX && reachable_blocks[owner[i]] &&
            core->instructions[i].kind ==
                MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS &&
            core->instructions[i].value.block_id < core->block_count)
            expected[core->instructions[i].value.block_id] = 1U;
    {
        size_t head = 0U, tail = 1U;
        queue[0] = 0U;
        visited[0] = 1U;
        for (i = 0U; ; ++i) {
            while (head < tail) {
                const MinicMachineBlock *block =
                    &machine->blocks[queue[head++]];
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
            if (i == core->block_count) break;
            if (!reachable_blocks[i] || visited[primary[i]]) continue;
            /* BLOCK_ADDRESS is itself a Core escape root.  Merely
             * visiting an arbitrary unreachable Machine block cannot
             * establish a new root. */
            if (!core->blocks[i].indirect_entry &&
                core->blocks[i].local_label_symbol == NULL &&
                !expected[i]) continue;
            if (getenv("MINIC_RV_MACHINE_TRACE") != NULL)
                (void)fprintf(stderr,
                    "MACHINE_CORE_COVERAGE_EXTERNAL_ROOT function=%s "
                    "core_block=%zu machine_block=%zu indirect=%u "
                    "local_label=%u address_taken=%u\n",
                    core->name != NULL ? core->name : "<unknown>",
                    i, primary[i], (unsigned)core->blocks[i].indirect_entry,
                    (unsigned)(core->blocks[i].local_label_symbol != NULL),
                    (unsigned)(expected[i] != 0U));
            visited[primary[i]] = 1U;
            queue[tail++] = primary[i];
        }
    }
    for (i = 0U; i < core->block_count; ++i)
        if (reachable_blocks[i] && !visited[primary[i]]) {
            failure_core_block = i;
            failure_machine_block = primary[i];
            goto cleanup;
        }
    for (i = 0U; i < machine->block_count; ++i) {
        const MinicMachineBlock *block = &machine->blocks[i];
        size_t j;
        if (visited[i]) continue;
        /* An unreachable physical island cannot prove a live Core effect. */
        for (j = 0U; j < block->instruction_count; ++j)
            if (block->instructions[j].has_core_origin) {
                failure_machine_block = i;
                failure_core_block = block->instructions[j].core_block_id;
                failure_core_instruction =
                    block->instructions[j].core_instruction_id;
                goto cleanup;
            }
    }
    failure_stage = "exact_core_cfg_edges";
    if (!machine_core_edges_match(core, reachable_blocks, machine, primary,
                                  queue, visit_stamps, expected, actual))
        goto cleanup;
    failure_stage = "observable_core_effects";
    for (i = 0U; i < core->instruction_count; ++i) {
        CoreMachineDemand demand;
        if (owner[i] == SIZE_MAX || !reachable_blocks[owner[i]]) continue;
        failure_core_instruction = i;
        failure_core_block = owner[i];
        demand = core_machine_demand(&core->instructions[i]);
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
    if (!valid && getenv("MINIC_RV_MACHINE_TRACE") != NULL)
        (void)fprintf(stderr,
            "MACHINE_CORE_COVERAGE_REJECT function=%s stage=%s "
            "core_block=%zu core_instruction=%zu machine_block=%zu\n",
            core != NULL && core->name != NULL ? core->name : "<unknown>",
            failure_stage, failure_core_block, failure_core_instruction,
            failure_machine_block);
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

bool minic_machine_core_coverage_verify(const MinicCoreFunction *core,
    const bool *reachable_blocks, const MinicMachineFunction *machine) {
    return minic_machine_core_coverage_verify_with_policy(
        core, reachable_blocks, machine, NULL);
}
