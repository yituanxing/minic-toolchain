#ifndef MINIC_TARGET_RISCV32_ABI_CURSOR_H
#define MINIC_TARGET_RISCV32_ABI_CURSOR_H
#include <stdbool.h>
#include <stddef.h>
/* One ILP32 psABI machine-placement owner for frame and direct/indirect
 * calls. Named 2*XLEN arguments may straddle a7/stack; unnamed wide
 * variadic arguments require even register pairs or aligned stack. */
typedef struct MinicRiscv32AbiCursor {
    size_t register_words, stack_words;
    bool variadic_stack_only;
} MinicRiscv32AbiCursor;
typedef struct MinicRiscv32AbiPlacement {
    size_t register_begin, register_count, stack_begin, stack_count;
} MinicRiscv32AbiPlacement;
bool minic_riscv32_abi_place(MinicRiscv32AbiCursor *, unsigned words,
                            bool unnamed, MinicRiscv32AbiPlacement *);
#endif
