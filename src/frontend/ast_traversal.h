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

/* Visit logical parent-to-child expression edges in one AST expression node. */
bool minic_c0_expression_visit_child_id_refs(MinicExpression *expression,
                                             MinicExpressionIdRefVisitor visitor,
                                             void *context);

/* Visit ExpressionId roots/graph references that are not expression-node child edges. */
bool minic_c0_program_visit_external_expression_id_refs(MinicC0Program *program,
                                                        MinicExpressionIdRefVisitor visitor,
                                                        void *context);

#endif
