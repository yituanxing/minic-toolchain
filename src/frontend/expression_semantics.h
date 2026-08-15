#ifndef MINIC_FRONTEND_EXPRESSION_SEMANTICS_H
#define MINIC_FRONTEND_EXPRESSION_SEMANTICS_H

#include "frontend/ast.h"
#include "target/target_info.h"

typedef struct MinicC0ConditionalResolution {
    MinicType result_type;
    bool convert_true;
    bool convert_false;
} MinicC0ConditionalResolution;

bool minic_c0_integer_assignment_value_type(const MinicC0Program *program,
                                            MinicType target_type,
                                            MinicExpressionId expression_id,
                                            MinicType *result_type);
bool minic_c0_integer_comparison_operand_type(const MinicC0Program *program,
                                              const MinicTargetInfo *target_info,
                                              MinicExpressionId expression_id,
                                              MinicType *operand_type);
bool minic_c0_resolve_conditional_expression(const MinicC0Program *program,
                                             const MinicTargetInfo *target,
                                             MinicExpressionId condition_id,
                                             MinicExpressionId true_id,
                                             MinicExpressionId false_id,
                                             MinicC0ConditionalResolution *resolution);

#endif
