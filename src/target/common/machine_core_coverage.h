#ifndef MINIC_TARGET_COMMON_MACHINE_CORE_COVERAGE_H
#define MINIC_TARGET_COMMON_MACHINE_CORE_COVERAGE_H

#include "core/core_ir.h"
#include "target/common/machine_ir.h"

/* A production Core->Machine selector must preserve every executable Core
 * effect.  Pure SSA definitions may fold into target operands, but observable
 * memory/call/ASM/ABI effects need provenance and the corresponding physical
 * effect bits.  This is an additional check beyond physical MachineVerifier;
 * neither check makes a partial target selector production-complete alone. */
bool minic_machine_core_coverage_verify(const MinicCoreFunction *,
                                        const bool *reachable_blocks,
                                        const MinicMachineFunction *);

/* This independent target proof is required for a compiler-owned SELECTED
 * replacement of an original target-qualified structured source ASM. The
 * callback receives both sealed Core operand roles/constraints and the
 * physical instruction, so copying a form ID onto an arbitrary opcode is
 * not a proof. It must check the exact native opcode/operand role mapping;
 * absent callback rejects all selected ASM seals. Ordinary source-owned
 * SOURCE_ASM still passes through its distinct source verifier. */
typedef bool (*MinicMachineCoreSelectedAsmProof)(
    const MinicCoreFunction *, const MinicCoreInstruction *,
    const MinicCoreInlineAsm *, const MinicMachineFunction *,
    const MinicMachineInstruction *, void *context);

typedef struct MinicMachineCoreCoveragePolicy {
    MinicMachineCoreSelectedAsmProof prove_selected_source_asm;
    void *context;
} MinicMachineCoreCoveragePolicy;

bool minic_machine_core_coverage_verify_with_policy(
    const MinicCoreFunction *, const bool *reachable_blocks,
    const MinicMachineFunction *, const MinicMachineCoreCoveragePolicy *);

#endif
