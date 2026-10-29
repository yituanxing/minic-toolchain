/* The source owner must not traverse non-CFG statements' unused block fields.
 * Malformed ancestry must fail closed rather than manufacture a safe set. */
#include "core/core_lower_internal.h"

#include <string.h>

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
    MinicStatement statements[6];
    MinicBlock blocks[3];
    MinicStatementId root_ids[] = {0U, 2U};
    MinicStatementId unrelated_ids[] = {1U};
    MinicStatementId child_ids[] = {3U};
    MinicC0Program program;
    MinicFunctionBodyView body;
    MinicCoreLowerContext context;
    CoreSourceSubtreeMembership membership = {0};
    MinicStatement detached;
    size_t i;

    (void)memset(statements, 0, sizeof(statements));
    (void)memset(blocks, 0, sizeof(blocks));
    (void)memset(&program, 0, sizeof(program));
    (void)memset(&body, 0, sizeof(body));
    (void)memset(&context, 0, sizeof(context));
    for (i = 0U; i < 6U; ++i) {
        statements[i].then_block = MINIC_BLOCK_INVALID;
        statements[i].else_block = MINIC_BLOCK_INVALID;
    }
    statements[0].kind = MINIC_STATEMENT_RETURN;
    statements[0].then_block = 1U; /* Unused field; must NOT traverse block 1. */
    statements[1].kind = MINIC_STATEMENT_LABEL;
    statements[2].kind = MINIC_STATEMENT_IF;
    statements[2].then_block = 2U;
    statements[3].kind = MINIC_STATEMENT_GOTO;
    statements[3].then_block = 1U; /* Also unused for a source goto. */
    statements[4].kind = MINIC_STATEMENT_LABEL;
    statements[5].kind = MINIC_STATEMENT_DEFAULT;

    blocks[0].statements = root_ids;
    blocks[0].statement_count = 2U;
    blocks[1].statements = unrelated_ids;
    blocks[1].statement_count = 1U;
    blocks[2].statements = child_ids;
    blocks[2].statement_count = 1U;
    program.statements = statements;
    program.statement_count = 6U;
    program.blocks = blocks;
    program.block_count = 3U;
    body.program = &program;
    context.body = &body;

    if (!core_source_subtree_membership_from_block(
            &context, &blocks[0], &membership)) return 1;
    if (!membership.statements[0] || membership.statements[1] ||
        !membership.statements[2] || !membership.statements[3] ||
        membership.statements[4] || membership.statements[5]) return 2;
    core_source_subtree_membership_destroy(&membership);

    if (!core_source_subtree_membership_from_statement(
            &context, &statements[2], 5U, &membership)) return 3;
    if (membership.statements[0] || membership.statements[1] ||
        !membership.statements[2] || !membership.statements[3] ||
        membership.statements[4] || !membership.statements[5]) return 4;
    core_source_subtree_membership_destroy(&membership);

    statements[2].then_block = 100U;
    if (core_source_subtree_membership_from_statement(
            &context, &statements[2], MINIC_STATEMENT_INVALID,
            &membership)) return 5;
    core_source_subtree_membership_destroy(&membership);
    statements[2].then_block = 2U;

    detached = statements[2];
    if (core_source_subtree_membership_from_statement(
            &context, &detached, MINIC_STATEMENT_INVALID, &membership)) return 6;
    core_source_subtree_membership_destroy(&membership);

    program.statement_count = 0U;
    if (core_source_subtree_membership_from_block(
            &context, &blocks[0], &membership)) return 7;
    core_source_subtree_membership_destroy(&membership);
    return 0;
}
