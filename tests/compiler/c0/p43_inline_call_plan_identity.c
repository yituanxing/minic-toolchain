/* P43: source CALL IDs must remain canonical even when several AST passes
 * successively choose a clone for the same expression. The transient plan
 * is discarded before Core lowering, which independently resolves the
 * executable callee from the active caller's proven facts. */
#include "compiler/inline_specialize_common.h"

#include <stdio.h>

#define CHECK(test, id) do { if (!(test)) return (id); } while (0)

/* The common unit also provides the source-owner worklist.  This small
 * contract test deliberately does not construct an owner index. */
bool minic_perf_program_build_expression_source_owners(
    const MinicC0Program *program, size_t function_limit,
    MinicFunctionId *owners, size_t owner_count) {
    (void)program;
    (void)function_limit;
    (void)owners;
    (void)owner_count;
    return false;
}

static int test_call_plan(void) {
    MinicFunction functions[7] = {0};
    MinicExpression expressions[6] = {0};
    MinicC0Program program = {0};
    MinicInlineCallPlan plan = {0};

    program.functions = functions;
    program.function_count = 3U;
    program.expressions = expressions;
    program.expression_count = 3U;

    expressions[0].kind = MINIC_EXPRESSION_CALL;
    expressions[0].value.call.function_id = 0U;
    expressions[1].kind = MINIC_EXPRESSION_INTEGER;
    expressions[2].kind = MINIC_EXPRESSION_CALL;
    expressions[2].value.call.function_id = 1U;

    CHECK(minic_inline_call_plan_initialize(&plan, &program), 1);
    CHECK(!minic_inline_call_plan_initialize(&plan, &program), 2);
    CHECK(minic_inline_call_plan_get(&plan, 0U) == 0U, 3);
    CHECK(minic_inline_call_plan_get(&plan, 2U) == 1U, 4);
    CHECK(minic_inline_call_plan_get(&plan, 1U) == MINIC_FUNCTION_INVALID, 5);

    /* Clones appended by successive integer/symbolic/boolean passes. */
    program.function_count = 6U;
    CHECK(minic_inline_call_plan_set(&plan, 2U, 3U), 6);
    CHECK(minic_inline_call_plan_set(&plan, 0U, 4U), 7);
    CHECK(minic_inline_call_plan_set(&plan, 2U, 5U), 8);
    CHECK(minic_inline_call_plan_get(&plan, 0U) == 4U, 9);
    CHECK(minic_inline_call_plan_get(&plan, 2U) == 5U, 10);
    CHECK(expressions[0].value.call.function_id == 0U, 11);
    CHECK(expressions[2].value.call.function_id == 1U, 12);

    /* A later-cloned FunctionBody appends a previously nonexistent CALL.
     * The plan must expand without losing earlier selections or modifying
     * the just-appended canonical expression. */
    expressions[3].kind = MINIC_EXPRESSION_CALL;
    expressions[3].value.call.function_id = 2U;
    expressions[4].kind = MINIC_EXPRESSION_INTEGER;
    expressions[5].kind = MINIC_EXPRESSION_CALL;
    expressions[5].value.call.function_id = 0U;
    program.expression_count = 6U;
    CHECK(minic_inline_call_plan_get(&plan, 3U) == 2U, 13);
    CHECK(minic_inline_call_plan_get(&plan, 5U) == 0U, 14);
    CHECK(minic_inline_call_plan_set(&plan, 5U, 3U), 15);
    CHECK(minic_inline_call_plan_get(&plan, 0U) == 4U, 16);
    CHECK(minic_inline_call_plan_get(&plan, 2U) == 5U, 17);
    CHECK(minic_inline_call_plan_get(&plan, 3U) == 2U, 18);
    CHECK(minic_inline_call_plan_get(&plan, 5U) == 3U, 19);
    CHECK(expressions[5].value.call.function_id == 0U, 20);

    CHECK(!minic_inline_call_plan_set(&plan, 1U, 3U), 21);
    CHECK(!minic_inline_call_plan_set(&plan, 6U, 3U), 22);
    CHECK(!minic_inline_call_plan_set(&plan, 3U, 7U), 23);
    CHECK(minic_inline_call_plan_get(&plan, 6U) == MINIC_FUNCTION_INVALID, 24);
    CHECK(expressions[0].value.call.function_id == 0U &&
          expressions[2].value.call.function_id == 1U &&
          expressions[3].value.call.function_id == 2U &&
          expressions[5].value.call.function_id == 0U, 25);

    minic_inline_call_plan_destroy(&plan);
    CHECK(plan.program == NULL && plan.selected == NULL &&
          plan.selected_count == 0U, 26);
    CHECK(minic_inline_call_plan_get(&plan, 0U) == MINIC_FUNCTION_INVALID, 27);
    CHECK(minic_inline_call_plan_initialize(&plan, &program), 28);
    CHECK(minic_inline_call_plan_get(&plan, 0U) == 0U &&
          minic_inline_call_plan_get(&plan, 2U) == 1U &&
          minic_inline_call_plan_get(&plan, 5U) == 0U, 29);
    minic_inline_call_plan_destroy(&plan);
    return 0;
}

int main(void) {
    int result = test_call_plan();
    if (result != 0) {
        fprintf(stderr, "P43_INLINE_CALL_PLAN_IDENTITY=FAIL test=%d\n", result);
        return result;
    }
    puts("P43_INLINE_CALL_PLAN_IDENTITY=PASS canonical_source=PASS clone_appends=PASS");
    return 0;
}
