#ifndef MINIC_CORE_VALUE_USES_H
#define MINIC_CORE_VALUE_USES_H
#include "core/core_ir.h"
#include <stdbool.h>
/* Pure Core-value operand visitation, no physical registers or stack slots.
 * Unknown instruction forms are rejected instead of guessed. */
typedef bool (*MinicCoreValueUseVisitor)(MinicCoreValueId, void *);
bool minic_core_visit_instruction_value_uses(const MinicCoreFunction *,
    const MinicCoreInstruction *, MinicCoreValueUseVisitor, void *);
bool minic_core_visit_terminator_value_uses(const MinicCoreTerminator *,
    MinicCoreValueUseVisitor, void *);
#endif
