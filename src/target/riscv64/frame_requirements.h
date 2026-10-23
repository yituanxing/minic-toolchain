#ifndef MINIC_TARGET_RISCV64_FRAME_REQUIREMENTS_H
#define MINIC_TARGET_RISCV64_FRAME_REQUIREMENTS_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>
/* Read-only Core/psABI requirements; real spill and object-reuse
 * optimization continues in the mature RV64 function codegen. */
bool minic_riscv64_core_variadic_fixed_prefix(const MinicC0Program *,
                                               const MinicCoreFunction *, size_t *, size_t *);
/* The psABI argument type used for outgoing-area sizing must be identical
 * to the one used by both direct/indirect assembly marshaling paths. */
bool minic_riscv64_core_call_argument_type(
    const MinicCoreFunction *function,
    const MinicCoreCallArgument *argument,
    const MinicType *parameter_types, size_t parameter_count,
    bool is_variadic, size_t argument_index,
    MinicType *argument_type, bool *is_fixed_parameter);
bool minic_riscv64_core_call_outgoing_stack_size_profile(
    const MinicC0Program *, const MinicCoreFunction *,
    bool lp64_soft_float, size_t *);
bool minic_riscv64_core_call_outgoing_stack_size(const MinicC0Program *,
                                                 const MinicCoreFunction *, size_t *);
#endif
