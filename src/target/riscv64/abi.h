#ifndef MINIC_TARGET_RISCV64_ABI_H
#define MINIC_TARGET_RISCV64_ABI_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum MinicRiscv64AbiValueKind {
    MINIC_RISCV64_ABI_VALUE_IGNORE = 0,
    MINIC_RISCV64_ABI_VALUE_INTEGER,
    MINIC_RISCV64_ABI_VALUE_FLOAT,
    MINIC_RISCV64_ABI_VALUE_AGGREGATE,
    MINIC_RISCV64_ABI_VALUE_INDIRECT
} MinicRiscv64AbiValueKind;

typedef struct MinicRiscv64AbiValue {
    MinicRiscv64AbiValueKind kind;
    size_t storage_size;
    size_t register_chunks;
} MinicRiscv64AbiValue;

bool minic_riscv64_classify_abi_value(const MinicC0Program *program,
                                      MinicType type,
                                      MinicRiscv64AbiValue *result);

#endif
