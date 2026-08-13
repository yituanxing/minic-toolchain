#ifndef MINIC_FRONTEND_AST_TRAVERSAL_H
#define MINIC_FRONTEND_AST_TRAVERSAL_H

#include "frontend/ast.h"

typedef bool (*MinicExpressionIdRefVisitor)(MinicExpressionId *expression_id, void *context);

bool minic_c0_expression_visit_child_id_refs(MinicExpression *expression,
                                             MinicExpressionIdRefVisitor visitor,
                                             void *context);

bool minic_c0_program_visit_external_expression_id_refs(MinicC0Program *program,
                                                        MinicExpressionIdRefVisitor visitor,
                                                        void *context);

#endif
