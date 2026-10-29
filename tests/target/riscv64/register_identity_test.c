/* Native target-policy test; RISC-V instructions are emitted as text only.
 * cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude \
 *    tests/target/riscv64/register_identity_test.c \
 *    src/target/riscv64/asm_plan.c -o /tmp/minic-riscv-register-test
 */
#include "target/riscv/register_identity.h"
#include "target/riscv64/asm_plan.h"

#include <stdio.h>
#include <string.h>

static bool alias_catalog(void) {
    static const struct { const char *a; const char *b; unsigned id; } pairs[] = {
        {"zero", "x0", 0U}, {"ra", "x1", 1U}, {"sp", "x2", 2U},
        {"tp", "x4", 4U}, {"t0", "x5", 5U}, {"t1", "x6", 6U},
        {"t2", "x7", 7U}, {"fp", "s0", 8U}, {"s0", "x8", 8U},
        {"a0", "x10", 10U}, {"a7", "x17", 17U},
        {"s11", "x27", 27U}, {"t3", "x28", 28U},
        {"t5", "x30", 30U}, {"t6", "x31", 31U},
    };
    static const char *const invalid[] = {
        "", "x", "x00", "x32", "x99", "x2a", "x-1", "t7",
        "a8", "s12", "v0", "x7 ", "T2", "%t2", "f0"
    };
    size_t i;
    for (i = 0U; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        unsigned a_id, b_id;
        if (!minic_riscv_gpr_name_id(pairs[i].a, &a_id) ||
            !minic_riscv_gpr_name_id(pairs[i].b, &b_id) ||
            a_id != pairs[i].id || b_id != pairs[i].id ||
            !minic_riscv_gpr_name_equal(pairs[i].a, pairs[i].b))
            return false;
    }
    for (i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        unsigned id;
        if (minic_riscv_gpr_name_id(invalid[i], &id))
            return false;
    }
    return minic_riscv_gpr_caller_saved(7U) &&
           minic_riscv_gpr_caller_saved(31U) &&
           !minic_riscv_gpr_caller_saved(8U) &&
           minic_riscv_gpr_callee_saved(8U) &&
           minic_riscv_gpr_callee_saved(27U) &&
           !minic_riscv_gpr_callee_saved(7U);
}

static bool plan_clobber_alias(void) {
    MinicCoreInlineAsmRegisterClobber clobber = {"x7", 2U};
    MinicCoreInlineAsm source = {0};
    MinicCoreFunction function = {0};
    MinicCoreInstruction instruction = {0};
    MinicRiscv64AsmInstructionPlan plan;
    source.register_clobbers = &clobber;
    source.register_clobber_count = 1U;
    function.inline_asms = &source;
    function.inline_asm_count = 1U;
    instruction.kind = MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM;
    instruction.value.structured_inline_asm.operand_count = 1U;
    instruction.value.structured_inline_asm.operands[0].operand_index = 0U;
    instruction.value.structured_inline_asm.operands[0].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    if (!minic_riscv64_asm_instruction_plan_build(&function, &instruction, &plan) ||
        plan.operand_registers[0] == NULL ||
        minic_riscv_gpr_name_equal(plan.operand_registers[0], "t2"))
        return false;

    /* x10 and a0 are one physical fixed register, not two independently
     * allocatable operands. Two read-only fixed inputs must fail closed. */
    MinicCoreFixedRegisterBinding fixed[2] = {
        {.register_name="x10", .register_name_length=3U, .is_local=true},
        {.register_name="a0", .register_name_length=2U, .is_local=true},
    };
    function.fixed_register_bindings = fixed;
    function.fixed_register_binding_count = 2U;
    source.register_clobber_count = 0U;
    instruction.value.structured_inline_asm.operand_count = 2U;
    instruction.value.structured_inline_asm.operands[0].has_fixed_register_binding = true;
    instruction.value.structured_inline_asm.operands[0].fixed_register_binding_id = 0U;
    instruction.value.structured_inline_asm.operands[1].operand_index = 1U;
    instruction.value.structured_inline_asm.operands[1].kind =
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
    instruction.value.structured_inline_asm.operands[1].has_fixed_register_binding = true;
    instruction.value.structured_inline_asm.operands[1].fixed_register_binding_id = 1U;
    if (minic_riscv64_asm_instruction_plan_build(&function, &instruction, &plan))
        return false;

    /* A real fixed binding cannot ignore a synonymous source clobber. */
    clobber.name = "x10";
    clobber.name_length = 3U;
    source.register_clobber_count = 1U;
    instruction.value.structured_inline_asm.operand_count = 1U;
    if (minic_riscv64_asm_instruction_plan_build(&function, &instruction, &plan))
        return false;

    /* Core permits 16 operands; this machine plan has exactly 10 slots and
     * must reject 11 rather than overrun the target arrays. */
    source.register_clobber_count = 0U;
    instruction.value.structured_inline_asm.operand_count = 11U;
    return !minic_riscv64_asm_instruction_plan_build(&function, &instruction, &plan);
}

int main(void) {
    if (!alias_catalog() || !plan_clobber_alias()) {
        (void)fprintf(stderr, "RISC-V GPR identity/inline-asm plan FAIL\n");
        return 1;
    }
    (void)puts("RISC-V GPR identity/inline-asm plan PASS");
    return 0;
}
