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
bool minic_multi_indirect_record(const MinicC0Program *program,
                                 const MinicTargetInfo *target, MinicType type,
                                 size_t *bytes);

#endif
