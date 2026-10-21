#ifndef MINIC_TARGET_RISCV64_FRAME_REQUIREMENTS_H
#define MINIC_TARGET_RISCV64_FRAME_REQUIREMENTS_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include <stdbool.h>
#include <stddef.h>
/* Read-only Core/psABI requirements; real spill and object-reuse
 * optimization continues in the mature RV64 function codegen. */
bool minic_riscv64_core_function_needs_saved_return_address(const MinicCoreFunction *);
bool minic_riscv64_core_function_uses_structured_inline_asm(const MinicCoreFunction *);
bool minic_riscv64_core_function_uses_dynamic_stack_alloc(const MinicCoreFunction *);
bool minic_riscv64_core_function_uses_variadic_argument_address(const MinicCoreFunction *);
bool minic_riscv64_core_variadic_fixed_prefix(const MinicC0Program *,
                                               const MinicCoreFunction *, size_t *, size_t *);
bool minic_riscv64_core_call_outgoing_stack_size(const MinicC0Program *,
                                                 const MinicCoreFunction *, size_t *);
#endif
