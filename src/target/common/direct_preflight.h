#ifndef MINIC_TARGET_COMMON_DIRECT_PREFLIGHT_H
#define MINIC_TARGET_COMMON_DIRECT_PREFLIGHT_H

#include "core/core_ir.h"
#include "target/common/core_function_facts.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* The two direct LP64 emitters share identical function/CFG admission rules.
 * This is a capability validator, not an ABI classifier or machine emitter. */
bool minic_multi_direct_lp64_function_preflight(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts, const char *arch, const char *name);

/* Call only after function admission. On rejection, report the failing
 * Core block; all machine diagnostics stay with the caller's backend. */
bool minic_multi_direct_lp64_cfg_preflight(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, size_t *failed_block);

#endif
