#ifndef MINIC_TARGET_RISCV64_ABI_H
#define MINIC_TARGET_RISCV64_ABI_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum MinicRiscv64AbiValueKind {
    MINIC_RISCV64_ABI_VALUE_INVALID = 0,
    MINIC_RISCV64_ABI_VALUE_VOID,
    MINIC_RISCV64_ABI_VALUE_IGNORE,
    MINIC_RISCV64_ABI_VALUE_INTEGER,
    MINIC_RISCV64_ABI_VALUE_FLOAT,
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

bool minic_riscv64_classify_abi_value(const MinicC0Program *program,
                                      MinicType type,
                                      MinicRiscv64AbiValue *result);

void minic_riscv64_abi_cursor_initialize(MinicRiscv64AbiCursor *cursor);
bool minic_riscv64_abi_classify_value(const MinicC0Program *program,
                                      MinicType type,
                                      MinicRiscv64AbiValue *value);
bool minic_riscv64_abi_place_argument(const MinicC0Program *program,
                                      MinicType type,
                                      bool is_fixed_parameter,
                                      MinicRiscv64AbiCursor *cursor,
                                      MinicRiscv64AbiArgumentLocation *location);

#endif
