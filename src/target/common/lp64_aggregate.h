#ifndef MINIC_TARGET_COMMON_LP64_AGGREGATE_H
#define MINIC_TARGET_COMMON_LP64_AGGREGATE_H

#include "core/core_type.h"

#include <stdbool.h>
#include <stddef.h>

/* Narrow common ABI predicates verified for AArch64 and LoongArch64, now
 * exclusively from immutable CoreType. x86 SysV and RV psABI still own
 * independent classifiers and machine register placement. */
bool minic_multi_gp_record_core(const MinicCoreTypeContext *, MinicCoreTypeId,
                                size_t *bytes, size_t *eightbytes);
bool minic_multi_empty_record_core(const MinicCoreTypeContext *, MinicCoreTypeId);
bool minic_multi_indirect_record_core(const MinicCoreTypeContext *, MinicCoreTypeId,
                                      size_t *bytes);
bool minic_multi_fp_return_record_core(const MinicCoreTypeContext *, MinicCoreTypeId,
                                       size_t *element_count, size_t *element_width);
#endif
