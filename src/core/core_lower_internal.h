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
    MinicCoreFunction *function;
    MinicCoreBlockId block_id;
    MinicCoreBlockId break_target;
    MinicCoreObjectId *local_objects;
    MinicCoreLocalIntegerConstant *local_integer_constants;
    unsigned int cfg_constant_call_depth;
    MinicCoreBlockId *statement_blocks;
    size_t statement_block_count;
} MinicCoreLowerContext;

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

#endif
