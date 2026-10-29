#ifndef MINIC_TARGET_X86_64_ABI_H
#define MINIC_TARGET_X86_64_ABI_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* SysV x86-64 target-specific ABI classification. This interface does not
 * assert compatibility with the LP64 strategies used by other targets.
 * Machine instruction selection, stack slots and x87 ops stay private. */
bool minic_x86_64_scalar_width(const MinicC0Program *, const MinicTargetInfo *,
                               MinicType, size_t *, bool *);
bool minic_x86_64_storage_width(const MinicC0Program *, const MinicTargetInfo *,
                                MinicType, size_t *, bool *);
bool minic_x86_64_one_gp_record(const MinicC0Program *, const MinicTargetInfo *,
                                MinicType, size_t *);
bool minic_x86_64_two_gp_record(const MinicC0Program *, const MinicTargetInfo *, MinicType);
bool minic_x86_64_record_abi_compatible(MinicType, MinicType);
/* SysV AMD64 MEMORY-class result uses a hidden first GPR destination. */
bool minic_x86_64_memory_result_record(const MinicC0Program *,
                                      const MinicTargetInfo *, MinicType, size_t *);

#endif
