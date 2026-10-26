/* ISA/ABI-free RISC-V machine selection contract for both XLEN widths. */
#include "target/riscv/instruction_select.h"
#include <stdio.h>
#include <limits.h>
#include <string.h>

static int match(const char *got, const char *expected) {
    return got != NULL && strcmp(got, expected) == 0;
}
static bool pointer_scale(size_t stride,
                          MinicRiscvPointerScaleKind kind,
                          unsigned shift) {
    MinicRiscvPointerScalePlan plan;
    return minic_riscv_plan_pointer_scale(stride, &plan) &&
           plan.kind == kind && plan.shift == shift;
}
int main(void) {
    const struct {
        MinicCoreInstructionKind kind;
        bool is_unsigned;
        unsigned xlen;
        const char *symbol;
    } arithmetic[] = {
        { MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY, false, 32U, "__mulsi3" },
        { MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY, false, 64U, "__muldi3" },
        { MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE, false, 32U, "__divsi3" },
        { MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE, true, 32U, "__udivsi3" },
        { MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER, false, 32U, "__modsi3" },
        { MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER, true, 32U, "__umodsi3" },
        { MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE, false, 64U, "__divdi3" },
        { MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE, true, 64U, "__udivdi3" },
        { MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER, false, 64U, "__moddi3" },
        { MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER, true, 64U, "__umoddi3" }
    };
    size_t i;
    for (i = 0U; i < sizeof(arithmetic) / sizeof(arithmetic[0]); ++i)
        if (!match(minic_riscv_integer_arithmetic_symbol(
                    arithmetic[i].kind, arithmetic[i].is_unsigned,
                    arithmetic[i].xlen), arithmetic[i].symbol))
            return 1;
    if (minic_riscv_integer_arithmetic_symbol(
            MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE, false, 128U) != NULL ||
        minic_riscv_integer_arithmetic_symbol(
            MINIC_CORE_INSTRUCTION_POINTER_LESS, true, 32U) != NULL)
        return 2;
    if (!match(minic_riscv_scalar_binary_opcode(
            MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT, false), "sra") ||
        !match(minic_riscv_scalar_binary_opcode(
            MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT, true), "srl") ||
        !match(minic_riscv_scalar_binary_opcode(
            MINIC_CORE_INSTRUCTION_INTEGER_LESS, false), "slt") ||
        !match(minic_riscv_scalar_binary_opcode(
            MINIC_CORE_INSTRUCTION_INTEGER_LESS, true), "sltu"))
        return 3;
    if (!match(minic_riscv_softfloat_arithmetic_symbol(
            MINIC_CORE_INSTRUCTION_DOUBLE_ADD), "__adddf3"))
        return 4;
    if (!pointer_scale(0U, MINIC_RISCV_POINTER_SCALE_ZERO, 0U) ||
        !pointer_scale(1U, MINIC_RISCV_POINTER_SCALE_UNIT, 0U) ||
        !pointer_scale(2U, MINIC_RISCV_POINTER_SCALE_SHIFT, 1U) ||
        !pointer_scale(8U, MINIC_RISCV_POINTER_SCALE_SHIFT, 3U) ||
        !pointer_scale(3U, MINIC_RISCV_POINTER_SCALE_MULTIPLY, 0U) ||
        !pointer_scale((size_t)-1, MINIC_RISCV_POINTER_SCALE_MULTIPLY, 0U) ||
        !pointer_scale((size_t)1U << (sizeof(size_t) * CHAR_BIT - 1U),
                       MINIC_RISCV_POINTER_SCALE_SHIFT,
                       (unsigned)(sizeof(size_t) * CHAR_BIT - 1U)) ||
        minic_riscv_plan_pointer_scale(0U, NULL))
        return 5;
    puts("RISCV_MACHINE_SELECTION=PASS ilp32_libgcc=5 lp64_libgcc=5 shift_compare=4 softfloat=1");
    return 0;
}
