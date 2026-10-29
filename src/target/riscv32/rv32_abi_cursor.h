#ifndef MINIC_TARGET_RISCV32_ABI_CURSOR_H
#define MINIC_TARGET_RISCV32_ABI_CURSOR_H
#include <stdbool.h>
#include <stddef.h>
/* One ILP32 psABI machine-placement owner for frame and direct/indirect
 * calls. Named 2*XLEN arguments may straddle a7/stack; only unnamed
 * values with both width 2*XLEN and natural alignment 2*XLEN require an
 * even register pair. A size-8, alignment-4 record must not skip a1. */
typedef struct MinicRiscv32AbiCursor {
    size_t register_words, stack_words;
    bool variadic_stack_only;
} MinicRiscv32AbiCursor;
typedef struct MinicRiscv32AbiPlacement {
    size_t register_begin, register_count, stack_begin, stack_count;
} MinicRiscv32AbiPlacement;
bool minic_riscv32_abi_place(MinicRiscv32AbiCursor *, unsigned words,
                            bool unnamed, size_t value_alignment,
                            MinicRiscv32AbiPlacement *);
#endif
