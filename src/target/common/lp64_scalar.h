#ifndef MINIC_TARGET_COMMON_LP64_SCALAR_H
#define MINIC_TARGET_COMMON_LP64_SCALAR_H
#include "target/common/direct_lp64_abi.h"
#include <stdbool.h>
#include <stddef.h>
/* Fully resolved execution-type scalar queries. All LP64 target machine
 * consumers use sealed CoreType, never frontend DataLayout. */
bool minic_lp64_core_integer_width(const MinicCoreTypeContext *, MinicCoreTypeId,
                                   size_t *, bool *);
bool minic_lp64_core_kind(const MinicCoreTypeContext *, MinicCoreTypeId,
                          MinicCoreTypeKind);
bool minic_lp64_core_pointer_pointee(const MinicCoreTypeContext *, MinicCoreTypeId,
                                     MinicCoreTypeId *);
bool minic_lp64_core_equal_unqualified(const MinicCoreTypeContext *,
                                       MinicCoreTypeId, MinicCoreTypeId);
bool minic_lp64_core_valid_parameter_type(const MinicCoreTypeContext *, MinicCoreTypeId);
bool minic_lp64_core_valid_return_type(const MinicCoreTypeContext *, MinicCoreTypeId);
/* A target must opt in to an actual record indirect-return convention. */
bool minic_lp64_core_valid_return_with_indirect(
    const MinicCoreTypeContext *, MinicCoreTypeId,
    MinicDirectLP64CoreIndirectRecord);
bool minic_lp64_core_valid_parameter_with_policy(
    const MinicCoreTypeContext *, MinicCoreTypeId,
    const MinicDirectLP64AbiPolicy *);
bool minic_lp64_core_valid_return_with_policy(
    const MinicCoreTypeContext *, MinicCoreTypeId,
    const MinicDirectLP64AbiPolicy *);
#endif
