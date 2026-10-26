#ifndef MINIC_TARGET_RISCV64_FRAME_REQUIREMENTS_H
#define MINIC_TARGET_RISCV64_FRAME_REQUIREMENTS_H
#include "core/core_ir.h"
#include "target/common/core_call_view.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>
/* Read-only Core/psABI requirements; real spill and object-reuse
 * optimization continues in the mature RV64 function codegen. */
bool minic_riscv64_core_variadic_fixed_prefix_profile(
    const MinicC0Program *, const MinicCoreFunction *,
    bool lp64_soft_float, size_t *, size_t *);
bool minic_riscv64_core_variadic_fixed_prefix(const MinicC0Program *,
                                               const MinicCoreFunction *, size_t *, size_t *);
/* Core CALL argument provenance and record identity belong to
 * Common CoreCallView. RV64 retains only ABI parameter placement. */
bool minic_riscv64_core_call_outgoing_stack_size_profile(
    const MinicC0Program *, const MinicCoreFunction *,
    bool lp64_soft_float, const MinicCoreCallIndex *, size_t *);
bool minic_riscv64_core_call_outgoing_stack_size(const MinicC0Program *,
                                                 const MinicCoreFunction *, size_t *);
#endif
