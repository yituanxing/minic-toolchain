#include "target/riscv64/rv32_abi_cursor.h"
#include <stdint.h>
bool minic_riscv32_abi_place(MinicRiscv32AbiCursor *cursor, unsigned words,
                            bool unnamed, MinicRiscv32AbiPlacement *location) {
    MinicRiscv32AbiCursor next;
    MinicRiscv32AbiPlacement loc;
    size_t available;
    if (!cursor || !location || (words != 1U && words != 2U) ||
        cursor->register_words > 8U ||
        cursor->stack_words > SIZE_MAX - 2U) return false;
    next = *cursor;
    if (unnamed && words == 2U && !next.variadic_stack_only) {
        if ((next.register_words & 1U) != 0U) ++next.register_words;
        if (next.register_words > 6U) next.register_words = 8U;
    }
    if (next.variadic_stack_only) next.register_words = 8U;
    available = 8U - next.register_words;
    loc.register_begin = next.register_words;
    loc.register_count = words < available ? words : available;
    if (unnamed && words == 2U && loc.register_count != 2U)
        loc.register_count = 0U;
    loc.stack_count = words - loc.register_count;
    if (unnamed && words == 2U && loc.stack_count &&
        (next.stack_words & 1U)) ++next.stack_words;
    if (next.stack_words > SIZE_MAX - loc.stack_count) return false;
    loc.stack_begin = next.stack_words;
    next.register_words += loc.register_count;
    next.stack_words += loc.stack_count;
    if (unnamed && loc.stack_count) next.variadic_stack_only = true;
    *cursor = next;
    *location = loc;
    return true;
}
