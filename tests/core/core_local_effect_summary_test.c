/* White-box AST effect-summary contract. This deliberately constructs the
 * smallest source tree needed to validate address and GNU-tail traversal. */
#include "core/core_lower_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

const MinicExpression *minic_c0_program_expression(
    const MinicC0Program *program, MinicExpressionId id) {
    return program != NULL && id < program->expression_count
               ? &program->expressions[id] : NULL;
}

const MinicStatement *minic_c0_program_statement(
    const MinicC0Program *program, MinicStatementId id) {
    return program != NULL && id < program->statement_count
               ? &program->statements[id] : NULL;
}

const MinicBlock *minic_c0_program_block(
    const MinicC0Program *program, MinicBlockId id) {
    return program != NULL && id < program->block_count
               ? &program->blocks[id] : NULL;
}

int main(void) {
    MinicExpression expressions[6];
    MinicStatement statements[2];
    MinicStatementId statement_ids[2] = {0U, 1U};
    MinicBlock blocks[2];
    MinicC0Program program;
    MinicFunctionBodyView body;
    MinicCoreLowerContext context;

    (void)memset(expressions, 0, sizeof(expressions));
    (void)memset(statements, 0, sizeof(statements));
    (void)memset(blocks, 0, sizeof(blocks));
    (void)memset(&program, 0, sizeof(program));
    (void)memset(&body, 0, sizeof(body));
    (void)memset(&context, 0, sizeof(context));

    /* index++ inside an assignment's destination address: data[index++]. */
    expressions[0].kind = MINIC_EXPRESSION_LOCAL;
    expressions[0].value_category = MINIC_VALUE_LVALUE;
    expressions[0].value.local_id = 1U;
    expressions[1].kind = MINIC_EXPRESSION_UNARY;
    expressions[1].value.unary.operator_kind = MINIC_UNARY_POST_INCREMENT;
    expressions[1].value.unary.operand = 0U;
    expressions[2].kind = MINIC_EXPRESSION_LOCAL;
    expressions[2].value_category = MINIC_VALUE_LVALUE;
    expressions[2].value.local_id = 2U;
    expressions[3].kind = MINIC_EXPRESSION_SUBSCRIPT;
    expressions[3].value_category = MINIC_VALUE_LVALUE;
    expressions[3].value.subscript.base = 2U;
    expressions[3].value.subscript.index = 1U;
    expressions[4].kind = MINIC_EXPRESSION_INTEGER;
    expressions[4].value.integer_value = 7;
    /* GNU statement-expression tail is detached from its block. */
    expressions[5].kind = MINIC_EXPRESSION_STATEMENT;
    expressions[5].value.statement_expression.block = 0U;
    expressions[5].value.statement_expression.result = 1U;

    statements[0].kind = MINIC_STATEMENT_ASSIGN;
    statements[0].target_expression = 3U;
    statements[0].expression = 4U;
    statements[1].kind = MINIC_STATEMENT_RECORD_COPY;
    statements[1].target_expression = 3U;
    statements[1].expression = 4U;
    blocks[1].statements = statement_ids;
    blocks[1].statement_count = 2U;
    program.expressions = expressions;
    program.expression_count = 6U;
    program.statements = statements;
    program.statement_count = 2U;
    program.blocks = blocks;
    program.block_count = 2U;
    body.program = &program;
    context.body = &body;

    if (!core_expression_may_modify_local(&context, 5U, 1U, 0U)) return 1;
    if (core_expression_may_modify_local(&context, 5U, 2U, 0U)) return 2;
    if (!core_block_may_modify_local(&context, &blocks[1], 1U, 0U)) return 3;
    if (core_block_may_modify_local(&context, &blocks[1], 3U, 0U)) return 4;
    statements[0].kind = MINIC_STATEMENT_RETURN;
    statements[0].expression = 4U;
    if (!core_block_may_modify_local(&context, &blocks[1], 1U, 0U)) return 5;
    statements[1].kind = MINIC_STATEMENT_RETURN;
    statements[1].expression = 4U;
    if (core_block_may_modify_local(&context, &blocks[1], 1U, 0U)) return 6;
    return 0;
}
