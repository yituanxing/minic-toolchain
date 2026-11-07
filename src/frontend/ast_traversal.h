#ifndef MINIC_FRONTEND_AST_TRAVERSAL_H
#define MINIC_FRONTEND_AST_TRAVERSAL_H

#include "frontend/ast.h"

/* A read-only visitor receives the ID by value. It may traverse a shared
 * FunctionBody without a write (including privately owned CALL overflow).
 * A remapper receives a short-lived writable handle; it must not retain the
 * pointer beyond its callback. The two contracts share one edge ordering. */
typedef bool (*MinicExpressionIdReadVisitor)(MinicExpressionId expression_id, void *context);
typedef bool (*MinicExpressionIdRefVisitor)(MinicExpressionId *expression_id, void *context);

/* Visit immutable logical parent-to-child edges without writing source AST. */
bool minic_c0_expression_visit_child_ids(const MinicExpression *expression,
                                         MinicExpressionIdReadVisitor visitor,
                                         void *context);

/* Rewrite logical child edges; only AST transformations may use this API. */
bool minic_c0_expression_remap_child_ids(MinicExpression *expression,
                                         MinicExpressionIdRefVisitor visitor,
                                         void *context);

/* Visit ExpressionId roots/graph references that are not expression-node child edges. */
bool minic_c0_program_visit_external_expression_id_refs(MinicC0Program *program,
                                                        MinicExpressionIdRefVisitor visitor,
                                                        void *context);

/* Validate then atomically remap every external ExpressionId through one old-to-new map. */
bool minic_c0_program_remap_external_expression_ids(MinicC0Program *program,
                                                    const MinicExpressionId *mapping,
                                                    size_t mapping_count);

#endif
