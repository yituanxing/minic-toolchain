/* Target-neutral AST specialization contracts: fact keys, section provenance,
 * conservative source ownership, stable CALL buckets and append-only tail. */
#include "compiler/inline_specialize_common.h"
#include "frontend/function_body.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, code) do { if (!(condition)) return (code); } while (0)

static bool fail_owner_oracle;

/* The production owner oracle has its own function-body tests. Here it is
 * controlled independently to exercise both indexed and fallback consumers. */
bool minic_perf_program_build_expression_source_owners(
    const MinicC0Program *program, size_t function_limit,
    MinicFunctionId *owners, size_t owner_count) {
    size_t i;
    if (fail_owner_oracle || program == NULL || function_limit > program->function_count ||
        owner_count != program->expression_count ||
        (owner_count != 0U && owners == NULL)) return false;
    for (i = 0U; i < owner_count; ++i) {
        owners[i] = i == 0U || i == 4U ? 0U :
                    i == 1U || i == 5U ? 1U :
                    i == 6U ? (MinicFunctionId)program->function_count :
                    MINIC_FUNCTION_INVALID;
    }
    return true;
}

static int test_fact_key(void) {
    MinicInlineSpecializationKey key;
    MinicFunction functions[4] = {0};
    MinicC0Program program = {0};
    MinicFunction caller = {0};
    MinicFunctionId match;
    program.functions = functions;
    program.function_count = 4U;
    functions[0].parameter_count = 2U;
    functions[2].is_integer_specialization = true;
    functions[2].specialization_source = 0U;
    functions[2].parameter_count = 2U;
    functions[2].specialization_integer_known[0] = true;
    functions[2].specialization_integer_bits[0] = 7U;
    functions[2].specialization_symbolic_known[1] = true;
    functions[2].specialization_symbolic_expression[1] = 123U;
    functions[2].specialization_boolean_range_known[0] = true;
    functions[2].section_name = "head";
    functions[2].section_name_length = 4U;
    functions[3] = functions[2];
    functions[3].specialization_boolean_range_known[0] = false;
    caller.section_name = "head";
    caller.section_name_length = 4U;

    minic_inline_spec_key_clear(&key);
    CHECK(key.symbolic_expression[0] == MINIC_EXPRESSION_INVALID &&
          !key.integer_known[0], 1);
    CHECK(minic_inline_spec_key_from_function(&key, &functions[2], 2U), 2);
    CHECK(key.integer_known[0] && key.integer_bits[0] == 7U &&
          key.symbolic_expression[1] == 123U && key.boolean_range_known[0], 3);
    CHECK(minic_inline_spec_key_matches(
        &functions[2], &functions[0], &caller, 0U, &key,
        MINIC_INLINE_SPEC_FACT_ALL, 2U), 4);
    CHECK(!minic_inline_spec_key_matches(
        &functions[3], &functions[0], &caller, 0U, &key,
        MINIC_INLINE_SPEC_FACT_ALL, 2U), 5);
    CHECK(minic_inline_spec_key_matches(
        &functions[3], &functions[0], &caller, 0U, &key,
        MINIC_INLINE_SPEC_FACT_INTEGER | MINIC_INLINE_SPEC_FACT_SYMBOLIC, 2U), 6);
    match = minic_inline_spec_find_variant(
        &program, &functions[0], &caller, 0U, &key,
        MINIC_INLINE_SPEC_FACT_ALL, 2U);
    CHECK(match == 2U, 7);
    match = minic_inline_spec_find_variant(
        &program, &functions[0], &caller, 0U, &key,
        MINIC_INLINE_SPEC_FACT_ALL, 3U);
    CHECK(match == MINIC_FUNCTION_INVALID, 8);
    functions[0].section_name = "source";
    functions[0].section_name_length = 6U;
    CHECK(!minic_inline_spec_section_matches(
        &functions[2], &functions[0], &caller), 9);
    functions[2].section_name = "source";
    functions[2].section_name_length = 6U;
    CHECK(minic_inline_spec_section_matches(
        &functions[2], &functions[0], &caller), 10);
    CHECK(!minic_inline_spec_key_matches(
        &functions[3], &functions[0], &caller, 0U, &key,
        MINIC_INLINE_SPEC_FACT_ALL, 2U), 11);
    CHECK(!minic_inline_spec_key_from_function(
        &key, &functions[2], MINIC_MAX_FUNCTION_PARAMETERS + 1U), 12);
    return 0;
}

static int test_owners_and_calls(void) {
    MinicExpression expressions[10] = {0};
    MinicFunction functions[3] = {0};
    MinicC0Program program = {0};
    MinicInlineInitialOwners initial = {0};
    MinicInlineCallWorklist worklist = {0};
    MinicInlineCallCursor cursor;
    MinicExpressionId actual;
    size_t i;
    static const size_t expected_source0[] = {0U, 2U, 4U, 6U, 7U, 9U};
    static const size_t expected_source1[] = {1U, 2U, 5U, 6U, 7U, 9U};
    static const size_t expected_fallback[] = {0U, 1U, 2U, 4U, 5U, 6U, 7U, 9U};
    program.functions = functions;
    program.function_count = 3U;
    program.expressions = expressions;
    program.expression_count = 7U;
    functions[0].section_name = ".init.text";
    functions[0].section_name_length = 10U;
    for (i = 0U; i < 10U; ++i) {
        expressions[i].kind = i == 3U || i == 8U
            ? MINIC_EXPRESSION_INTEGER : MINIC_EXPRESSION_CALL;
        expressions[i].value.call.function_id = i == 5U
            ? MINIC_FUNCTION_INVALID : 0U;
    }
    CHECK(minic_inline_spec_sections_present(&program), 20);
    CHECK(minic_inline_initial_owners_build(&program, 2U, &initial), 21);
    CHECK(initial.ready &&
          minic_inline_initial_owners_get(&initial, 0U) == 0U &&
          minic_inline_initial_owners_get(&initial, 6U) == MINIC_FUNCTION_INVALID, 22);
    /* A clone can acquire the numeric ID previously used for ambiguity.
     * The captured canonical-source limit still prevents aliasing that ID. */
    program.function_count = 4U;
    CHECK(minic_inline_initial_owners_get(&initial, 6U) ==
          MINIC_FUNCTION_INVALID, 36);
    program.function_count = 3U;
    minic_inline_initial_owners_destroy(&initial);
    fail_owner_oracle = true;
    CHECK(!minic_inline_initial_owners_build(&program, 2U, &initial), 23);
    minic_inline_initial_owners_destroy(&initial);
    fail_owner_oracle = false;

    CHECK(minic_inline_call_worklist_build(&program, 2U, &worklist), 24);
    CHECK(worklist.ready && worklist.owner_ready &&
          worklist.call_count == 6U && worklist.valid_call_count == 5U, 25);
    program.expression_count = 10U; /* Two CALLs appended after index build. */
    minic_inline_call_cursor_begin(&worklist, 0U, &cursor);
    CHECK(cursor.skipped_other_owner == 1U, 26);
    for (i = 0U; i < sizeof(expected_source0) / sizeof(expected_source0[0]); ++i)
        CHECK(minic_inline_call_cursor_next(&cursor, &program, &actual) &&
              actual == expected_source0[i], 27);
    CHECK(!minic_inline_call_cursor_next(&cursor, &program, &actual), 28);
    minic_inline_call_cursor_begin(&worklist, 1U, &cursor);
    CHECK(cursor.skipped_other_owner == 2U, 29);
    for (i = 0U; i < sizeof(expected_source1) / sizeof(expected_source1[0]); ++i)
        CHECK(minic_inline_call_cursor_next(&cursor, &program, &actual) &&
              actual == expected_source1[i], 30);
    CHECK(!minic_inline_call_cursor_next(&cursor, &program, &actual), 31);
    minic_inline_call_worklist_destroy(&worklist);

    fail_owner_oracle = true;
    CHECK(minic_inline_call_worklist_build(&program, 2U, &worklist) &&
          !worklist.ready && !worklist.owner_ready, 32);
    minic_inline_call_cursor_begin(&worklist, 0U, &cursor);
    for (i = 0U; i < sizeof(expected_fallback) / sizeof(expected_fallback[0]); ++i)
        CHECK(minic_inline_call_cursor_next(&cursor, &program, &actual) &&
              actual == expected_fallback[i], 33);
    CHECK(!minic_inline_call_cursor_next(&cursor, &program, &actual), 34);
    minic_inline_call_worklist_destroy(&worklist);
    fail_owner_oracle = false;
    functions[0].section_name = NULL;
    functions[0].section_name_length = 0U;
    CHECK(minic_inline_initial_owners_build(&program, 2U, &initial) &&
          !initial.ready &&
          minic_inline_initial_owners_get(&initial, 0U) == MINIC_FUNCTION_INVALID, 35);
    minic_inline_initial_owners_destroy(&initial);
    return 0;
}

int main(void) {
    CHECK(test_fact_key() == 0, 40);
    CHECK(test_owners_and_calls() == 0, 41);
    puts("INLINE_SPECIALIZE_COMMON=PASS key section_owner transitive_buckets appended_calls conservative_fallback");
    return 0;
}
