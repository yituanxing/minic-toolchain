#ifndef MINIC_TARGET_COMMON_LP64_AGGREGATE_H
#define MINIC_TARGET_COMMON_LP64_AGGREGATE_H

#include "frontend/ast.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stddef.h>

/* Narrow common ABI predicates verified for AArch64 and LoongArch64.
 * Not a universal LP64 calling convention: x86 SysV and RV psABI retain
 * independent classifiers. Never use this to infer HFA/vector handling. */
bool minic_multi_gp_record(const MinicC0Program *program,
                           const MinicTargetInfo *target, MinicType type,
                           size_t *bytes, size_t *eightbytes);
/* GNU C permits complete, zero-byte records. AAPCS64 and LoongArch LP64D
 * pass them without consuming any argument register or stack slot. */
bool minic_multi_empty_record(const MinicC0Program *program,
                              const MinicTargetInfo *target, MinicType type);
bool minic_multi_indirect_record(const MinicC0Program *program,
                                 const MinicTargetInfo *target, MinicType type,
                                 size_t *bytes);

/* Strict 1-2 member homogeneous float/double record return shape shared
 * by AAPCS64 and LP64D. Excludes vectors, arrays, nested/packed records,
 * unions, bitfields, padded and long-double HFA. Native FPR assignment is
 * target-local. */
bool minic_multi_fp_return_record(
    const MinicC0Program *, const MinicTargetInfo *, MinicType,
    size_t *element_count, size_t *element_width);
#endif
