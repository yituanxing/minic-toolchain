#ifndef MINIC_TARGET_COMMON_AGGREGATE_SHAPE_H
#define MINIC_TARGET_COMMON_AGGREGATE_SHAPE_H

#include "frontend/ast.h"
#include "target/data_layout.h"
#include <stdbool.h>
#include <stddef.h>

/* Shared storage *facts*, never a calling convention. RV32 = 4-byte
 * ABI word; all four 64-bit targets = 8 bytes. SysV, RISC-V, AAPCS64 and
 * LoongArch still classify FPR/GPR/stack, sret and variadics separately. */
typedef struct MinicAggregateStorageShape {
    size_t bytes;
    size_t alignment;
    size_t words;
} MinicAggregateStorageShape;

bool minic_common_aggregate_storage_shape(
    const MinicC0Program *, const MinicDataLayout *, MinicType,
    size_t word_bytes, MinicAggregateStorageShape *);

/* Shape only: recursively integral/pointer members. Not evidence that the
 * psABI passes a record through GP registers, even if it occupies 1 word. */
bool minic_common_aggregate_integral_tree(const MinicC0Program *, MinicType);
#endif
