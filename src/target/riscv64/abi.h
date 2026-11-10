#ifndef MINIC_TARGET_RISCV64_ABI_H
#define MINIC_TARGET_RISCV64_ABI_H

#include "core/core_type.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum MinicRiscv64AbiValueKind {
    MINIC_RISCV64_ABI_VALUE_INVALID = 0,
    MINIC_RISCV64_ABI_VALUE_VOID,
    MINIC_RISCV64_ABI_VALUE_IGNORE,
    MINIC_RISCV64_ABI_VALUE_INTEGER,
    MINIC_RISCV64_ABI_VALUE_FLOAT,
    MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR,
    MINIC_RISCV64_ABI_VALUE_AGGREGATE,
    MINIC_RISCV64_ABI_VALUE_INDIRECT
} MinicRiscv64AbiValueKind;

typedef struct MinicRiscv64AbiValue {
    MinicRiscv64AbiValueKind kind;
    size_t storage_size;
    size_t register_chunks;
    size_t slot_count;
} MinicRiscv64AbiValue;

typedef struct MinicRiscv64AbiCursor {
    size_t integer_register_count;
    size_t floating_register_count;
    size_t stack_slot_count;
    bool lp64_soft_float; /* per-target, not a process global */
} MinicRiscv64AbiCursor;

typedef struct MinicRiscv64AbiArgumentLocation {
    MinicRiscv64AbiValue value;
    size_t integer_register_begin;
    size_t integer_register_count;
    size_t floating_register_begin;
    size_t floating_register_count;
    size_t stack_slot_begin;
    size_t stack_slot_count;
} MinicRiscv64AbiArgumentLocation;

void minic_riscv64_abi_cursor_initialize(MinicRiscv64AbiCursor *cursor);
/* Shared RV64 named-prefix cursor validation for variadic functions. */
bool minic_riscv64_abi_variadic_prefix_from_cursor(
    const MinicRiscv64AbiCursor *, size_t *, size_t *);
/* Canonical Core-owned execution types. No source-C placement API remains. */
bool minic_riscv64_abi_classify_core_value_profile(
    const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    bool lp64_soft_float, MinicRiscv64AbiValue *value);
bool minic_riscv64_abi_cursor_initialize_for_core_return_profile(
    const MinicCoreTypeContext *types, MinicCoreTypeId return_type,
    bool lp64_soft_float, MinicRiscv64AbiCursor *cursor,
    MinicRiscv64AbiValue *return_value);
bool minic_riscv64_abi_place_core_argument(
    const MinicCoreTypeContext *types, MinicCoreTypeId type_id,
    bool is_fixed_parameter, MinicRiscv64AbiCursor *cursor,
    MinicRiscv64AbiArgumentLocation *location);

#endif
