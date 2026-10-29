#ifndef MINIC_TARGET_COMMON_DIRECT_LP64_ABI_H
#define MINIC_TARGET_COMMON_DIRECT_LP64_ABI_H

#include <stdbool.h>
#include <stddef.h>

/* These describe physical ABI behavior, not architecture identities. Two
 * targets with matching pointer widths need not use the same call ABI. */
typedef enum MinicDirectLP64RecordRegisters {
    MINIC_DIRECT_LP64_RECORD_WHOLE_OR_STACK,
    MINIC_DIRECT_LP64_RECORD_SPLIT_WITH_STACK
} MinicDirectLP64RecordRegisters;

typedef enum MinicDirectLP64WideRegisters {
    MINIC_DIRECT_LP64_WIDE_EVEN_PAIR_OR_STACK,
    MINIC_DIRECT_LP64_WIDE_SPLIT_WITH_STACK
} MinicDirectLP64WideRegisters;

typedef enum MinicDirectLP64VariadicCursor {
    MINIC_DIRECT_LP64_VARIADIC_ABI_STATE,
    MINIC_DIRECT_LP64_VARIADIC_LINEAR_GP
} MinicDirectLP64VariadicCursor;

typedef struct MinicDirectLP64AbiPolicy {
    const char *name;
    size_t gp_argument_registers;
    size_t fp_argument_registers;
    size_t stack_slot_bytes;
    size_t stack_alignment;
    /* Zero means a dedicated hidden-result register; one consumes a0. */
    size_t indirect_result_argument_registers;
    MinicDirectLP64RecordRegisters record_registers;
    MinicDirectLP64WideRegisters wide_registers;
    bool long_double_in_fp_register;
    bool variadic_floating_in_fp_register;
    bool variadic_named_double_supported;
    /* Unnamed 2*GRLEN-aligned scalar values must start at an even GAR.
     * This is independent of whether named wide scalars may split at a7. */
    bool variadic_align_wide_gp_pair;
    MinicDirectLP64VariadicCursor variadic_cursor;
    size_t variadic_save_bytes;
    size_t variadic_save_alignment;
    size_t extra_variadic_cursor_bytes;
    bool variadic_requires_public_stack_alignment;
} MinicDirectLP64AbiPolicy;

/* Qualified direct-LP64 contract, deliberately not a generic all-ISA ABI. */
static inline bool minic_direct_lp64_abi_policy_valid(
    const MinicDirectLP64AbiPolicy *abi) {
    return abi != NULL && abi->name != NULL && abi->name[0] != '\0' &&
        abi->gp_argument_registers == 8U &&
        abi->fp_argument_registers == 8U &&
        abi->stack_slot_bytes == 8U && abi->stack_alignment == 16U &&
        abi->indirect_result_argument_registers <= 1U &&
        (abi->record_registers == MINIC_DIRECT_LP64_RECORD_WHOLE_OR_STACK ||
         abi->record_registers == MINIC_DIRECT_LP64_RECORD_SPLIT_WITH_STACK) &&
        (abi->wide_registers == MINIC_DIRECT_LP64_WIDE_EVEN_PAIR_OR_STACK ||
         abi->wide_registers == MINIC_DIRECT_LP64_WIDE_SPLIT_WITH_STACK) &&
        abi->variadic_save_alignment >= abi->stack_slot_bytes &&
        abi->variadic_save_alignment <= abi->stack_alignment &&
        (abi->variadic_save_alignment &
         (abi->variadic_save_alignment - 1U)) == 0U &&
        abi->variadic_save_bytes % abi->stack_slot_bytes == 0U &&
        (abi->variadic_cursor == MINIC_DIRECT_LP64_VARIADIC_ABI_STATE ||
         abi->variadic_cursor == MINIC_DIRECT_LP64_VARIADIC_LINEAR_GP) &&
        abi->variadic_save_bytes >= abi->gp_argument_registers * 8U &&
        (abi->variadic_cursor == MINIC_DIRECT_LP64_VARIADIC_ABI_STATE
            ? abi->extra_variadic_cursor_bytes >= 32U &&
              abi->extra_variadic_cursor_bytes % abi->stack_alignment == 0U &&
              !abi->variadic_requires_public_stack_alignment
            : abi->extra_variadic_cursor_bytes == 0U &&
              !abi->long_double_in_fp_register &&
              !abi->variadic_floating_in_fp_register);
}

#endif
