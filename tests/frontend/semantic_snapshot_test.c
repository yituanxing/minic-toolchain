#include "frontend/semantic_snapshot.h"

#include <assert.h>
#include <stddef.h>

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

static void test_exact_match(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    assert(minic_semantic_snapshot_matches(&snapshot, &program));
    assert(minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(minic_semantic_snapshot_only_probe_expressions_changed(&snapshot, &program));
}

static void test_declarator_type_rollback(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.array_type_count += 3U;
    program.function_type_count += 2U;

    assert(!minic_semantic_snapshot_matches(&snapshot, &program));
    assert(minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_only_probe_expressions_changed(&snapshot, &program));
    assert(minic_semantic_snapshot_rollback_declarator_types(&snapshot, &program));
    assert(minic_semantic_snapshot_matches(&snapshot, &program));
}

static void test_expression_probe_rollback(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.expression_count += 4U;

    assert(!minic_semantic_snapshot_matches(&snapshot, &program));
    assert(!minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(minic_semantic_snapshot_only_probe_expressions_changed(&snapshot, &program));
    assert(minic_semantic_snapshot_rollback_probe_expressions(&snapshot, &program));
    assert(minic_semantic_snapshot_matches(&snapshot, &program));
}

static void test_zero_growth_expression_probe_rollback(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);

    assert(minic_semantic_snapshot_only_probe_expressions_changed(&snapshot, &program));
    assert(minic_semantic_snapshot_rollback_probe_expressions(&snapshot, &program));
    assert(minic_semantic_snapshot_matches(&snapshot, &program));
}

static void test_non_type_mutation_rejected(void) {
    MinicC0Program program = {0};
    MinicSemanticSnapshot snapshot;

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.array_type_count += 1U;
    program.expression_count += 1U;

    assert(!minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_declarator_types(&snapshot, &program));
    assert(!minic_semantic_snapshot_only_probe_expressions_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_probe_expressions(&snapshot, &program));
    assert(program.array_type_count == snapshot.array_type_count + 1U);
    assert(program.expression_count == snapshot.expression_count + 1U);

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.function_type_count += 1U;
    program.record_count += 1U;

    assert(!minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_declarator_types(&snapshot, &program));
    assert(!minic_semantic_snapshot_only_probe_expressions_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_probe_expressions(&snapshot, &program));
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

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.function_type_count -= 1U;

    assert(!minic_semantic_snapshot_only_declarator_types_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_declarator_types(&snapshot, &program));

    seed_program_counts(&program);
    snapshot = minic_semantic_snapshot_capture(&program);
    program.expression_count -= 1U;

    assert(!minic_semantic_snapshot_only_probe_expressions_changed(&snapshot, &program));
    assert(!minic_semantic_snapshot_rollback_probe_expressions(&snapshot, &program));
}

int main(void) {
    test_exact_match();
    test_declarator_type_rollback();
    test_expression_probe_rollback();
    test_zero_growth_expression_probe_rollback();
    test_non_type_mutation_rejected();
    test_counts_cannot_move_backwards();
    return 0;
}
