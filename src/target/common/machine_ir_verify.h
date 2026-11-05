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
    /* Indirect jumps can target a set of indirect-entry blocks without one
     * encoded block operand per possible destination. */
    bool has_indirect_successors;
} MinicMachineOpcodeContract;

typedef bool (*MinicMachineLookupOpcodeContract)(
    const MinicTargetInfo *, uint32_t opcode,
    MinicMachineOpcodeContract *contract, void *context);
typedef bool (*MinicMachineValidateRegister)(
    const MinicTargetInfo *, const MinicMachineRegister *, void *context);
typedef bool (*MinicMachineValidateAddress)(
    const MinicTargetInfo *, const MinicMachineAddress *, void *context);
typedef bool (*MinicMachineValidateRelocation)(
    const MinicTargetInfo *, const char *symbol, uint32_t relocation,
    int64_t addend, void *context);
typedef bool (*MinicMachineValidateSourceAsm)(
    const MinicTargetInfo *, const MinicMachineInstruction *, void *context);

typedef struct MinicMachineVerifyPolicy {
    MinicMachineLookupOpcodeContract lookup_opcode;
    MinicMachineValidateRegister validate_register;
    MinicMachineValidateAddress validate_address;
    MinicMachineValidateRelocation validate_relocation;
    MinicMachineValidateSourceAsm validate_source_asm;
    void *context;
} MinicMachineVerifyPolicy;

/* A complete-function gate, not a per-instruction opt-in. No machine text
 * should be serialized until this returns true for the entire function. */
bool minic_machine_function_verify(const MinicMachineFunction *,
    const MinicMachineVerifyPolicy *);

#endif
