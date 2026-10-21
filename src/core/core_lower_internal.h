#ifndef MINIC_CORE_CORE_LOWER_INTERNAL_H
#define MINIC_CORE_CORE_LOWER_INTERNAL_H

#include "core/core_lower.h"
#include "frontend/const_eval.h"

typedef struct MinicCoreLocalIntegerConstant {
    MinicConstValue value;
    MinicConstValue specialization_value;
    bool known;
    bool escaped;
    bool program_zero_checked;
    bool program_zero_stable;
    bool specialization_known;
    bool specialization_modified;
} MinicCoreLocalIntegerConstant;

typedef struct MinicCoreLowerContext {
    const MinicFunctionBodyView *body;
    const MinicFunction *source_function;
    const MinicTargetInfo *target;
    MinicCoreReentryIndex *reentry_index; /* Borrowed from Core Function Set. */
    MinicCoreFunction *function;
    MinicCoreBlockId block_id;
    MinicCoreBlockId break_target;
    MinicCoreObjectId *local_objects;
    MinicCoreLocalIntegerConstant *local_integer_constants;
    unsigned int cfg_constant_call_depth;
    MinicCoreBlockId *statement_blocks;
    size_t statement_block_count;
} MinicCoreLowerContext;

/* Private source-to-Core expression families, deliberately not public ABI.
 * Their effectful CFG and scalar building primitives stay Core-owned. */
MinicCoreLowerStatus minic_core_lower_try_conditional(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled);
MinicCoreLowerStatus minic_core_lower_try_sequenced(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, MinicExpressionId expression_id, bool *handled);
MinicCoreLowerStatus minic_core_lower_try_builtin_unary(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled);
MinicCoreLowerStatus minic_core_lower_try_comparisons(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled);
MinicCoreLowerStatus minic_core_lower_try_pointer_arithmetic(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled);

/* Cross-module Core lowering primitives required by expression families.
 * Namespaced to avoid collisions with frontend, backend or toolchain code. */
MinicCoreLowerStatus minic_core_lower_internal_set_branch(MinicCoreLowerContext *context,
                                       MinicCoreBlockId block_id,
                                       MinicSourceSpan span,
                                       MinicCoreBlockId target);
MinicCoreLowerStatus minic_core_lower_internal_lower_condition_branch(MinicCoreLowerContext *context,
                                                   MinicExpressionId expression_id,
                                                   MinicSourceSpan span,
                                                   MinicCoreBlockId when_true,
                                                   MinicCoreBlockId when_false);
MinicCoreLowerStatus minic_core_lower_internal_reload_scalar_value(MinicCoreLowerContext *context,
                                                MinicSourceSpan span,
                                                MinicType type,
                                                MinicCoreObjectId object_id,
                                                MinicCoreValueId *value_id);
MinicCoreLowerStatus minic_core_lower_internal_append_integer_conversion(MinicCoreLowerContext *context,
                                                      MinicSourceSpan span,
                                                      MinicType target_type,
                                                      MinicCoreValueId source_value,
                                                      MinicCoreValueId *value_id);
MinicCoreLowerStatus minic_core_lower_internal_lower_scalar_assignment_value(MinicCoreLowerContext *context,
                                                          MinicType target_type,
                                                          MinicExpressionId expression_id,
                                                          MinicCoreValueId *value_id);
MinicCoreLowerStatus minic_core_lower_internal_store_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreObjectId object_id,
                                               MinicCoreValueId value_id);
MinicCoreLowerStatus minic_core_lower_internal_lower_expression_statement(MinicCoreLowerContext *context,
                                                       const MinicStatement *statement);
MinicCoreLowerStatus minic_core_lower_internal_spill_scalar_value(MinicCoreLowerContext *context,
                                               MinicSourceSpan span,
                                               MinicType type,
                                               MinicCoreValueId value_id,
                                               MinicCoreObjectId *object_id);
MinicCoreLowerStatus minic_core_lower_internal_lower_scalar_equality_operands(MinicCoreLowerContext *context,
                                                           MinicExpressionId left_id,
                                                           MinicExpressionId right_id,
                                                           MinicCoreValueId *left_value,
                                                           MinicCoreValueId *right_value);
MinicCoreLowerStatus minic_core_lower_internal_lower_integer_binary_operands(MinicCoreLowerContext *context,
                                                          MinicExpressionId left_id,
                                                          MinicExpressionId right_id,
                                                          MinicType result_type,
                                                          MinicCoreValueId *left_value,
                                                          MinicCoreValueId *right_value);
MinicCoreLowerStatus minic_core_lower_internal_lower_floating_binary_operands(MinicCoreLowerContext *context,
                                                         MinicExpressionId left_id,
                                                         MinicExpressionId right_id,
                                                         MinicType result_type,
                                                         MinicCoreValueId *left_value,
                                                         MinicCoreValueId *right_value);
MinicCoreLowerStatus minic_core_lower_internal_append_long_double_binary_helper(
    MinicCoreLowerContext *context,
    MinicSourceSpan span,
    const char *helper,
    MinicCoreValueId left,
    MinicCoreValueId right,
    MinicType return_type,
    MinicCoreValueId *value_id);

/* Private Core-lowering facts: no target-specific machine state. */
void core_local_constant_set(MinicCoreLowerContext *, MinicLocalId,
                             const MinicConstValue *);
bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *,
                                         MinicExpressionId, MinicConstValue *);
bool core_const_signed_value(const MinicCoreLowerContext *,
                             const MinicConstValue *, int64_t *);

/* Conservatively shared between fact oracle and source-to-Core lowering. */
bool minic_core_lower_direct_local_read(const MinicCoreLowerContext *,
                                        MinicExpressionId, MinicLocalId *);
bool minic_core_lower_known_null_pointer(const MinicCoreLowerContext *,
                                         MinicExpressionId);
bool minic_core_lower_boolean_range_expression(const MinicCoreLowerContext *,
                                               MinicExpressionId, unsigned int);
bool minic_core_lower_unreachable_external_reentry(
    const MinicCoreLowerContext *, const MinicStatement *, MinicStatementId);

bool core_capture_enum_metadata(MinicCoreLowerContext *context);
/* Source-wide fail-closed _Bool==0 fact used only by Core CFG lowering. */
bool core_zero_bool_cfg_condition(const MinicCoreLowerContext *context,
                                  MinicExpressionId id,
                                  unsigned int depth,
                                  bool *is_zero);
const MinicDataLayout *core_data_layout(const MinicCoreLowerContext *context);
bool core_unsigned_bit_field_semantic_type(const MinicCoreLowerContext *context, MinicType type);
bool core_bit_field_storage_type(const MinicCoreLowerContext *context,
                                 MinicType value_type,
                                 MinicType *storage_type,
                                 unsigned int *storage_width);
bool core_global_addressable_type(MinicType type);
bool core_memory_scalar_type(MinicType type);
bool core_import_fixed_register_binding(MinicCoreLowerContext *context,
                                        size_t source_binding_id,
                                        size_t *core_binding_id);
bool core_scalar_expression_value_type(const MinicFunctionBodyView *body,
                                       const MinicExpression *expression,
                                       MinicType *value_type);
MinicCoreLowerStatus ensure_statement_block(MinicCoreLowerContext *context,
                                            MinicStatementId statement_id,
                                            MinicCoreBlockId *block_id);
MinicCoreLowerStatus lower_address(MinicCoreLowerContext *context,
                                   MinicExpressionId expression_id,
                                   MinicCoreValueId *address_id);
MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,
                                           MinicSourceSpan span,
                                           MinicType target_type,
                                           MinicCoreValueId source_value,
                                           MinicCoreValueId *value_id);
MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
                                      MinicExpressionId expression_id,
                                      MinicCoreValueId *value_id);
MinicCoreLowerStatus minic_core_lower_inline_asm(MinicCoreLowerContext *context,
                                                 const MinicStatement *statement);

/* C bit-field semantics: one cohesive source-to-Core operation family.
 * The same signedness/width and target DataLayout contracts cover read,
 * compound assignment and increment/decrement. */
MinicCoreLowerStatus minic_core_lower_internal_append_field_address(
    MinicCoreLowerContext *context, MinicSourceSpan span,
    MinicCoreValueId base_id, MinicRecordId record_id, size_t field_index,
    MinicType field_type, MinicCoreValueId *address_id);
MinicCoreLowerStatus minic_core_lower_try_bit_field_read(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *matched);
MinicCoreLowerStatus minic_core_lower_try_bit_field_compound(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *matched);
MinicCoreLowerStatus minic_core_lower_bit_field_update(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    const MinicExpression *operand, bool increment, bool prefix,
    MinicCoreValueId *value_id);

/* The call family owns C argument evaluation, value/object snapshots,
 * indirect callee ordering and Core CALL instructions. ABI registers are
 * placed later in the target subsystem, not by this internal API. */
MinicCoreLowerStatus minic_core_lower_internal_record_materialized_address(
    MinicCoreLowerContext *context, MinicExpressionId expression_id,
    MinicCoreValueId *address_id);
MinicCoreLowerStatus minic_core_lower_direct_call(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id);
MinicCoreLowerStatus minic_core_lower_direct_record_call_object(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreObjectId *result_object);
MinicCoreLowerStatus minic_core_lower_indirect_call(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id);

/* Source-to-Core assignments; target ABI and machine details never enter. */
MinicCoreLowerStatus minic_core_lower_assignment_pair(
    MinicCoreLowerContext *context, MinicExpressionId target_id,
    MinicExpressionId source_id, MinicSourceSpan span,
    MinicCoreValueId *result_value);
MinicCoreLowerStatus minic_core_lower_assignment(
    MinicCoreLowerContext *context, const MinicStatement *statement);
MinicCoreLowerStatus minic_core_lower_scalar_update(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id);
MinicCoreLowerStatus minic_core_lower_try_bit_field_assignment(
    MinicCoreLowerContext *context, const MinicExpression *target,
    MinicExpressionId source_id, MinicSourceSpan span,
    MinicCoreValueId *result_value, bool *matched);

#endif
