#include "frontend/semantic_snapshot.h"

/* Keep ownership assertions active even when the suite uses MODE=release. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static void seed_program_counts(MinicC0Program *program) {
    program->expression_count = 3U;
    program->local_count = 5U;
    program->cleanup_context_count = 7U;
    program->statement_count = 11U;
    program->inline_asm_count = 13U;
    program->file_asm_count = 17U;
    program->block_count = 19U;
    program->function_count = 23U;
    program->record_count = 29U;
    program->array_type_count = 31U;
    program->function_type_count = 37U;
    program->type_alias_count = 41U;
    program->enum_count = 43U;
    program->enumerator_count = 47U;
    program->global_object_count = 53U;
    program->fixed_register_binding_count = 59U;
}

/* Capture still covers every tracked arena; this assertion belongs to the
 * test, not to a duplicate predicate in production semantic code. */
static void assert_snapshot_counts(const MinicSemanticSnapshot *s,
                                   const MinicC0Program *p) {
    assert(s->expression_count == p->expression_count);
    assert(s->local_count == p->local_count);
    assert(s->cleanup_context_count == p->cleanup_context_count);
    assert(s->statement_count == p->statement_count);
    assert(s->inline_asm_count == p->inline_asm_count);
    assert(s->file_asm_count == p->file_asm_count);
    assert(s->block_count == p->block_count);
    assert(s->function_count == p->function_count);
    assert(s->record_count == p->record_count);
    assert(s->array_type_count == p->array_type_count);
    assert(s->function_type_count == p->function_type_count);
    assert(s->type_alias_count == p->type_alias_count);
    assert(s->enum_count == p->enum_count);
    assert(s->enumerator_count == p->enumerator_count);
    assert(s->global_object_count == p->global_object_count);
    assert(s->fixed_register_binding_count == p->fixed_register_binding_count);
}

static void test_exact_match(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    assert_snapshot_counts(&snapshot, &program);
    assert(minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
    assert_snapshot_counts(&snapshot, &program);
}

static void test_declarator_type_rollback(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.array_type_count += 3U;
    program.function_type_count += 2U;

    assert(minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(minic_semantic_snapshot_rollback_declarator_types(&snapshot, &program));
    assert_snapshot_counts(&snapshot, &program);
}

static void test_expression_probe_rollback(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    snapshot = minic_semantic_snapshot_capture(&program);
    {
        MinicExpression expression = {0};
        MinicExpressionId id;
        size_t index;

        expression.kind = MINIC_EXPRESSION_INTEGER;
        for (index = 0U; index < 4U; ++index) {
            assert(minic_c0_program_add_expression(&program, &expression, &id));
        }
    }

    assert(!minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
    assert_snapshot_counts(&snapshot, &program);
    minic_c0_program_destroy(&program);
}

static void test_combined_expression_type_probe_rollback(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot = minic_semantic_snapshot_capture(&program);
    MinicExpression expression = {0};
    MinicExpressionId id;

    expression.kind = MINIC_EXPRESSION_INTEGER;
    assert(minic_c0_program_add_expression(&program, &expression, &id));
    assert(id == 0U);
    /* The actual production query rollback allows unpublished value-only
     * type descriptors and expressions to grow in the same probe. */
    program.array_type_count = 1U;
    program.function_type_count = 1U;
    assert(minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
    assert_snapshot_counts(&snapshot, &program);
    minic_c0_program_destroy(&program);
}

static void test_owned_call_probe_rollback(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;
    MinicExpression expression = {0};
    MinicExpressionId keep_id;
    MinicExpressionId id;
    uint32_t scratch[4] = {8U, 9U, 10U, 11U};

    expression.kind = MINIC_EXPRESSION_CALL;
    expression.value.call.argument_count = 12U;
    expression.value.call.overflow_arguments = scratch;

    assert(minic_c0_program_add_expression(&program, &expression, &keep_id));
    assert(keep_id == 0U);
    snapshot = minic_semantic_snapshot_capture(&program);
    assert(minic_c0_program_add_expression(&program, &expression, &id));
    assert(id == 1U);
    assert(program.expressions[id].value.call.overflow_arguments != scratch);
    assert(program.expressions[id].value.call.overflow_arguments[3] == 11U);
    /* Reject unrelated mutations without discarding the owned CALL payload. */
    program.record_count++;
    assert(!minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
    assert(program.expressions[id].value.call.overflow_arguments[3] == 11U);
    program.record_count--;

    assert(minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
    assert(program.expression_count == 1U);
    /* A stale owned pointer means the rolled-back CALL leaked its allocation. */
    assert(program.expressions[id].value.call.overflow_arguments == NULL);
    assert(program.expressions[keep_id].value.call.overflow_arguments != NULL);
    assert(program.expressions[keep_id].value.call.overflow_arguments[3] == 11U);

    /* Reused tail slots must own a fresh private copy, not a discarded one. */
    assert(minic_c0_program_add_expression(&program, &expression, &id));
    assert(id == 1U);
    assert(program.expressions[id].value.call.overflow_arguments[3] == 11U);
    assert(minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
    assert(program.expressions[id].value.call.overflow_arguments == NULL);
    minic_c0_program_destroy(&program);
}

static void test_zero_growth_expression_probe_rollback(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);

    assert(minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
    assert_snapshot_counts(&snapshot, &program);
}

static void test_non_type_mutation_rejected(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.array_type_count += 1U;
    program.expression_count += 1U;
    program.local_count += 1U;

    assert(!minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_declarator_types(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
    assert(program.array_type_count == snapshot.array_type_count + 1U);
    assert(program.expression_count == snapshot.expression_count + 1U);
    assert(program.local_count == snapshot.local_count + 1U);

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.function_type_count += 1U;
    program.record_count += 1U;

    assert(!minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_declarator_types(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
    assert(program.function_type_count == snapshot.function_type_count + 1U);
    assert(program.record_count == snapshot.record_count + 1U);
}

static void test_counts_cannot_move_backwards(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.array_type_count -= 1U;

    assert(!minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_declarator_types(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.function_type_count -= 1U;

    assert(!minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_declarator_types(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.expression_count -= 1U;

    assert(!minic_semantic_snapshot_rollback_probe_expression_types(&snapshot, &program));
}

int main(void) {
    test_exact_match();
    test_declarator_type_rollback();
    test_expression_probe_rollback();
    test_combined_expression_type_probe_rollback();
    test_owned_call_probe_rollback();
    test_zero_growth_expression_probe_rollback();
    test_non_type_mutation_rejected();
    test_counts_cannot_move_backwards();
    return 0;
}
