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
    /* A verified typed extended-float numerical intrinsic reuses Core CALL's
     * argument carrier, but may lower directly to x87/target arithmetic.
     * It must keep its Core origin without inventing a physical CALL effect.
     * An ordinary CALL must still have the real machine CALL effect. */
    core_instructions[2].value.call.extended_float_operation =
        MINIC_CORE_EXT_FLOAT_ADD;
    machine_instructions[1].effects = 0U;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 20;
    machine_instructions[1].has_core_origin = false;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 21;
    machine_instructions[1].has_core_origin = true;
    core_instructions[2].value.call.extended_float_operation =
        MINIC_CORE_EXT_FLOAT_NONE;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 22;
    machine_instructions[1].effects = MINIC_MACHINE_EFFECT_CALL;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 23;
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

/* C local labels, computed-goto entries and escaped block addresses are
 * independently reachable from outside the ABI entry path.  They are real
 * Machine roots, never invented CFG branches from the function prologue.
 * Their ordinary outgoing Core edges must nevertheless survive exactly. */
static int check_independent_core_roots(void) {
    MinicCoreInstruction address = {0};
    MinicCoreInstructionId address_id = 0U;
    MinicCoreBlock core_blocks[3] = {0};
    MinicCoreFunction core = {0};
    MinicMachineBlock machine_blocks[4] = {0};
    MinicMachineFunction machine = {0};
    size_t prologue_target = 1U;
    size_t secondary_target = 3U;
    bool reachable[3] = {true, true, true};

    core_blocks[0].has_terminator = true;
    core_blocks[0].terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core_blocks[1].has_terminator = true;
    core_blocks[1].terminator.kind = MINIC_CORE_TERMINATOR_BRANCH;
    core_blocks[1].terminator.branch_target = 2U;
    core_blocks[1].indirect_entry = true;
    core_blocks[2].has_terminator = true;
    core_blocks[2].terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core.blocks = core_blocks;
    core.block_count = 3U;
    core.name = "independent_core_roots";

    machine_blocks[0].machine_auxiliary = true;
    machine_blocks[0].successors = &prologue_target;
    machine_blocks[0].successor_count = 1U;
    machine_blocks[1].has_core_block_origin = true;
    machine_blocks[1].core_block_id = 0U;
    machine_blocks[2].has_core_block_origin = true;
    machine_blocks[2].core_block_id = 1U;
    machine_blocks[2].indirect_entry = true;
    machine_blocks[2].successors = &secondary_target;
    machine_blocks[2].successor_count = 1U;
    machine_blocks[3].has_core_block_origin = true;
    machine_blocks[3].core_block_id = 2U;
    machine.blocks = machine_blocks;
    machine.block_count = 4U;

    /* A reachable cloned-label root has no fabricated prologue edge. */
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 1;
    /* Starting at that root still requires the exact outgoing branch. */
    machine_blocks[2].successor_count = 0U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 2;
    machine_blocks[2].successor_count = 1U;
    /* External assembler-local symbols are independent roots too, even
     * when the Core block is not itself an indirect-branch target. */
    core_blocks[1].indirect_entry = false;
    core_blocks[1].local_label_symbol = ".Lexternal_entry";
    machine_blocks[2].indirect_entry = false;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 3;
    core_blocks[1].local_label_symbol = NULL;
    /* An address-taken block can escape independently of explicit branches
     * and of whether target code folds the address value into a constant. */
    address.kind = MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS;
    address.value.block_id = 1U;
    core_blocks[0].instructions = &address_id;
    core_blocks[0].instruction_count = 1U;
    core.instructions = &address;
    core.instruction_count = 1U;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 4;
    /* Ordinary disconnected blocks without source-reachable roots are
     * forbidden to claim either live effects or a missing edge. */
    core_blocks[0].instruction_count = 0U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 5;
    return 0;
}

static int check_compiler_barrier_origin(void) {
    MinicCoreInstruction core_instruction = {0};
    MinicCoreInstructionId instruction_id = 0U;
    MinicCoreBlock core_block = {0};
    MinicCoreFunction core = {0};
    MinicMachineInstruction machine_instruction = {0};
    MinicMachineBlock machine_blocks[2] = {0};
    MinicMachineFunction machine = {0};
    bool reachable[] = {true};
    core_instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
    core_block.instructions = &instruction_id;
    core_block.instruction_count = 1U;
    core_block.has_terminator = true;
    core_block.terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core.blocks = &core_block;
    core.block_count = 1U;
    core.instructions = &core_instruction;
    core.instruction_count = 1U;
    machine_instruction.kind = MINIC_MACHINE_INSTRUCTION_COMPILER_BARRIER;
    machine_instruction.has_core_origin = true;
    machine_instruction.effects = MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    machine_blocks[0].machine_auxiliary = true;
    machine_blocks[0].has_fallthrough = true;
    machine_blocks[0].fallthrough_target = 1U;
    machine_blocks[1].has_core_block_origin = true;
    machine_blocks[1].instructions = &machine_instruction;
    machine_blocks[1].instruction_count = 1U;
    machine.blocks = machine_blocks;
    machine.block_count = 2U;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 1;
    core_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 2;
    core_instruction.kind = MINIC_CORE_INSTRUCTION_COMPILER_BARRIER;
    machine_instruction.effects = 0U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine))
        return 3;
    return 0;
}

static int check_structured_scalar_asm_bindings(void) {
    MinicCoreInstruction core_instruction = {0};
    MinicCoreInstructionId core_id = 0U;
    MinicCoreBlock core_block = {0};
    MinicCoreInlineAsm source = {0};
    MinicCoreFunction core = {0};
    MinicMachineOperand bound[8] = {0};
    MinicMachineInstruction selected = {0};
    MinicMachineBlock blocks[2] = {0};
    MinicMachineFunction machine = {0};
    bool reachable[] = {true};
    size_t i;
    core_instruction.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    core_instruction.value.structured_inline_asm.operand_count = 8U;
    for (i = 0U; i < 8U; ++i) {
        MinicCoreStructuredInlineAsmOperand *operand =
            &core_instruction.value.structured_inline_asm.operands[i];
        operand->kind = MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        operand->operand_index = i;
        operand->value = (MinicCoreValueId)i;
        bound[i].kind = MINIC_MACHINE_OPERAND_REGISTER;
        bound[i].value.physical.reg.number = (unsigned)(10U + i);
        bound[i].value.physical.reg.width_bits = 32U;
        bound[i].value.physical.reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
        bound[i].value.physical.access = MINIC_MACHINE_REGISTER_READ;
        bound[i].asm_constraint = "r";
    }
    source.template_text = "nop";
    source.template_length = 3U;
    source.is_volatile = true;
    source.has_memory_clobber = true;
    source.operand_plan_owner = MINIC_ASM_OPERAND_PLAN_GENERIC_STRUCTURED;
    source.source_asm_unique_id = 101U;
    core_block.instructions = &core_id;
    core_block.instruction_count = 1U;
    core_block.has_terminator = true;
    core_block.terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    core.blocks = &core_block;
    core.block_count = 1U;
    core.instructions = &core_instruction;
    core.instruction_count = 1U;
    core.inline_asms = &source;
    core.inline_asm_count = 1U;
    selected.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    selected.has_core_origin = true;
    selected.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
        MINIC_MACHINE_EFFECT_VOLATILE |
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    selected.operands = bound;
    selected.operand_count = 8U;
    selected.source_asm_template = "nop";
    selected.source_asm_length = 3U;
    selected.has_source_asm_unique_id = true;
    selected.source_asm_unique_id = 101U;
    blocks[0].machine_auxiliary = true;
    blocks[0].has_fallthrough = true;
    blocks[0].fallthrough_target = 1U;
    blocks[1].has_core_block_origin = true;
    blocks[1].instructions = &selected;
    blocks[1].instruction_count = 1U;
    machine.blocks = blocks;
    machine.block_count = 2U;
    if (!minic_machine_core_coverage_verify(&core, reachable, &machine)) return 1;
    selected.operand_count = 7U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 2;
    selected.operand_count = 8U;
    bound[7].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 3;
    bound[7].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    bound[7].kind = MINIC_MACHINE_OPERAND_INTEGER;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 7;
    bound[7].kind = MINIC_MACHINE_OPERAND_REGISTER;
    selected.effects &= ~(unsigned)MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 4;
    selected.effects |= MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    selected.source_asm_unique_id = 100U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 5;
    selected.source_asm_unique_id = 101U;
    core_instruction.value.structured_inline_asm.operand_count =
        MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT + 1U;
    if (minic_machine_core_coverage_verify(&core, reachable, &machine)) return 8;
    core_instruction.value.structured_inline_asm.operand_count = 8U;
    return minic_machine_core_coverage_verify(&core, reachable, &machine) ? 0 : 6;
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
    error = check_independent_core_roots();
    if (error != 0) {
        fprintf(stderr, "MACHINE_CORE_COVERAGE=FAIL root_case=%d\n", error);
        return 1;
    }
    error = check_compiler_barrier_origin();
    if (error != 0) {
        fprintf(stderr, "MACHINE_CORE_COVERAGE=FAIL barrier_case=%d\n", error);
        return 1;
    }
    error = check_structured_scalar_asm_bindings();
    if (error != 0) {
        fprintf(stderr, "MACHINE_CORE_COVERAGE=FAIL scalar_asm_case=%d\n", error);
        return 1;
    }
    puts("MACHINE_CORE_COVERAGE=PASS effect_origin=1 no_dropped_core=1 cfg_edges=1 asm_goto=1 unreachable_effects=1 compiler_barrier=1 scalar_asm_bindings=8");
    return 0;
}
