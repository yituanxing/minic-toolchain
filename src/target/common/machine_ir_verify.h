#ifndef MINIC_TARGET_COMMON_MACHINE_IR_VERIFY_H
#define MINIC_TARGET_COMMON_MACHINE_IR_VERIFY_H

#include "target/common/machine_ir.h"

/* This contract is supplied by the selected target, never by C Lowering or
 * a guessed generic five-ISA opcode list. The verifier looks it up afresh,
 * so a forged instruction cannot self-declare a weaker ISA requirement,
 * different operands, or missing implicit register clobbers. */
typedef struct MinicMachineOpcodeContract {
    size_t operand_count;
    MinicMachineOperandKind operand_kinds[MINIC_MACHINE_MAX_OPERANDS];
    unsigned int register_access[MINIC_MACHINE_MAX_OPERANDS];
    uint64_t required_generated_features;
    unsigned int required_effects;
    unsigned int permitted_effects;
    MinicMachineRegisterEffect implicit[MINIC_MACHINE_MAX_REGISTER_EFFECTS];
    size_t implicit_count;
    bool terminates_block;
    /* A conditional branch may terminate the block while still falling
     * through to the immediately following block. Plain jumps/returns may
     * not advertise a second implicit control-flow edge. */
    bool permits_fallthrough;
    /* Indirect jumps can target a set of indirect-entry blocks without one
     * encoded block operand per possible destination. */
    bool has_indirect_successors;
} MinicMachineOpcodeContract;

typedef bool (*MinicMachineLookupOpcodeContract)(
    const MinicTargetInfo *, uint32_t opcode,
    MinicMachineOpcodeContract *contract, void *context);
/* Full opcode-specific operand legality: immediate ranges, register
 * combinations, shifts, alignment, and any target encodability constraints. */
typedef bool (*MinicMachineValidateSelectedInstruction)(
    const MinicMachineFunction *, const MinicMachineInstruction *, void *context);
typedef bool (*MinicMachineValidateGeneratedFeatures)(
    const MinicTargetInfo *, uint64_t generated_features, void *context);
typedef bool (*MinicMachineValidateFunctionAbi)(
    const MinicTargetInfo *, MinicFunctionCallingConvention, void *context);
typedef bool (*MinicMachineValidateFrameObject)(
    const MinicTargetInfo *, const MinicMachineFunction *,
    const MinicMachineFrameObject *, void *context);
typedef bool (*MinicMachineValidateRegister)(
    const MinicTargetInfo *, const MinicMachineRegister *, void *context);
typedef bool (*MinicMachineValidateAddress)(
    const MinicTargetInfo *, const MinicMachineAddress *, void *context);
typedef bool (*MinicMachineValidateRelocation)(
    const MinicTargetInfo *, const char *symbol, uint32_t relocation,
    int64_t addend, void *context);
typedef bool (*MinicMachineValidateSourceAsm)(
    const MinicTargetInfo *, const MinicMachineInstruction *, void *context);
/* A selected source-ASM expansion is NOT raw source text. Independently
 * recognize the opcode, physical operands, effects and sealed source form;
 * no callback means target-qualified selected expansions are unsupported. */
typedef bool (*MinicMachineValidateSelectedSourceAsm)(
    const MinicMachineFunction *, const MinicMachineInstruction *, void *context);

typedef struct MinicMachineVerifyPolicy {
    MinicMachineLookupOpcodeContract lookup_opcode;
    MinicMachineValidateSelectedInstruction validate_selected_instruction;
    /* Required for ALL targets, including x86/A64/LoongArch: a MachineFunction
     * cannot claim arbitrary generated ISA bits to qualify forged opcodes. */
    MinicMachineValidateGeneratedFeatures validate_generated_features;
    /* Machine ABI legality depends on the selected target, not source C. */
    MinicMachineValidateFunctionAbi validate_function_abi;
    /* Required ABI/ISA physical frame addressing gate (e.g. x86 red zone,
     * FP/SP basis and dynamic stack, RISC-V immediate addressing). */
    MinicMachineValidateFrameObject validate_frame_object;
    MinicMachineValidateRegister validate_register;
    MinicMachineValidateAddress validate_address;
    MinicMachineValidateRelocation validate_relocation;
    MinicMachineValidateSourceAsm validate_source_asm;
    MinicMachineValidateSelectedSourceAsm validate_selected_source_asm;
    void *context;
} MinicMachineVerifyPolicy;

/* A complete-function gate, not a per-instruction opt-in. No machine text
 * should be serialized until this returns true for the entire function. */
bool minic_machine_function_verify(const MinicMachineFunction *,
    const MinicMachineVerifyPolicy *);

#endif
