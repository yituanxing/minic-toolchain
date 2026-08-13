#ifndef MINIC_FRONTEND_AST_TRAVERSAL_H
#define MINIC_FRONTEND_AST_TRAVERSAL_H

#include "frontend/ast.h"

/*
 * The visited pointer is a short-lived mutable handle slot. A visitor may read or replace the
 * ExpressionId but must not retain the pointer after the callback returns. The traversal
 * implementation is therefore free to use an arena field today or a temporary slot for another
 * physical representation later.
 *
 * 被访问的指针只是一次回调期间有效的可变 handle 槽位。visitor 可以读取或替换
 * ExpressionId，但不能保存该指针；因此未来底层表示可以从当前 arena 字段切换为临时槽位。
 */
typedef bool (*MinicExpressionIdRefVisitor)(MinicExpressionId *expression_id, void *context);
typedef bool (*MinicExpressionIdVisitor)(MinicExpressionId expression_id, void *context);

typedef struct MinicFunctionBodyView {
    const MinicC0Program *program;
    MinicFunctionId function_id;
} MinicFunctionBodyView;

/* Visit logical parent-to-child expression edges in one AST expression node. */
bool minic_c0_expression_visit_child_id_refs(MinicExpression *expression,
                                             MinicExpressionIdRefVisitor visitor,
                                             void *context);

/* Read-only form of the same logical child-edge traversal. */
bool minic_c0_expression_visit_child_ids(const MinicExpression *expression,
                                         MinicExpressionIdVisitor visitor,
                                         void *context);

/* Visit ExpressionId roots/graph references that are not expression-node child edges. */
bool minic_c0_program_visit_external_expression_id_refs(MinicC0Program *program,
                                                        MinicExpressionIdRefVisitor visitor,
                                                        void *context);

/* Validate then atomically remap every external ExpressionId through one old-to-new map. */
bool minic_c0_program_remap_external_expression_ids(MinicC0Program *program,
                                                    const MinicExpressionId *mapping,
                                                    size_t mapping_count);

/*
 * FunctionBody is a logical ownership view over the current Program representation. It does not
 * promise that function-local nodes occupy contiguous Program ranges, except for the existing
 * LocalId range contract already stored by MinicFunction.
 */
bool minic_c0_function_body_view(const MinicC0Program *program,
                                 MinicFunctionId function_id,
                                 MinicFunctionBodyView *view);
const MinicFunction *minic_c0_function_body_function(const MinicFunctionBodyView *view);
MinicBlockId minic_c0_function_body_root_block(const MinicFunctionBodyView *view);
bool minic_c0_function_body_owns_local(const MinicFunctionBodyView *view, MinicLocalId local_id);

/* Verify structural function-body ownership and function-local semantic references. */
bool minic_c0_program_validate_function_body_ownership(const MinicC0Program *program);

#endif
