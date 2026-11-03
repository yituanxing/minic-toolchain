#ifndef MINIC_FRONTEND_EXPRESSION_SEMANTICS_H
#define MINIC_FRONTEND_EXPRESSION_SEMANTICS_H

#include "frontend/ast.h"
#include "target/source_policy/source_info.h"

/* Exactly one Frontend semantic owner for GNU output lvalues.
 * C assignment and source ASM tied hard-register outputs have distinct
 * permissions. Parser and AST verifier MUST both consume this same rule.
 * Invocation owns source constraint policy; machine target owns ISA register
 * recognition. Neither is inferred from the other. */
/* Source-only GNU memory operand admission: target-described m/Q grammar
 * references addressable C objects, including aggregates. This does NOT
 * authorize emitting any machine template or clobber pattern. */
bool minic_c0_inline_asm_input_value_supported(
    const MinicTargetInvocation *, const MinicExpression *,
    const char *constraint, size_t constraint_length);

bool minic_c0_inline_asm_output_value_supported(
    const MinicC0Program *, const MinicTargetInvocation *,
    const MinicExpression *, const char *constraint, size_t constraint_length);

bool minic_c0_integer_assignment_value_type(const MinicC0Program *program,
                                            MinicType target_type,
                                            MinicExpressionId source_expression_id,
                                            MinicType *result);
bool minic_c0_integer_range_representable_in_type(const MinicC0Program *program,
                                                  const MinicTargetInvocation *target,
                                                  MinicType source_type,
                                                  MinicType destination_type);
bool minic_c0_conditional_result_type(const MinicC0Program *program,
                                      const MinicTargetInvocation *target,
                                      MinicExpressionId when_true_expression_id,
                                      MinicExpressionId when_false_expression_id,
                                      MinicType *result);
/* C null pointer constant: an integer constant expression evaluating to 0,
 * or that expression cast to void*. Target-aware because the constant
 * evaluator must honor the source integer model and casts. */
bool minic_c0_expression_is_null_pointer_constant(
    const MinicC0Program *program, const MinicTargetInvocation *target,
    MinicExpressionId expression_id);

bool minic_c0_pointer_arithmetic_element_size(const MinicC0Program *program,
                                              const MinicTargetInvocation *invocation,
                                              MinicType pointer_type,
                                              size_t *element_size);

#endif
