#ifndef MINIC_TARGET_COMMON_DIRECT_CALL_PREFLIGHT_H
#define MINIC_TARGET_COMMON_DIRECT_CALL_PREFLIGHT_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* Shared admission/planning bound for the two direct LP64 backends. */
#define MINIC_DIRECT_LP64_MAX_CALL_ARGUMENTS 64U

/* ABI aggregate indirection stays target-owned: AAPCS64 differs from LP64D.
 * Validate C/Core call shapes without deciding register/stack placement. */
typedef bool (*MinicDirectLP64IndirectRecord)(
    const MinicC0Program *, const MinicTargetInfo *, MinicType, size_t *);

/* Output the first failed instruction and whether any calls were observed.
 * Machine diagnostics and return-address preservation remain target-owned. */
bool minic_multi_direct_lp64_calls_preflight(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const char *arch, const char *name,
    MinicDirectLP64IndirectRecord indirect_record,
    size_t *failed_instruction, bool *has_calls);

#endif
