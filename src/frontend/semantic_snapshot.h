#ifndef MINIC_FRONTEND_SEMANTIC_SNAPSHOT_H
#define MINIC_FRONTEND_SEMANTIC_SNAPSHOT_H

#include "frontend/ast.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * A narrow transition aid for parser probes and semantic builders that still
 * materialize directly into Program-owned value arenas.
 *
 * This is deliberately not a general Program transaction/rollback facility:
 * Program-owned records, functions, blocks, globals, and other entities may own
 * nested allocations that cannot be made safe merely by restoring arena counts.
 * Long-term syntax probes must build transient syntax and avoid Program mutation.
 *
 * The bounded rollback classes below are value-only arenas:
 * - expressions;
 * - array types;
 * - function types.
 *
 * Each rollback rejects any mutation outside its declared class. This makes the
 * existing mutation boundary explicit while declaration/Sema ownership is moved
 * out of the parser; it is not the long-term substitute for a semantic commit.
 */
typedef struct MinicSemanticSnapshot {
    size_t expression_count;
    size_t local_count;
    size_t cleanup_context_count;
    size_t statement_count;
    size_t inline_asm_count;
    size_t file_asm_count;
    size_t block_count;
    size_t function_count;
    size_t record_count;
    size_t array_type_count;
    size_t function_type_count;
    size_t type_alias_count;
    size_t enum_count;
    size_t enumerator_count;
    size_t global_object_count;
    size_t fixed_register_binding_count;
} MinicSemanticSnapshot;

static inline MinicSemanticSnapshot minic_semantic_snapshot_capture(const MinicC0Program *program) {
    MinicSemanticSnapshot snapshot = {0};

    if (program == NULL) {
        return snapshot;
    }
    snapshot.expression_count = program->expression_count;
    snapshot.local_count = program->local_count;
    snapshot.cleanup_context_count = program->cleanup_context_count;
    snapshot.statement_count = program->statement_count;
    snapshot.inline_asm_count = program->inline_asm_count;
    snapshot.file_asm_count = program->file_asm_count;
    snapshot.block_count = program->block_count;
    snapshot.function_count = program->function_count;
    snapshot.record_count = program->record_count;
    snapshot.array_type_count = program->array_type_count;
    snapshot.function_type_count = program->function_type_count;
    snapshot.type_alias_count = program->type_alias_count;
    snapshot.enum_count = program->enum_count;
    snapshot.enumerator_count = program->enumerator_count;
    snapshot.global_object_count = program->global_object_count;
    snapshot.fixed_register_binding_count = program->fixed_register_binding_count;
    return snapshot;
}

static inline bool minic_semantic_snapshot_matches(const MinicSemanticSnapshot *snapshot,
                                                   const MinicC0Program *program) {
    if (snapshot == NULL || program == NULL) {
        return false;
    }
    return program->expression_count == snapshot->expression_count &&
           program->local_count == snapshot->local_count &&
           program->cleanup_context_count == snapshot->cleanup_context_count &&
           program->statement_count == snapshot->statement_count &&
           program->inline_asm_count == snapshot->inline_asm_count &&
           program->file_asm_count == snapshot->file_asm_count &&
           program->block_count == snapshot->block_count &&
           program->function_count == snapshot->function_count &&
           program->record_count == snapshot->record_count &&
           program->array_type_count == snapshot->array_type_count &&
           program->function_type_count == snapshot->function_type_count &&
           program->type_alias_count == snapshot->type_alias_count &&
           program->enum_count == snapshot->enum_count &&
           program->enumerator_count == snapshot->enumerator_count &&
           program->global_object_count == snapshot->global_object_count &&
           program->fixed_register_binding_count == snapshot->fixed_register_binding_count;
}

static inline bool
minic_semantic_snapshot_only_expressions_changed(const MinicSemanticSnapshot *snapshot,
                                                 const MinicC0Program *program) {
    if (snapshot == NULL || program == NULL ||
        program->expression_count < snapshot->expression_count) {
        return false;
    }
    return program->local_count == snapshot->local_count &&
           program->cleanup_context_count == snapshot->cleanup_context_count &&
           program->statement_count == snapshot->statement_count &&
           program->inline_asm_count == snapshot->inline_asm_count &&
           program->file_asm_count == snapshot->file_asm_count &&
           program->block_count == snapshot->block_count &&
           program->function_count == snapshot->function_count &&
           program->record_count == snapshot->record_count &&
           program->array_type_count == snapshot->array_type_count &&
           program->function_type_count == snapshot->function_type_count &&
           program->type_alias_count == snapshot->type_alias_count &&
           program->enum_count == snapshot->enum_count &&
           program->enumerator_count == snapshot->enumerator_count &&
           program->global_object_count == snapshot->global_object_count &&
           program->fixed_register_binding_count == snapshot->fixed_register_binding_count;
}

static inline bool minic_semantic_snapshot_rollback_expressions(
    const MinicSemanticSnapshot *snapshot, MinicC0Program *program) {
    if (!minic_semantic_snapshot_only_expressions_changed(snapshot, program)) {
        return false;
    }
    program->expression_count = snapshot->expression_count;
    return true;
}

static inline bool minic_semantic_snapshot_only_declarator_types_changed(
    const MinicSemanticSnapshot *snapshot, const MinicC0Program *program) {
    if (snapshot == NULL || program == NULL ||
        program->array_type_count < snapshot->array_type_count ||
        program->function_type_count < snapshot->function_type_count) {
        return false;
    }
    return program->expression_count == snapshot->expression_count &&
           program->local_count == snapshot->local_count &&
           program->cleanup_context_count == snapshot->cleanup_context_count &&
           program->statement_count == snapshot->statement_count &&
           program->inline_asm_count == snapshot->inline_asm_count &&
           program->file_asm_count == snapshot->file_asm_count &&
           program->block_count == snapshot->block_count &&
           program->function_count == snapshot->function_count &&
           program->record_count == snapshot->record_count &&
           program->type_alias_count == snapshot->type_alias_count &&
           program->enum_count == snapshot->enum_count &&
           program->enumerator_count == snapshot->enumerator_count &&
           program->global_object_count == snapshot->global_object_count &&
           program->fixed_register_binding_count == snapshot->fixed_register_binding_count;
}

static inline bool minic_semantic_snapshot_rollback_declarator_types(
    const MinicSemanticSnapshot *snapshot, MinicC0Program *program) {
    if (!minic_semantic_snapshot_only_declarator_types_changed(snapshot, program)) {
        return false;
    }
    program->array_type_count = snapshot->array_type_count;
    program->function_type_count = snapshot->function_type_count;
    return true;
}

static inline bool minic_semantic_snapshot_only_probe_values_changed(
    const MinicSemanticSnapshot *snapshot, const MinicC0Program *program) {
    if (snapshot == NULL || program == NULL ||
        program->expression_count < snapshot->expression_count ||
        program->array_type_count < snapshot->array_type_count ||
        program->function_type_count < snapshot->function_type_count) {
        return false;
    }
    return program->local_count == snapshot->local_count &&
           program->cleanup_context_count == snapshot->cleanup_context_count &&
           program->statement_count == snapshot->statement_count &&
           program->inline_asm_count == snapshot->inline_asm_count &&
           program->file_asm_count == snapshot->file_asm_count &&
           program->block_count == snapshot->block_count &&
           program->function_count == snapshot->function_count &&
           program->record_count == snapshot->record_count &&
           program->type_alias_count == snapshot->type_alias_count &&
           program->enum_count == snapshot->enum_count &&
           program->enumerator_count == snapshot->enumerator_count &&
           program->global_object_count == snapshot->global_object_count &&
           program->fixed_register_binding_count == snapshot->fixed_register_binding_count;
}

static inline bool minic_semantic_snapshot_rollback_probe_values(
    const MinicSemanticSnapshot *snapshot, MinicC0Program *program) {
    if (!minic_semantic_snapshot_only_probe_values_changed(snapshot, program)) {
        return false;
    }
    program->expression_count = snapshot->expression_count;
    program->array_type_count = snapshot->array_type_count;
    program->function_type_count = snapshot->function_type_count;
    return true;
}

#endif
