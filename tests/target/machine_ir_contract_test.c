/* Real owned Machine IR contract: independent of the current direct-text
 * backends. All five target identities exercise one fail-closed verifier. */
#include "target/common/machine_ir.h"
#include "target/common/machine_ir_verify.h"

#include <stdio.h>
#include <string.h>

enum ProbeOpcode {
    PROBE_LOAD_FRAME = 1,
    PROBE_CALL,
    PROBE_JUMP,
    PROBE_RETURN,
    PROBE_LOAD_ADDRESS,
    PROBE_INDIRECT_JUMP
};

typedef struct ProbePolicyState {
    bool source_asm_allowed;
} ProbePolicyState;

static MinicMachineRegister gp(unsigned int number, unsigned int width) {
    MinicMachineRegister result = {0};
    result.bank = MINIC_MACHINE_REGISTER_GENERAL;
    result.number = number;
    result.width_bits = (uint16_t)width;
    return result;
}

static bool probe_lookup(const MinicTargetInfo *target, uint32_t opcode,
    MinicMachineOpcodeContract *result, void *context) {
    const unsigned int width =
        target->architecture == MINIC_TARGET_ARCH_RISCV32 ? 32U : 64U;
    (void)context;
    memset(result, 0, sizeof(*result));
    switch (opcode) {
    case PROBE_LOAD_FRAME:
        result->operand_count = 2U;
        result->operand_kinds[0] = MINIC_MACHINE_OPERAND_REGISTER;
        result->register_access[0] = MINIC_MACHINE_REGISTER_WRITE;
        result->operand_kinds[1] = MINIC_MACHINE_OPERAND_FRAME;
        result->required_effects = MINIC_MACHINE_EFFECT_MEMORY_READ;
        result->permitted_effects = MINIC_MACHINE_EFFECT_MEMORY_READ |
                                    MINIC_MACHINE_EFFECT_VOLATILE;
        return true;
    case PROBE_CALL:
        result->operand_count = 1U;
        result->operand_kinds[0] = MINIC_MACHINE_OPERAND_SYMBOL;
        result->required_generated_features = UINT64_C(1);
        result->required_effects = MINIC_MACHINE_EFFECT_CALL;
        result->permitted_effects = MINIC_MACHINE_EFFECT_CALL |
                                    MINIC_MACHINE_EFFECT_MEMORY_READ |
                                    MINIC_MACHINE_EFFECT_MEMORY_WRITE;
        result->implicit_count = 1U;
        result->implicit[0].reg = gp(3U, width);
        result->implicit[0].access = MINIC_MACHINE_REGISTER_CLOBBER;
        return true;
    case PROBE_JUMP:
        result->operand_count = 1U;
        result->operand_kinds[0] = MINIC_MACHINE_OPERAND_BLOCK;
        result->required_effects = MINIC_MACHINE_EFFECT_BRANCH;
        result->permitted_effects = MINIC_MACHINE_EFFECT_BRANCH;
        result->terminates_block = true;
        return true;
    case PROBE_RETURN:
        result->required_effects = MINIC_MACHINE_EFFECT_RETURN;
        result->permitted_effects = MINIC_MACHINE_EFFECT_RETURN;
        result->terminates_block = true;
        return true;
    case PROBE_LOAD_ADDRESS:
        result->operand_count = 2U;
        result->operand_kinds[0] = MINIC_MACHINE_OPERAND_REGISTER;
        result->register_access[0] = MINIC_MACHINE_REGISTER_WRITE;
        result->operand_kinds[1] = MINIC_MACHINE_OPERAND_ADDRESS;
        result->required_effects = MINIC_MACHINE_EFFECT_MEMORY_READ;
        result->permitted_effects = MINIC_MACHINE_EFFECT_MEMORY_READ;
        return true;
    case PROBE_INDIRECT_JUMP:
        result->operand_count = 1U;
        result->operand_kinds[0] = MINIC_MACHINE_OPERAND_REGISTER;
        result->register_access[0] = MINIC_MACHINE_REGISTER_READ;
        result->required_effects = MINIC_MACHINE_EFFECT_BRANCH;
        result->permitted_effects = MINIC_MACHINE_EFFECT_BRANCH;
        result->terminates_block = true;
        result->has_indirect_successors = true;
        return true;
    default:
        return false;
    }
}

static bool probe_register(const MinicTargetInfo *target,
    const MinicMachineRegister *reg, void *context) {
    const unsigned int width =
        target->architecture == MINIC_TARGET_ARCH_RISCV32 ? 32U : 64U;
    (void)context;
    return reg->bank == MINIC_MACHINE_REGISTER_GENERAL &&
           reg->width_bits == width && reg->number < 32U;
}

static bool probe_address(const MinicTargetInfo *target,
    const MinicMachineAddress *address, void *context) {
    (void)target;
    (void)context;
    return address->target_mode == 1U && address->has_base &&
           !address->has_segment &&
           (!address->has_index || address->scale == 1U ||
            address->scale == 2U || address->scale == 4U ||
            address->scale == 8U);
}

static bool probe_relocation(const MinicTargetInfo *target,
    const char *symbol, uint32_t relocation, int64_t addend, void *context) {
    (void)target;
    (void)context;
    return strcmp(symbol, "blocked") != 0 &&
           relocation <= 2U && addend >= -2048 && addend <= 2048;
}

static bool probe_asm(const MinicTargetInfo *target,
    const MinicMachineInstruction *ins, void *context) {
    const ProbePolicyState *state = context;
    (void)target;
    return state->source_asm_allowed &&
           (ins->effects & MINIC_MACHINE_EFFECT_COMPILER_BARRIER) != 0U;
}

static bool add_generated(MinicMachineFunction *fn, size_t block,
                          MinicMachineInstruction *ins) {
    ins->kind = MINIC_MACHINE_INSTRUCTION_SELECTED;
    return minic_machine_block_append_instruction(fn, block, ins);
}

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "MACHINE_IR_CONTRACT=FAIL target=%d line=%d: %s\n", \
                (int)arch, __LINE__, #condition); \
        goto fail; \
    } \
} while (0)

static bool exercise_target(MinicTargetArchitecture arch) {
    MinicTargetInfo target = {0};
    MinicMachineFunction fn = {0};
    MinicMachineVerifyPolicy policy = {0};
    ProbePolicyState state = {true};
    MinicMachineInstruction ins = {0};
    MinicMachineFrameObject frame = {0};
    MinicMachineInstruction *load, *call, *jump;
    MinicMachineRegister saved_reg;
    MinicMachineOperandKind saved_kind;
    char *saved_string;
    size_t entry, exit_block, frame_id;
    size_t saved_count;
    uint32_t saved_opcode;
    uint64_t saved_features;
    bool result = false;
    const unsigned int width = arch == MINIC_TARGET_ARCH_RISCV32 ? 32U : 64U;
    char symbol[] = "callee";
    char label[] = ".Lmachine_exit";
    target.architecture = arch;
    target.riscv_isa.strict_isa =
        arch == MINIC_TARGET_ARCH_RISCV32 ||
        arch == MINIC_TARGET_ARCH_RISCV64;
    target.riscv_isa.codegen_features = 1U;
    policy.lookup_opcode = probe_lookup;
    policy.validate_register = probe_register;
    policy.validate_address = probe_address;
    policy.validate_relocation = probe_relocation;
    policy.validate_source_asm = probe_asm;
    policy.context = &state;
    REQUIRE(minic_machine_function_initialize(&fn, &target,
                                              "machine_probe", 1U));
    fn.frame_size = 16U;
    fn.frame_alignment = 16U;
    frame.size_bytes = 8U;
    frame.alignment_bytes = 8U;
    frame.area = MINIC_MACHINE_FRAME_LOCAL;
    frame.has_fixed_offset = true;
    REQUIRE(minic_machine_function_add_frame_object(&fn, &frame, &frame_id));
    REQUIRE(frame_id == 0U);
    REQUIRE(minic_machine_function_add_block(&fn, NULL, false, &entry));
    REQUIRE(minic_machine_function_add_block(&fn, label, false, &exit_block));
    REQUIRE(entry == 0U && exit_block == 1U);
    label[2] = 'X';
    REQUIRE(strcmp(fn.blocks[exit_block].exported_label,
                   ".Lmachine_exit") == 0);
    REQUIRE(minic_machine_block_add_successor(&fn, entry, exit_block));

    memset(&ins, 0, sizeof(ins));
    ins.opcode = PROBE_LOAD_FRAME;
    ins.effects = MINIC_MACHINE_EFFECT_MEMORY_READ;
    ins.operand_count = 2U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ins.operands[0].value.physical.reg = gp(1U, width);
    ins.operands[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    ins.operands[1].kind = MINIC_MACHINE_OPERAND_FRAME;
    ins.operands[1].value.frame.index = frame_id;
    REQUIRE(add_generated(&fn, entry, &ins));

    memset(&ins, 0, sizeof(ins));
    ins.opcode = PROBE_CALL;
    ins.effects = MINIC_MACHINE_EFFECT_CALL;
    ins.operand_count = 1U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_SYMBOL;
    ins.operands[0].value.symbol.name = symbol;
    ins.operands[0].value.symbol.relocation = 1U;
    ins.implicit_count = 1U;
    ins.implicit[0].reg = gp(3U, width);
    ins.implicit[0].access = MINIC_MACHINE_REGISTER_CLOBBER;
    REQUIRE(add_generated(&fn, entry, &ins));
    symbol[0] = 'X';
    REQUIRE(strcmp(fn.blocks[entry].instructions[1].operands[0]
                       .value.symbol.name, "callee") == 0);

    memset(&ins, 0, sizeof(ins));
    ins.opcode = PROBE_JUMP;
    ins.effects = MINIC_MACHINE_EFFECT_BRANCH;
    ins.operand_count = 1U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_BLOCK;
    ins.operands[0].value.block_id = exit_block;
    REQUIRE(add_generated(&fn, entry, &ins));
    memset(&ins, 0, sizeof(ins));
    ins.opcode = PROBE_RETURN;
    ins.effects = MINIC_MACHINE_EFFECT_RETURN;
    REQUIRE(add_generated(&fn, exit_block, &ins));
    REQUIRE(minic_machine_function_verify(&fn, &policy));

    load = &fn.blocks[entry].instructions[0];
    call = &fn.blocks[entry].instructions[1];
    jump = &fn.blocks[entry].instructions[2];
    saved_opcode = load->opcode;
    load->opcode = UINT32_C(0xdead);
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    load->opcode = saved_opcode;
    saved_kind = load->operands[1].kind;
    load->operands[1].kind = MINIC_MACHINE_OPERAND_INTEGER;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    load->operands[1].kind = saved_kind;
    load->operands[1].value.frame.index = 999U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    load->operands[1].value.frame.index = frame_id;
    saved_reg = load->operands[0].value.physical.reg;
    load->operands[0].value.physical.reg.number = 999U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    load->operands[0].value.physical.reg = saved_reg;
    saved_count = call->implicit_count;
    call->implicit_count = 0U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    call->implicit_count = saved_count;
    call->implicit[0].access = MINIC_MACHINE_REGISTER_READ;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    call->implicit[0].access = MINIC_MACHINE_REGISTER_CLOBBER;
    saved_features = fn.generated_features;
    fn.generated_features = 0U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.generated_features = saved_features;
    saved_string = call->operands[0].value.symbol.name;
    call->operands[0].value.symbol.name = "callee; .section .evil";
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    call->operands[0].value.symbol.name = saved_string;
    call->operands[0].value.symbol.relocation = 999U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    call->operands[0].value.symbol.relocation = 1U;
    jump->operands[0].value.block_id = 12345U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    jump->operands[0].value.block_id = exit_block;
    saved_count = fn.blocks[entry].successor_count;
    fn.blocks[entry].successor_count = 0U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].successor_count = saved_count;
    fn.frame_objects[0].alignment_bytes = 3U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.frame_objects[0].alignment_bytes = 8U;
    fn.frame_objects[0].fixed_offset = 1000;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.frame_objects[0].fixed_offset = 1;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.frame_objects[0].fixed_offset = 8;
    load->operands[1].value.frame.displacement = INT64_MAX;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    load->operands[1].value.frame.displacement = 0;
    fn.frame_objects[0].fixed_offset = 0;
    load->source_asm_template = "fake source escape";
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    load->source_asm_template = NULL;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    minic_machine_function_destroy(&fn);

    /* A source ASM statement is separately admitted, but may not borrow a
     * selected opcode or secretly bypass its generated-ISA requirement. */
    REQUIRE(minic_machine_function_initialize(&fn, &target,
                                              "machine_asm_probe", 1U));
    REQUIRE(minic_machine_function_add_block(&fn, NULL, false, &entry));
    memset(&ins, 0, sizeof(ins));
    ins.opcode = PROBE_LOAD_ADDRESS;
    ins.effects = MINIC_MACHINE_EFFECT_MEMORY_READ;
    ins.operand_count = 2U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ins.operands[0].value.physical.reg = gp(1U, width);
    ins.operands[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    ins.operands[1].kind = MINIC_MACHINE_OPERAND_ADDRESS;
    ins.operands[1].value.address.target_mode = 1U;
    ins.operands[1].value.address.has_base = true;
    ins.operands[1].value.address.base = gp(4U, width);
    REQUIRE(add_generated(&fn, entry, &ins));
    memset(&ins, 0, sizeof(ins));
    ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
                  MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    ins.source_asm_template = "opaque_source_isa_instruction";
    ins.operand_count = 1U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ins.operands[0].value.physical.reg = gp(1U, width);
    ins.operands[0].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    ins.operands[0].asm_constraint = "r";
    REQUIRE(minic_machine_block_append_instruction(&fn, entry, &ins));
    memset(&ins, 0, sizeof(ins));
    ins.opcode = PROBE_RETURN;
    ins.effects = MINIC_MACHINE_EFFECT_RETURN;
    REQUIRE(add_generated(&fn, entry, &ins));
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[0].operands[1].value.address.target_mode = 999U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[0].operands[1].value.address.target_mode = 1U;
    fn.blocks[entry].instructions[0].operands[1].value.address.has_index = true;
    fn.blocks[entry].instructions[0].operands[1].value.address.index =
        gp(5U, width);
    fn.blocks[entry].instructions[0].operands[1].value.address.scale = 3U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[0].operands[1].value.address.scale = 4U;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    state.source_asm_allowed = false;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    state.source_asm_allowed = true;
    fn.blocks[entry].instructions[1].opcode = PROBE_CALL;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[1].opcode = 0U;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    minic_machine_function_destroy(&fn);

    /* An indirect machine branch may name a set of indirect-entry blocks
     * without embedding fake direct-branch opcode operands. */
    REQUIRE(minic_machine_function_initialize(&fn, &target,
                                              "machine_indirect_probe", 1U));
    REQUIRE(minic_machine_function_add_block(&fn, NULL, false, &entry));
    REQUIRE(minic_machine_function_add_block(&fn, NULL, true, &exit_block));
    REQUIRE(minic_machine_block_add_successor(&fn, entry, exit_block));
    memset(&ins, 0, sizeof(ins));
    ins.opcode = PROBE_INDIRECT_JUMP;
    ins.effects = MINIC_MACHINE_EFFECT_BRANCH;
    ins.operand_count = 1U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ins.operands[0].value.physical.reg = gp(2U, width);
    ins.operands[0].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    REQUIRE(add_generated(&fn, entry, &ins));
    memset(&ins, 0, sizeof(ins));
    ins.opcode = PROBE_RETURN;
    ins.effects = MINIC_MACHINE_EFFECT_RETURN;
    REQUIRE(add_generated(&fn, exit_block, &ins));
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    fn.blocks[exit_block].indirect_entry = false;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[exit_block].indirect_entry = true;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    result = true;
fail:
    minic_machine_function_destroy(&fn);
    return result;
}

int main(void) {
    MinicTargetArchitecture architecture;
    for (architecture = MINIC_TARGET_ARCH_RISCV64;
         architecture <= MINIC_TARGET_ARCH_RISCV32; ++architecture) {
        if (!exercise_target(architecture)) return 1;
    }
    puts("MACHINE_IR_CONTRACT=PASS five_architectures=5 typed_opcode=PASS "
         "forged_negatives=PASS source_asm=PASS indirect_branch=PASS");
    return 0;
}
