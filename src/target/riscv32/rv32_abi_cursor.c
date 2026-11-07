#include "target/riscv32/rv32_abi_cursor.h"
#include "target/riscv/gpr_abi_cursor.h"

/* ILP32 policy: only unnamed two-word values aligned to 2*XLEN (8 bytes)
 * require an even pair. Records with size 8 but alignment 1 or 4 retain
 * sequential aN/aN+1 placement. The same cursor owns stack spill rules. */
bool minic_riscv32_abi_place(MinicRiscv32AbiCursor *cursor, unsigned words,
                            bool unnamed, size_t value_alignment,
                            MinicRiscv32AbiPlacement *location) {
    MinicRiscvGprAbiCursor next;
    MinicRiscvGprAbiPlacement placed;
    if (cursor == NULL || location == NULL || value_alignment == 0U)
        return false;
    next.register_words = cursor->register_words;
    next.stack_words = cursor->stack_words;
    next.variadic_stack_only = cursor->variadic_stack_only;
    if (!minic_riscv_gpr_abi_place(
            &next, words, unnamed,
            unnamed && words == 2U && value_alignment >= 8U, &placed))
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
