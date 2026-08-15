#ifndef MINIC_FRONTEND_EXPRESSION_SEMANTICS_H
#define MINIC_FRONTEND_EXPRESSION_SEMANTICS_H

#include "frontend/ast.h"
#include "target/target_info.h"

bool minic_c0_integer_assignment_value_type(const MinicC0Program *program,
                                            MinicType target_type,
                                            MinicExpressionId source_expression_id,
                                            MinicType *result);
bool minic_c0_integer_comparison_operand_type(const MinicC0Program *program,
                                              const MinicTargetInfo *target,
                                              MinicExpressionId expression_id,
                                              MinicType *result);
bool minic_c0_conditional_result_type(const MinicC0Program *program,
                                      const MinicTargetInfo *target,
                                      MinicExpressionId when_true_expression_id,
                                      MinicExpressionId when_false_expression_id,
                                      MinicType *result);

#endif
