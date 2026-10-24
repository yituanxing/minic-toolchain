/* Standalone psABI placement contract, independent of any Core IR frame. */
#include "target/riscv/gpr_abi_cursor.h"
#include <stdio.h>

static int check(MinicRiscvGprAbiCursor in, size_t words,
                 bool unnamed, bool pair, size_t rb, size_t rc,
                 size_t sb, size_t sc, size_t next_reg, size_t next_stack) {
    MinicRiscvGprAbiPlacement p = {0};
    if (!minic_riscv_gpr_abi_place(&in, words, unnamed, pair, &p) ||
        p.register_begin != rb || p.register_count != rc ||
        p.stack_begin != sb || p.stack_count != sc ||
        in.register_words != next_reg || in.stack_words != next_stack)
        return 1;
    return 0;
}
int main(void) {
    const MinicRiscvGprAbiCursor after_a0 = {1U, 0U, false};
    const MinicRiscvGprAbiCursor after_a6 = {7U, 0U, false};
    const MinicRiscvGprAbiCursor odd_stack = {8U, 1U, false};
    const MinicRiscvGprAbiCursor already_stack = {8U, 2U, true};
    MinicRiscvGprAbiPlacement result = {0};
    MinicRiscvGprAbiCursor overflow = {8U, SIZE_MAX, false};
    if (check(after_a6, 2U, false, false, 7U, 1U, 0U, 1U, 8U, 1U)) return 1;
    if (check(after_a0, 2U, true, true, 2U, 2U, 0U, 0U, 4U, 0U)) return 2;
    if (check(after_a6, 2U, true, true, 8U, 0U, 0U, 2U, 8U, 2U)) return 3;
    if (check(odd_stack, 2U, true, true, 8U, 0U, 2U, 2U, 8U, 4U)) return 4;
    if (check(already_stack, 1U, true, false, 8U, 0U, 2U, 1U, 8U, 3U)) return 5;
    if (check(after_a0, 2U, false, false, 1U, 2U, 0U, 0U, 3U, 0U)) return 6;
    if (minic_riscv_gpr_abi_place(&overflow, 1U, true, false, &result) ||
        overflow.stack_words != SIZE_MAX) return 7;
    if (minic_riscv_gpr_abi_place(&overflow, 2U, false, true, &result)) return 8;
    puts("RISCV_GPR_ABI_CURSOR=PASS named_split=1 wide_even=1 stack_align=1 overflow=reject");
    return 0;
}
