#ifndef MINIC_FRONTEND_CONST_EVAL_H
#define MINIC_FRONTEND_CONST_EVAL_H

#include "frontend/ast.h"
#include "target/target_info.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct MinicConstValue {
    MinicType type;
    uint64_t bits;
} MinicConstValue;

bool minic_const_eval_integer(const MinicC0Program *program,
                              const MinicTargetInfo *target,
                              MinicExpressionId expression_id,
                              MinicConstValue *value);
bool minic_const_value_is_zero(const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicConstValue *value,
                               bool *is_zero);
bool minic_const_value_as_int64(const MinicC0Program *program,
                                const MinicTargetInfo *target,
                                const MinicConstValue *value,
                                int64_t *result);

#endif
