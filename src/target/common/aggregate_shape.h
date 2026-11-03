#ifndef MINIC_TARGET_COMMON_AGGREGATE_SHAPE_H
#define MINIC_TARGET_COMMON_AGGREGATE_SHAPE_H

#include "core/core_type.h"
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

/* Production CoreType ingress: a complete nominal RECORD carries the exact
 * target-resolved byte extent and alignment. No Program or C DataLayout may
 * be queried on this path. */
bool minic_common_aggregate_storage_shape_core(
    const MinicCoreTypeContext *, MinicCoreTypeId,
    size_t word_bytes, MinicAggregateStorageShape *);

#endif
