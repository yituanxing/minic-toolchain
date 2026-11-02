#ifndef MINIC_TARGET_COMMON_LP64_SCALAR_H
#define MINIC_TARGET_COMMON_LP64_SCALAR_H
#include "core/core_type.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include "target/common/direct_call_plan.h"
#include <stdbool.h>
#include <stddef.h>
/* Narrow admission predicates proven in AArch64/LoongArch64; target ABI
 * register assignment and lowering stay target-owned. */
bool minic_lp64_valid_int(MinicType type);
bool minic_lp64_valid_int_or_bool(MinicType type);
bool minic_lp64_scalar_integer_width(const MinicC0Program *, const MinicTargetInfo *, MinicType, size_t *, bool *);
/* Fully resolved execution-type scalar queries. Core consumers with a bound
 * type arena must use these, never reopen the frontend DataLayout. */
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
typedef bool (*MinicDirectLP64CoreIndirectRecord)(
    const MinicCoreTypeContext *, MinicCoreTypeId, size_t *);
bool minic_lp64_core_valid_return_with_indirect(
    const MinicCoreTypeContext *, MinicCoreTypeId,
    MinicDirectLP64CoreIndirectRecord);
bool minic_lp64_register64_type(const MinicC0Program *, const MinicTargetInfo *, MinicType);
bool minic_lp64_valid_parameter_type(const MinicC0Program *, const MinicTargetInfo *, MinicType);
bool minic_lp64_valid_return_type(const MinicC0Program *, const MinicTargetInfo *, MinicType);
/* Indirect aggregate return admission is opt-in per machine ABI.  NULL is
 * deliberately fail-closed: the LoongArch64 and AAPCS64 sret conventions
 * cannot be inferred from the shared LP64 argument layout. */
bool minic_lp64_valid_return_with_indirect(const MinicC0Program *,
    const MinicTargetInfo *, MinicType, MinicDirectLP64IndirectRecord);
#endif
