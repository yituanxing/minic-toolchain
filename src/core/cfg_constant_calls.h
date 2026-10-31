#ifndef MINIC_CORE_CFG_CONSTANT_CALLS_H
#define MINIC_CORE_CFG_CONSTANT_CALLS_H
#include "core/core_lower_internal.h"
#include <stdbool.h>
/* Pure-call operand admission: an unsupported shape is NOT a proof. */
bool minic_core_cfg_pure_call_argument(const MinicCoreLowerContext *,
                                       MinicExpressionId, unsigned int);
/* Shared callback for the AST no-effect GNU statement-guard proof. */
bool minic_core_cfg_pure_guard_truth(void *, MinicExpressionId, bool *);
/* Bounded small loops / constant inline call oracle, no target opcode logic. */
bool minic_core_cfg_constant_inline_call(const MinicCoreLowerContext *,
                                         const MinicExpression *,
                                         MinicConstValue *);
#endif
