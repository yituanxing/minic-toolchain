#ifndef MINIC_CORE_VALUE_USES_H
#define MINIC_CORE_VALUE_USES_H
#include "core/core_ir.h"
#include <stdbool.h>
#include <stddef.h>
/* Pure Core-value operand visitation, no physical registers or stack slots.
 * Unknown instruction forms are rejected instead of guessed. */
typedef bool (*MinicCoreValueUseVisitor)(MinicCoreValueId, void *);
bool minic_core_visit_instruction_value_uses(const MinicCoreFunction *,
    const MinicCoreInstruction *, MinicCoreValueUseVisitor, void *);
bool minic_core_visit_terminator_value_uses(const MinicCoreTerminator *,
    MinicCoreValueUseVisitor, void *);

/* Count *semantic* uses of every Core SSA value once per function, including
 * all block terminators and duplicate operands (x+x has two uses). Caller
 * owns `counts` with capacity >= function->value_count. This is a pure Core
 * fact: register/ABI/stack policy remains target-owned. Fail on malformed
 * block indices, out-of-range values, or overflowing counts. */
bool minic_core_function_value_use_counts(const MinicCoreFunction *,
    size_t *counts, size_t capacity);
#endif
