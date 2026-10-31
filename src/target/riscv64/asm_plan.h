#ifndef MINIC_TARGET_RISCV64_ASM_PLAN_H
#define MINIC_TARGET_RISCV64_ASM_PLAN_H

#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* RV64 native asm allocation is recorded once per function emission. */
#define MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT 12U
#define MINIC_RISCV64_ASM_MAX_OPERANDS 10U
extern const char *const minic_riscv64_asm_callee_saved_registers[MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT];

typedef struct MinicRiscv64AsmInstructionPlan {
    const char *operand_registers[MINIC_RISCV64_ASM_MAX_OPERANDS];
    bool memory_operand[MINIC_RISCV64_ASM_MAX_OPERANDS];
    const char *scratch_register;
    const char *transfer_scratch_register;
    bool valid;
} MinicRiscv64AsmInstructionPlan;

typedef struct MinicRiscv64AsmFunctionPlan {
    MinicRiscv64AsmInstructionPlan *instructions;
    size_t instruction_count;
    uint16_t callee_saved_mask;
} MinicRiscv64AsmFunctionPlan;

bool minic_riscv64_asm_instruction_plan_build(
    const MinicCoreFunction *, const MinicCoreInstruction *,
    MinicRiscv64AsmInstructionPlan *);
bool minic_riscv64_asm_function_plan_build(
    const MinicCoreFunction *, MinicRiscv64AsmFunctionPlan *);
const MinicRiscv64AsmInstructionPlan *minic_riscv64_asm_function_plan_get(
    const MinicRiscv64AsmFunctionPlan *, size_t instruction_id);
void minic_riscv64_asm_function_plan_destroy(MinicRiscv64AsmFunctionPlan *);
size_t minic_riscv64_asm_callee_saved_mask_count(uint16_t);

#endif
