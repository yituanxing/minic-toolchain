#ifndef MINIC_FRONTEND_CONST_EVAL_H
#define MINIC_FRONTEND_CONST_EVAL_H

#include "frontend/ast.h"
#include "target/source_policy/source_info.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct MinicConstValue {
    MinicType type;
    uint64_t bits;
} MinicConstValue;

bool minic_const_eval_integer(const MinicC0Program *program,
                              const MinicTargetInvocation *target,
                              MinicExpressionId expression_id,
                              MinicConstValue *value);
bool minic_const_eval_arithmetic_to_integer(const MinicC0Program *program,
                                            const MinicTargetInvocation *target,
                                            MinicExpressionId expression_id,
                                            MinicType destination_type,
                                            MinicConstValue *value);
bool minic_const_value_convert_integer(const MinicC0Program *program,
                                       const MinicTargetInvocation *target,
                                       const MinicConstValue *source,
                                       MinicType type,
                                       MinicConstValue *result);
bool minic_const_value_integer_representable_in_type(const MinicC0Program *program,
                                                     const MinicTargetInvocation *target,
                                                     const MinicConstValue *value,
                                                     MinicType destination_type);
bool minic_const_value_is_zero(const MinicC0Program *program,
                               const MinicTargetInvocation *target,
                               const MinicConstValue *value,
                               bool *is_zero);
bool minic_const_value_as_int64(const MinicC0Program *program,
                                const MinicTargetInvocation *target,
                                const MinicConstValue *value,
                                int64_t *result);

#endif
