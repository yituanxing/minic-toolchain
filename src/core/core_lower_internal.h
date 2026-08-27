#ifndef MINIC_CORE_CORE_LOWER_INTERNAL_H
#define MINIC_CORE_CORE_LOWER_INTERNAL_H

#include "core/core_lower.h"

typedef struct MinicCoreLowerContext {
    const MinicFunctionBodyView *body;
    const MinicFunction *source_function;
    const MinicTargetInfo *target;
    MinicCoreFunction *function;
    MinicCoreBlockId block_id;
    MinicCoreBlockId break_target;
    MinicCoreObjectId *local_objects;
    MinicCoreBlockId *statement_blocks;
    size_t statement_block_count;
} MinicCoreLowerContext;

bool core_capture_enum_metadata(MinicCoreLowerContext *context);
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
