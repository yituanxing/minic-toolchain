#ifndef MINIC_TARGET_BACKEND_CORE_USAGE_H
#define MINIC_TARGET_BACKEND_CORE_USAGE_H

#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stddef.h>

/* Opt-in, read-only inventory of a Core translation unit; operation counts
 * are NOT verified backend capability nor a new legalizer/ABI contract.
 * Keep its implementation compiled once while each backend can opt into
 * the same diagnostics from its own target emission callback. */
void minic_backend_core_usage_dump(
    const char *arch, const MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count);

#endif
