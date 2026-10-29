/* Indexed and standalone Core reentry queries must produce the same typed
 * source-edge reasons. This is a white-box C0 graph, not a machine-code test. */
#include "core/reentry_index.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

const MinicStatement *minic_c0_program_statement(
    const MinicC0Program *program, MinicStatementId id) {
    return program != NULL && id < program->statement_count
        ? &program->statements[id] : NULL;
}

const MinicExpression *minic_c0_program_expression(
    const MinicC0Program *program, MinicExpressionId id) {
    return program != NULL && id < program->expression_count
        ? &program->expressions[id] : NULL;
}

static bool check_same(
    MinicCoreReentryIndex *index, const MinicC0Program *program,
    const MinicCoreReentryQuery *query, unsigned int expected) {
    return minic_core_reentry_query(index, program, query) == expected &&
           minic_core_reentry_query(NULL, program, query) == expected;
}

int main(void) {
    MinicStatement statements[9];
    MinicInlineAsmLabel label;
    MinicInlineAsm inline_asm;
    MinicExpression expressions[1];
    MinicC0Program program;
    MinicCoreReentryIndex *index;
    MinicCoreReentryQuery query;
    bool members[9];
    size_t i;
    const unsigned int full =
        MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP |
        MINIC_CORE_REENTRY_CHECK_LABEL_ADDRESS |
        MINIC_CORE_REENTRY_CHECK_CASE_DISPATCH;

    (void)memset(statements, 0, sizeof(statements));
    (void)memset(&label, 0, sizeof(label));
    (void)memset(&inline_asm, 0, sizeof(inline_asm));
    (void)memset(expressions, 0, sizeof(expressions));
    (void)memset(&program, 0, sizeof(program));
    (void)memset(&query, 0, sizeof(query));
    (void)memset(members, 0, sizeof(members));
    for (i = 0U; i < 9U; ++i) {
        statements[i].kind = MINIC_STATEMENT_LABEL;
        statements[i].target_statement = MINIC_STATEMENT_INVALID;
        statements[i].then_block = MINIC_BLOCK_INVALID;
        statements[i].else_block = MINIC_BLOCK_INVALID;
    }
    /* External direct goto targets 2; an internal goto in member 4 also
     * targets 2 and must not count as an external source when both are kept. */
    statements[0].kind = MINIC_STATEMENT_GOTO;
    statements[0].target_statement = 2U;
    statements[4].kind = MINIC_STATEMENT_GOTO;
    statements[4].target_statement = 2U;
    statements[1].kind = MINIC_STATEMENT_INLINE_ASM;
    statements[1].inline_asm_id = 0U;
    label.target_statement = 3U;
    inline_asm.is_goto = true;
    inline_asm.label_count = 1U;
    inline_asm.labels = &label;
    statements[5].kind = MINIC_STATEMENT_CASE;
    statements[6].kind = MINIC_STATEMENT_DEFAULT;
    expressions[0].kind = MINIC_EXPRESSION_LABEL_ADDRESS;
    expressions[0].value.label_statement_id = 7U;
    program.statements = statements;
    program.statement_count = 9U;
    program.expressions = expressions;
    program.expression_count = 1U;
    program.inline_asms = &inline_asm;
    program.inline_asm_count = 1U;

    index = minic_core_reentry_index_create(&program);
    if (!minic_core_reentry_index_ready(index, &program)) return 1;

    members[2] = true;
    members[4] = true;
    query.membership = members;
    query.label_id = MINIC_STATEMENT_INVALID;
    query.checks = full;
    query.purpose = MINIC_CORE_REENTRY_PURPOSE_IF_DISCARDED;
    if (!check_same(index, &program, &query,
                    MINIC_CORE_REENTRY_REASON_DIRECT_GOTO)) return 2;
    members[0] = true;
    if (!check_same(index, &program, &query, 0U)) return 3;

    (void)memset(members, 0, sizeof(members));
    members[1] = true;
    members[3] = true;
    query.checks = MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP;
    if (!check_same(index, &program, &query, 0U)) return 4;

    query.membership = NULL;
    query.label_id = 3U;
    query.checks = MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP |
                   MINIC_CORE_REENTRY_CHECK_LABEL_ADDRESS;
    query.purpose = MINIC_CORE_REENTRY_PURPOSE_SWITCH_LABEL;
    if (!check_same(index, &program, &query,
                    MINIC_CORE_REENTRY_REASON_ASM_GOTO)) return 5;
    query.label_id = 7U;
    if (!check_same(index, &program, &query,
                    MINIC_CORE_REENTRY_REASON_LABEL_ADDRESS)) return 6;

    (void)memset(members, 0, sizeof(members));
    members[5] = true;
    members[6] = true;
    query.membership = members;
    query.label_id = MINIC_STATEMENT_INVALID;
    query.checks = MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP;
    query.purpose = MINIC_CORE_REENTRY_PURPOSE_UNREACHABLE_TAIL;
    if (!check_same(index, &program, &query, 0U)) return 7;
    query.checks = full;
    query.purpose = MINIC_CORE_REENTRY_PURPOSE_IF_DISCARDED;
    if (!check_same(index, &program, &query,
                    MINIC_CORE_REENTRY_REASON_CASE_DISPATCH)) return 8;

    members[2] = true;
    members[3] = true;
    members[7] = true;
    if (!check_same(index, &program, &query,
                    MINIC_CORE_REENTRY_REASON_DIRECT_GOTO |
                    MINIC_CORE_REENTRY_REASON_ASM_GOTO |
                    MINIC_CORE_REENTRY_REASON_CASE_DISPATCH |
                    MINIC_CORE_REENTRY_REASON_LABEL_ADDRESS)) return 9;

    query.membership = NULL;
    query.label_id = 8U;
    query.checks = MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP |
                   MINIC_CORE_REENTRY_CHECK_LABEL_ADDRESS;
    query.purpose = MINIC_CORE_REENTRY_PURPOSE_LOOP_LABEL;
    if (!check_same(index, &program, &query, 0U)) return 10;
    query.label_id = 9U;
    if (!check_same(index, &program, &query,
                    MINIC_CORE_REENTRY_REASON_INDETERMINATE)) return 11;

    minic_core_reentry_index_destroy(index);
    return 0;
}
