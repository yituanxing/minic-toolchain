/* Real owned Machine IR contract: independent of the current direct-text
 * backends. All five target identities exercise one fail-closed verifier. */
#include "target/common/machine_ir.h"
#include "target/common/machine_builder.h"
#include "target/common/machine_ir_verify.h"

#include <stdio.h>
#include <string.h>

enum ProbeOpcode {
    PROBE_LOAD_FRAME = 1,
    PROBE_CALL,
    PROBE_JUMP,
    PROBE_RETURN,
    PROBE_LOAD_ADDRESS,
    PROBE_INDIRECT_JUMP,
    PROBE_CONDITIONAL,
    PROBE_BLOCK_ADDRESS,
    PROBE_MANY
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

static void reset_input(MinicMachineInstruction *ins,
    MinicMachineOperand *operands, MinicMachineRegisterEffect *implicit) {
    memset(ins, 0, sizeof(*ins));
    memset(operands, 0, MINIC_MACHINE_MAX_OPERANDS * sizeof(*operands));
    memset(implicit, 0,
           MINIC_MACHINE_MAX_REGISTER_EFFECTS * sizeof(*implicit));
    ins->operands = operands;
    ins->implicit = implicit;
}

static bool probe_generated_features(const MinicTargetInfo *target,
    uint64_t features, void *context) {
    (void)target;
    (void)context;
    return features == UINT64_C(1);
}

static bool probe_function_abi(const MinicTargetInfo *target,
    MinicFunctionCallingConvention abi, void *context) {
    const size_t expected_pointer_bytes =
        target->architecture == MINIC_TARGET_ARCH_RISCV32 ? 4U : 8U;
    (void)context;
    return target->data_layout != NULL &&
        target->data_layout->pointer_size == expected_pointer_bytes &&
        (abi == MINIC_CALLING_CONVENTION_DEFAULT ||
         (target->architecture == MINIC_TARGET_ARCH_X86_64 &&
          abi == MINIC_CALLING_CONVENTION_MS_X64));
}

static bool probe_frame_object(const MinicTargetInfo *target,
    const MinicMachineFunction *fn, const MinicMachineFrameObject *obj,
    void *context) {
    (void)target;
    (void)fn;
    (void)context;
    return !(obj->base == MINIC_MACHINE_FRAME_BASE_ENTRY_STACK_POINTER &&
             obj->area == MINIC_MACHINE_FRAME_OUTGOING);
}

static bool probe_selected(const MinicMachineFunction *fn,
    const MinicMachineInstruction *ins, void *context) {
    (void)context;
    if (ins->opcode == PROBE_LOAD_FRAME) {
        const MinicMachineFrameObject *object =
            &fn->frame_objects[ins->operands[1].value.frame.index];
        const int64_t base = object->fixed_offset;
        const int64_t addend = ins->operands[1].value.frame.displacement;
        if (addend < 0 && base < INT64_MIN - addend) return false;
        if (addend > 0 && base > INT64_MAX - addend) return false;
        return (base + addend) >= -16 && (base + addend) <= 16;
    }
    if (ins->opcode == PROBE_LOAD_ADDRESS)
        return ins->operands[1].value.address.displacement >= -2048 &&
               ins->operands[1].value.address.displacement <= 2048;
    return true;
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
    case PROBE_CONDITIONAL:
        result->operand_count = 1U;
        result->operand_kinds[0] = MINIC_MACHINE_OPERAND_BLOCK;
        result->required_effects = MINIC_MACHINE_EFFECT_BRANCH;
        result->permitted_effects = MINIC_MACHINE_EFFECT_BRANCH;
        result->terminates_block = true;
        result->permits_fallthrough = true;
        return true;
    case PROBE_BLOCK_ADDRESS:
        result->operand_count = 2U;
        result->operand_kinds[0] = MINIC_MACHINE_OPERAND_REGISTER;
        result->register_access[0] = MINIC_MACHINE_REGISTER_WRITE;
        result->operand_kinds[1] = MINIC_MACHINE_OPERAND_BLOCK_ADDRESS;
        return true;
    case PROBE_MANY: {
        size_t i;
        result->operand_count = MINIC_MACHINE_MAX_OPERANDS;
        for (i = 0U; i < MINIC_MACHINE_MAX_OPERANDS; ++i)
            result->operand_kinds[i] = MINIC_MACHINE_OPERAND_INTEGER;
        result->implicit_count = MINIC_MACHINE_MAX_REGISTER_EFFECTS;
        for (i = 0U; i < MINIC_MACHINE_MAX_REGISTER_EFFECTS; ++i) {
            result->implicit[i].reg = gp((unsigned int)(i % 32U), width);
            result->implicit[i].access = MINIC_MACHINE_REGISTER_CLOBBER;
        }
        return true;
    }
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
    if (ins->operand_count == 0U) ins->operands = NULL;
    if (ins->implicit_count == 0U) ins->implicit = NULL;
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
    MinicDataLayout layout = {0};
    MinicMachineFunction fn = {0};
    MinicMachineVerifyPolicy policy = {0};
    ProbePolicyState state = {true};
    MinicMachineInstruction ins = {0};
    MinicMachineBuilder builder = {0};
    MinicMachineOperand input_operands[MINIC_MACHINE_MAX_OPERANDS];
    MinicMachineRegisterEffect input_implicit[MINIC_MACHINE_MAX_REGISTER_EFFECTS];
    MinicMachineFrameObject frame = {0};
    MinicMachineInstruction *load, *call, *jump;
    MinicMachineRegister saved_reg;
    MinicMachineOperandKind saved_kind;
    char *saved_string;
    size_t entry, exit_block, auxiliary_block, frame_id;
    size_t saved_count;
    size_t barrier_index;
    uint32_t saved_opcode;
    uint64_t saved_features;
    bool result = false;
    const unsigned int width = arch == MINIC_TARGET_ARCH_RISCV32 ? 32U : 64U;
    char symbol[] = "callee";
    char got_symbol[] = "got_anchor";
    char source_bytes[] = {'o','p','a','q','u','e','\0','r','v'};
    char label[] = ".Lmachine_exit";
    target.architecture = arch;
    layout.byte_order = MINIC_BYTE_ORDER_LITTLE;
    layout.pointer_size = arch == MINIC_TARGET_ARCH_RISCV32 ? 4U : 8U;
    layout.pointer_alignment = layout.pointer_size;
    target.data_layout = &layout;
    /* The generic contract must not know the byte shape of target-private
     * ISA state. The target callback alone owns feature validation. */
    policy.lookup_opcode = probe_lookup;
    policy.validate_selected_instruction = probe_selected;
    policy.validate_generated_features = probe_generated_features;
    policy.validate_function_abi = probe_function_abi;
    policy.validate_frame_object = probe_frame_object;
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

    reset_input(&ins, input_operands, input_implicit);
    REQUIRE(!minic_machine_block_append_instruction(&fn, entry, &ins));

    reset_input(&ins, input_operands, input_implicit);
    ins.opcode = PROBE_LOAD_FRAME;
    ins.effects = MINIC_MACHINE_EFFECT_MEMORY_READ;
    ins.operand_count = 2U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ins.operands[0].value.physical.reg = gp(1U, width);
    ins.operands[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    ins.operands[1].kind = MINIC_MACHINE_OPERAND_FRAME;
    ins.operands[1].value.frame.index = frame_id;
    REQUIRE(add_generated(&fn, entry, &ins));

    reset_input(&ins, input_operands, input_implicit);
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

    reset_input(&ins, input_operands, input_implicit);
    ins.opcode = PROBE_JUMP;
    ins.effects = MINIC_MACHINE_EFFECT_BRANCH;
    ins.operand_count = 1U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_BLOCK;
    ins.operands[0].value.block_id = exit_block;
    REQUIRE(add_generated(&fn, entry, &ins));
    reset_input(&ins, input_operands, input_implicit);
    ins.opcode = PROBE_RETURN;
    ins.effects = MINIC_MACHINE_EFFECT_RETURN;
    REQUIRE(add_generated(&fn, exit_block, &ins));
    REQUIRE(minic_machine_function_verify(&fn, &policy));

    /* The physical MachineFunction refers to a target-owned DataLayout,
     * never a freshly guessed 32/64-bit data model in Core. */
    layout.pointer_size = 0U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    layout.pointer_size = width == 32U ? 8U : 4U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    layout.pointer_size = width / 8U;
    REQUIRE(minic_machine_function_verify(&fn, &policy));

    /* Plain jumps cannot invent an additional fallthrough; conditional
     * branches can, but only with the target's independent contract. */
    REQUIRE(minic_machine_block_set_fallthrough(&fn, entry, exit_block));
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[2].opcode = PROBE_CONDITIONAL;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[2].opcode = PROBE_JUMP;
    fn.blocks[entry].has_fallthrough = false;
    REQUIRE(minic_machine_function_verify(&fn, &policy));

    /* Generated features must not be self-proclaimed, including on non-RV
     * backends where the target's codegen feature bits live elsewhere. */
    fn.generated_features = UINT64_C(2);
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.generated_features = UINT64_C(1);
    policy.validate_generated_features = NULL;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    policy.validate_generated_features = probe_generated_features;
    policy.validate_selected_instruction = NULL;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    policy.validate_selected_instruction = probe_selected;
    policy.validate_function_abi = NULL;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    policy.validate_function_abi = probe_function_abi;
    policy.validate_frame_object = NULL;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    policy.validate_frame_object = probe_frame_object;
    fn.calling_convention = MINIC_CALLING_CONVENTION_MS_X64;
    if (arch == MINIC_TARGET_ARCH_X86_64)
        REQUIRE(minic_machine_function_verify(&fn, &policy));
    else
        REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.calling_convention = MINIC_CALLING_CONVENTION_DEFAULT;

    /* Local SP offsets are unstable across dynamic allocation, unlike FP
     * references, which are explicitly distinguished in physical MIR. */
    fn.has_dynamic_stack = true;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.frame_objects[0].base = MINIC_MACHINE_FRAME_BASE_FRAME_POINTER;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    fn.frame_objects[0].base = MINIC_MACHINE_FRAME_BASE_STACK_POINTER;
    fn.has_dynamic_stack = false;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    fn.frame_objects[0].base =
        MINIC_MACHINE_FRAME_BASE_ENTRY_STACK_POINTER;
    fn.frame_objects[0].area = MINIC_MACHINE_FRAME_OUTGOING;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.frame_objects[0].area = MINIC_MACHINE_FRAME_LOCAL;
    fn.frame_objects[0].base = MINIC_MACHINE_FRAME_BASE_STACK_POINTER;

    load = &fn.blocks[entry].instructions[0];
    call = &fn.blocks[entry].instructions[1];
    jump = &fn.blocks[entry].instructions[2];
    REQUIRE(sizeof(MinicMachineInstruction) < 256U);
    REQUIRE(fn.blocks[exit_block].instructions[0].operands == NULL);
    REQUIRE(fn.blocks[exit_block].instructions[0].implicit == NULL);
    /* Inactive operand and implicit storage no longer exists, so a zero-op
     * return carries no hidden per-opcode 16/64-element memory cost. */
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
    load->operands[1].value.frame.displacement = 17;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    load->operands[1].value.frame.displacement = 0;
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
    fn.frame_objects[0].fixed_offset = -8;
    fn.frame_objects[0].size_bytes = 16U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.frame_objects[0].size_bytes = 8U;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    fn.frame_objects[0].fixed_offset = 8;
    load->operands[1].value.frame.displacement = INT64_MAX;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    load->operands[1].value.frame.displacement = 0;
    fn.frame_objects[0].fixed_offset = 0;
    saved_string = fn.blocks[exit_block].exported_label;
    fn.blocks[exit_block].exported_label = fn.name;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[exit_block].exported_label = saved_string;
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
    reset_input(&ins, input_operands, input_implicit);
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
    ins.operands[1].value.address.has_symbol = true;
    ins.operands[1].value.address.symbol = got_symbol;
    ins.operands[1].value.address.relocation = 2U;
    ins.operands[1].value.address.has_symbol = false;
    REQUIRE(!add_generated(&fn, entry, &ins));
    ins.operands[1].value.address.has_symbol = true;
    REQUIRE(add_generated(&fn, entry, &ins));
    got_symbol[0] = 'X';
    REQUIRE(strcmp(fn.blocks[entry].instructions[0].operands[1]
                    .value.address.symbol, "got_anchor") == 0);
    reset_input(&ins, input_operands, input_implicit);
    ins.kind = MINIC_MACHINE_INSTRUCTION_SOURCE_ASM;
    ins.effects = MINIC_MACHINE_EFFECT_SOURCE_ASM |
                  MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    ins.source_asm_template = source_bytes;
    ins.source_asm_length = sizeof(source_bytes);
    ins.has_core_origin = true;
    ins.core_instruction_id = 1U;
    ins.operand_count = 1U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ins.operands[0].value.physical.reg = gp(1U, width);
    ins.operands[0].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    ins.operands[0].asm_constraint = "r";
    ins.implicit = NULL;
    REQUIRE(minic_machine_block_append_instruction(&fn, entry, &ins));
    source_bytes[0] = 'X';
    REQUIRE(fn.blocks[entry].instructions[1].source_asm_length ==
            sizeof(source_bytes));
    REQUIRE(fn.blocks[entry].instructions[1].source_asm_template[0] == 'o');
    REQUIRE(fn.blocks[entry].instructions[1].source_asm_template[6] == '\0');
    REQUIRE(fn.blocks[entry].instructions[1].source_asm_template[8] == 'v');
    reset_input(&ins, input_operands, input_implicit);
    ins.opcode = PROBE_RETURN;
    ins.effects = MINIC_MACHINE_EFFECT_RETURN;
    REQUIRE(add_generated(&fn, entry, &ins));
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[0].operands[1].value.address.relocation = 999U;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[0].operands[1].value.address.relocation = 2U;
    fn.blocks[entry].instructions[0].operands[1].value.address.has_symbol = false;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[0].operands[1].value.address.has_symbol = true;
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
    fn.blocks[entry].instructions[1].has_core_origin = false;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[1].has_core_origin = true;
    fn.blocks[entry].instructions[1].source_asm_terminates_block = true;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[1].source_asm_terminates_block = false;
    fn.blocks[entry].instructions[1].source_asm_permits_fallthrough = true;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[1].source_asm_permits_fallthrough = false;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    minic_machine_function_destroy(&fn);

    /* An indirect machine branch may name a set of indirect-entry blocks
     * without embedding fake direct-branch opcode operands. */
    REQUIRE(minic_machine_function_initialize(&fn, &target,
                                              "machine_indirect_probe", 1U));
    REQUIRE(minic_machine_function_add_block(&fn, NULL, false, &entry));
    REQUIRE(minic_machine_function_add_block(&fn, NULL, true, &exit_block));
    REQUIRE(minic_machine_function_add_block(
        &fn, NULL, true, &auxiliary_block));
    fn.blocks[auxiliary_block].machine_auxiliary = true;
    REQUIRE(minic_machine_block_add_successor(&fn, entry, exit_block));
    reset_input(&ins, input_operands, input_implicit);
    ins.opcode = PROBE_BLOCK_ADDRESS;
    ins.operand_count = 2U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ins.operands[0].value.physical.reg = gp(1U, width);
    ins.operands[0].value.physical.access = MINIC_MACHINE_REGISTER_WRITE;
    ins.operands[1].kind = MINIC_MACHINE_OPERAND_BLOCK_ADDRESS;
    ins.operands[1].value.block_id = auxiliary_block;
    REQUIRE(add_generated(&fn, entry, &ins));
    reset_input(&ins, input_operands, input_implicit);
    ins.opcode = PROBE_INDIRECT_JUMP;
    ins.effects = MINIC_MACHINE_EFFECT_BRANCH;
    ins.operand_count = 1U;
    ins.operands[0].kind = MINIC_MACHINE_OPERAND_REGISTER;
    ins.operands[0].value.physical.reg = gp(2U, width);
    ins.operands[0].value.physical.access = MINIC_MACHINE_REGISTER_READ;
    REQUIRE(add_generated(&fn, entry, &ins));
    reset_input(&ins, input_operands, input_implicit);
    ins.opcode = PROBE_RETURN;
    ins.effects = MINIC_MACHINE_EFFECT_RETURN;
    REQUIRE(add_generated(&fn, exit_block, &ins));
    REQUIRE(add_generated(&fn, auxiliary_block, &ins));
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    /* BLOCK_ADDRESS names an indirect-entry internal label, but does not
     * require a fabricated direct-branch CFG edge to that label. */
    fn.blocks[auxiliary_block].indirect_entry = false;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[auxiliary_block].indirect_entry = true;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    fn.blocks[exit_block].indirect_entry = false;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[exit_block].indirect_entry = true;
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    minic_machine_function_destroy(&fn);

    /* The exact-count owner is not merely a small fixed inline array:
     * stress its entire public operand/register-effect capacity. */
    REQUIRE(minic_machine_function_initialize(
        &fn, &target, "machine_capacity_probe", UINT64_C(1)));
    REQUIRE(minic_machine_function_add_block(&fn, NULL, false, &entry));
    reset_input(&ins, input_operands, input_implicit);
    ins.opcode = PROBE_MANY;
    ins.operand_count = MINIC_MACHINE_MAX_OPERANDS;
    ins.implicit_count = MINIC_MACHINE_MAX_REGISTER_EFFECTS;
    {
        size_t i;
        for (i = 0U; i < ins.operand_count; ++i) {
            ins.operands[i].kind = MINIC_MACHINE_OPERAND_INTEGER;
            ins.operands[i].value.integer = (int64_t)i;
        }
        for (i = 0U; i < ins.implicit_count; ++i) {
            ins.implicit[i].reg = gp((unsigned int)(i % 32U), width);
            ins.implicit[i].access = MINIC_MACHINE_REGISTER_CLOBBER;
        }
    }
    REQUIRE(add_generated(&fn, entry, &ins));
    /* Core compiler ordering must survive MachineIR without a fake native
     * fence, source-ASM escape or emitted bytes. The marker is not admitted
     * without exact Core provenance. */
    REQUIRE(minic_machine_builder_initialize(&builder, &fn));
    REQUIRE(minic_machine_builder_position_at(&builder, entry));
    REQUIRE(!minic_machine_builder_emit_compiler_barrier(&builder));
    minic_machine_builder_set_origin(&builder, 0U, 17U);
    barrier_index = fn.blocks[entry].instruction_count;
    REQUIRE(minic_machine_builder_emit_compiler_barrier(&builder));
    reset_input(&ins, input_operands, input_implicit);
    ins.opcode = PROBE_RETURN;
    ins.effects = MINIC_MACHINE_EFFECT_RETURN;
    REQUIRE(add_generated(&fn, entry, &ins));
    REQUIRE(minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[barrier_index].effects |=
        MINIC_MACHINE_EFFECT_MEMORY_WRITE;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[barrier_index].effects =
        MINIC_MACHINE_EFFECT_COMPILER_BARRIER;
    fn.blocks[entry].instructions[barrier_index].has_core_origin = false;
    REQUIRE(!minic_machine_function_verify(&fn, &policy));
    fn.blocks[entry].instructions[barrier_index].has_core_origin = true;
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
         "forged_negatives=PASS source_asm=PASS indirect_branch=PASS "
         "compiler_barrier=PASS");
    return 0;
}
