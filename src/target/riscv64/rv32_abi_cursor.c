#include "target/riscv64/rv32_abi_cursor.h"
#include "target/riscv/gpr_abi_cursor.h"

/* ILP32 policy: every unnamed 2-XLEN scalar requires an even pair.
 * LP64 uses the shared cursor only when its classifier requires it. */
bool minic_riscv32_abi_place(MinicRiscv32AbiCursor *cursor, unsigned words,
                            bool unnamed, MinicRiscv32AbiPlacement *location) {
    MinicRiscvGprAbiCursor next;
    MinicRiscvGprAbiPlacement placed;
    if (cursor == NULL || location == NULL) return false;
    next.register_words = cursor->register_words;
    next.stack_words = cursor->stack_words;
    next.variadic_stack_only = cursor->variadic_stack_only;
    if (!minic_riscv_gpr_abi_place(
            &next, words, unnamed, unnamed && words == 2U, &placed))
        return false;
    cursor->register_words = next.register_words;
    cursor->stack_words = next.stack_words;
    cursor->variadic_stack_only = next.variadic_stack_only;
    location->register_begin = placed.register_begin;
    location->register_count = placed.register_count;
    location->stack_begin = placed.stack_begin;
    location->stack_count = placed.stack_count;
    return true;
}
