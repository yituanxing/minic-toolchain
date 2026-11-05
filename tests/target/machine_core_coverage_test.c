#include "target/common/machine_core_coverage.h"

#include <stdio.h>

static int check(void) {
    MinicCoreInstruction core_instructions[6] = {0};
    MinicCoreInstructionId core_ids[5] = {0U, 1U, 2U, 3U, 4U};
    MinicCoreInstructionId unreachable_id = 5U;
    MinicCoreBlock core_blocks[2] = {0};
    MinicCoreInlineAsm source_asm = {0};
    MinicCoreFunction core = {0};
    MinicMachineInstruction machine_instructions[5] = {0};
    MinicMachineBlock machine_blocks[3] = {0};
    MinicMachineFunction machine = {0};
    bool reachable[2] = {true, false};
    core_instructions[0].kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    core_instructions[1].kind = MINIC_CORE_INSTRUCTION_STORE;
    core_instructions[1].value.store.is_volatile = true;
    core_instructions[2].kind = MINIC_CORE_INSTRUCTION_CALL;
    core_instructions[3].kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    core_instructions[3].value.structured_inline_asm.inline_asm_id = 0U;
    core_instructions[4].kind = MINIC_CORE_INSTRUCTION_LOAD;
    core_instructions[4].value.load.is_volatile = true;
    core_instructions[5].kind = MINIC_CORE_INSTRUCTION_STORE;
    source_asm.template_text = "nop";
    source_asm.template_length = 3U;
    core_blocks[0].instructions = core_ids;
    core_blocks[0].instruction_count = 5U;
    core_blocks[0].has_terminator = true;
    core_blocks[0].terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core_blocks[1].instructions = &unreachable_id;
    core_blocks[1].instruction_count = 1U;
    core_blocks[1].has_terminator = true;
    core_blocks[1].terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core.blocks = core_blocks;
    core.block_count = 2U;
    core.instructions = core_instructions;
    core.instruction_count = 6U;
    core.inline_asms = &source_asm;
    core.inline_asm_count = 1U;
    machine_instructions[0] = (MinicMachineInstruction){
        .kind = MINIC_MACHINE_INSTRUCTION_SELECTED,
        .has_core_origin = true, .core_block_id = 0U,
        .core_instruction_id = 1U,
        .effects = MINIC_MACHINE_EFFECT_MEMORY_WRITE |
                   MINIC_MACHINE_EFFECT_VOLATILE
    };
    machine_instructions[1] = (MinicMachineInstruction){
        .kind = MINIC_MACHINE_INSTRUCTION_SELECTED,
        .has_core_origin = true, .core_block_id = 0U,
        .core_instruction_id = 2U,
        .effects = MINIC_MACHINE_EFFECT_CALL
    };
    machine_instructions[2] = (MinicMachineInstruction){
        .kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM,
        .has_core_origin = true, .core_block_id = 0U,
        .core_instruction_id = 3U,
        .effects = MINIC_MACHINE_EFFECT_SOURCE_ASM,
        .source_asm_template = "nop", .source_asm_length = 3U
    };
    machine_instructions[3] = (MinicMachineInstruction){
        .kind = MINIC_MACHINE_INSTRUCTION_SELECTED,
        .has_core_origin = true, .core_block_id = 0U,
        .core_instruction_id = 4U,
        .effects = MINIC_MACHINE_EFFECT_MEMORY_READ |
                   MINIC_MACHINE_EFFECT_VOLATILE
    };
    machine_blocks[0].machine_auxiliary = true;
    machine_blocks[0].has_fallthrough = true;
    machine_blocks[0].fallthrough_target = 1U;
    machine_blocks[1].has_core_block_origin = true;
    machine_blocks[1].core_block_id = 0U;
    machine_blocks[1].instructions = machine_instructions;
    machine_blocks[1].instruction_count = 4U;
    machine.blocks = machine_blocks;
    machine.block_count = 2U;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine)) return 1;
    /* Pure SSA constants may be folded into an immediate. */
    machine_blocks[1].instruction_count = 3U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 2;
    machine_blocks[1].instruction_count = 4U;
    machine_instructions[0].effects = MINIC_MACHINE_EFFECT_MEMORY_READ |
                                      MINIC_MACHINE_EFFECT_VOLATILE;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 3;
    machine_instructions[0].effects = MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 4;
    machine_instructions[0].effects = MINIC_MACHINE_EFFECT_MEMORY_WRITE |
                                      MINIC_MACHINE_EFFECT_VOLATILE;
    machine_instructions[2].kind = MINIC_MACHINE_INSTRUCTION_SELECTED;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 5;
    machine_instructions[2].kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    machine_instructions[2].source_asm_template = "wfe";
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 9;
    machine_instructions[2].source_asm_template = "nop";
    source_asm.source_asm_unique_id = 42U;
    machine_instructions[2].has_source_asm_unique_id = true;
    machine_instructions[2].source_asm_unique_id = 42U;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 13;
    machine_instructions[2].source_asm_unique_id = 41U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 14;
    source_asm.source_asm_unique_id = 0U;
    machine_instructions[2].has_source_asm_unique_id = false;
    machine_instructions[2].source_asm_unique_id = 0U;
    source_asm.is_volatile = true;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 15;
    machine_instructions[2].effects |= MINIC_MACHINE_EFFECT_VOLATILE;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 16;
    source_asm.has_memory_clobber = true;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 17;
    machine_instructions[2].effects |= MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 18;
    source_asm.is_volatile = false;
    source_asm.has_memory_clobber = false;
    machine_instructions[2].effects = MINIC_MACHINE_EFFECT_SOURCE_ASM;
    machine_instructions[1].core_block_id = 1U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 6;
    machine_instructions[1].core_block_id = 0U;
    machine_instructions[1].core_instruction_id = 0U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 7;
    machine_instructions[1].core_instruction_id = 2U;
    /* A Core effect may select more than one physical instruction. */
    machine_instructions[4] = machine_instructions[0];
    machine_blocks[1].instruction_count = 5U;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine)) return 8;
    /* An unreachable helper must not impersonate a live Core LOAD. */
    machine_blocks[1].instruction_count = 3U;
    machine_blocks[2].machine_auxiliary = true;
    machine_blocks[2].instructions = &machine_instructions[3];
    machine_blocks[2].instruction_count = 1U;
    machine.block_count = 3U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 19;
    machine.block_count = 2U;
    machine_blocks[1].instruction_count = 5U;
    /* Core entry is an ID, not necessarily block zero; prologue must reach
     * the actual entry before entering any other Core-owned machine block. */
    machine_blocks[1].instruction_count = 0U;
    core_blocks[1].instruction_count = 0U;
    core.entry_block = 1U;
    reachable[0] = false;
    reachable[1] = true;
    machine_blocks[1].core_block_id = 1U;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 10;
    machine_blocks[1].core_block_id = 0U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 11;
    machine_blocks[1].core_block_id = 1U;
    machine_blocks[1].has_core_block_origin = false;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 12;
    return 0;
}

static int check_cfg_edges(void) {
    MinicCoreInstruction ins = {0};
    MinicCoreInstructionId instruction_id = 0U;
    MinicCoreInlineAsm source = {0};
    MinicCoreBlock core_blocks[2] = {0};
    MinicCoreFunction core = {0};
    MinicMachineInstruction machine_ins = {0};
    MinicMachineBlock machine_blocks[4] = {0};
    MinicMachineFunction machine = {0};
    size_t from_entry = 1U;
    size_t through_aux = 3U;
    size_t from_aux[] = {2U, 3U};
    size_t with_spurious[] = {3U, 1U};
    bool reachable[] = {true, true};
    core_blocks[0].has_terminator = true;
    core_blocks[0].terminator.kind = MINIC_CORE_TERMINATOR_BRANCH;
    core_blocks[0].terminator.branch_target = 1U;
    core_blocks[1].has_terminator = true;
    core_blocks[1].terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core.blocks = core_blocks;
    core.block_count = 2U;
    machine_blocks[0].machine_auxiliary = true;
    machine_blocks[0].successors = &from_entry;
    machine_blocks[0].successor_count = 1U;
    machine_blocks[1].has_core_block_origin = true;
    machine_blocks[1].core_block_id = 0U;
    machine_blocks[1].successors = &through_aux;
    machine_blocks[1].successor_count = 1U;
    machine_blocks[2].has_core_block_origin = true;
    machine_blocks[2].core_block_id = 1U;
    machine_blocks[3].machine_auxiliary = true;
    machine_blocks[3].successors = from_aux;
    machine_blocks[3].successor_count = 2U;
    machine.blocks = machine_blocks;
    machine.block_count = 4U;
    /* The physical branch island contains a loop, but reaches only Core 1. */
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 1;
    core_blocks[0].terminator.branch_target = 0U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 2;
    core_blocks[0].terminator.branch_target = 1U;
    machine_blocks[1].successors = with_spurious;
    machine_blocks[1].successor_count = 2U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 3;
    machine_blocks[1].successors = &through_aux;
    machine_blocks[1].successor_count = 1U;
    core_blocks[0].terminator.kind = MINIC_CORE_TERMINATOR_INDIRECT_BRANCH;
    core_blocks[1].indirect_entry = true;
    machine_blocks[2].indirect_entry = true;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 4;
    core_blocks[1].indirect_entry = false;
    machine_blocks[2].indirect_entry = false;
    /* Unlike an indirect terminator, asm-goto is a mid-block Core effect:
     * its label edge must coexist with the ordinary RETURN terminator. */
    core_blocks[0].terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core_blocks[0].instructions = &instruction_id;
    core_blocks[0].instruction_count = 1U;
    core.instructions = &ins;
    core.instruction_count = 1U;
    ins.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    ins.value.structured_inline_asm.inline_asm_id = 0U;
    source.template_text = "nop";
    source.template_length = 3U;
    source.is_goto = true;
    source.goto_target = 1U;
    source.goto_second_target = MINIC_CORE_BLOCK_INVALID;
    core.inline_asms = &source;
    core.inline_asm_count = 1U;
    machine_ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    machine_ins.has_core_origin = true;
    machine_ins.core_instruction_id = 0U;
    machine_ins.core_block_id = 0U;
    machine_ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM;
    machine_ins.source_asm_template = "nop";
    machine_ins.source_asm_length = 3U;
    machine_blocks[1].instructions = &machine_ins;
    machine_blocks[1].instruction_count = 1U;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 5;
    source.goto_target = 0U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 6;
    return 0;
}

int main(void) {
    int error = check();
    if (error != 0) {
        fprintf(stderr, "MACHINE_CORE_COVERAGE=FAIL case=%d\n", error);
        return 1;
    }
    error = check_cfg_edges();
    if (error != 0) {
        fprintf(stderr, "MACHINE_CORE_COVERAGE=FAIL cfg_case=%d\n", error);
        return 1;
    }
    puts("MACHINE_CORE_COVERAGE=PASS effect_origin=1 no_dropped_core=1 cfg_edges=1 asm_goto=1 unreachable_effects=1");
    return 0;
}
