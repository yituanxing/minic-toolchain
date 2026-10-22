#include "core/reentry_index.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dedicated owner for read-only incoming AST edges and opt-in counters;
 * no target ABI or physical machine resource belongs in this layer. */
/* Read-only incoming control-flow references for one Core Function Set build.
 * Owned by minic_prepare_core_function_set() and explicitly passed to lowerings.
 * Standalone users of minic_core_lower_function() keep the legacy proof path.
 * Invalid AST or allocation failure disables the index: never infer absence
 * of a goto, asm-goto, or label address from an incomplete index. */
typedef struct MinicCoreReentryIncoming {
    size_t source_statement;
    size_t next;
} MinicCoreReentryIncoming;

struct MinicCoreReentryIndex {
    const MinicC0Program *program;
    size_t *heads;
    MinicCoreReentryIncoming *incoming;
    unsigned char *addressed;
    size_t incoming_count;
    unsigned long long indexed_if_queries;
    unsigned long long indexed_loop_queries;
};

void minic_core_reentry_index_destroy(MinicCoreReentryIndex *index) {
    if (index == NULL) return;
    if (getenv("MINIC_CORE_REENTRY_INDEX_PERF") != NULL &&
        index->program != NULL) {
        (void)fprintf(stderr,
                      "MINIC_CORE_REENTRY_INDEX ready=1 incoming=%zu "
                      "indexed_if=%llu indexed_loop=%llu statements=%zu expressions=%zu\n",
                      index->incoming_count,
                      index->indexed_if_queries,
                      index->indexed_loop_queries,
                      index->program->statement_count,
                      index->program->expression_count);
    }
    free(index->heads);
    free(index->incoming);
    free(index->addressed);
    free(index);
}

MinicCoreReentryIndex *minic_core_reentry_index_create(const MinicC0Program *program) {
    size_t statement_count;
    size_t incoming_count;
    size_t i;
    size_t next_incoming;
    MinicCoreReentryIndex *index = (MinicCoreReentryIndex *)calloc(1U, sizeof(*index));
    if (index == NULL) return NULL;
    if (program == NULL || program->statement_count == 0U ||
        program->statement_count > SIZE_MAX / sizeof(*index->heads) ||
        program->statement_count > SIZE_MAX / sizeof(*index->addressed)) {
        goto fallback;
    }
    statement_count = program->statement_count;
    incoming_count = 0U;
    /* First pass validates all statements and computes exact edge capacity. */
    for (i = 0U; i < statement_count; ++i) {
        const MinicStatement *source = minic_c0_program_statement(program, i);
        if (source == NULL) {
            goto fallback;
        }
        if (source->kind == MINIC_STATEMENT_GOTO &&
            source->target_statement < statement_count) {
            if (incoming_count == SIZE_MAX) goto fallback;
            ++incoming_count;
        } else if (source->kind == MINIC_STATEMENT_INLINE_ASM &&
                   source->inline_asm_id < program->inline_asm_count) {
            const MinicInlineAsm *code = &program->inline_asms[source->inline_asm_id];
            size_t label_index;
            if (!code->is_goto) continue;
            if (code->label_count != 0U && code->labels == NULL) goto fallback;
            for (label_index = 0U; label_index < code->label_count; ++label_index) {
                if (code->labels[label_index].target_statement < statement_count) {
                    if (incoming_count == SIZE_MAX) goto fallback;
                    ++incoming_count;
                }
            }
        }
    }
    if (incoming_count > SIZE_MAX / sizeof(*index->incoming)) goto fallback;
    index->heads = (size_t *)malloc(statement_count * sizeof(*index->heads));
    index->addressed =
        (unsigned char *)calloc(statement_count, sizeof(*index->addressed));
    if (incoming_count != 0U) {
        index->incoming = (MinicCoreReentryIncoming *)malloc(
            incoming_count * sizeof(*index->incoming));
    }
    if (index->heads == NULL || index->addressed == NULL ||
        (incoming_count != 0U && index->incoming == NULL)) {
        goto fallback;
    }
    for (i = 0U; i < statement_count; ++i) {
        index->heads[i] = SIZE_MAX;
    }
    /* All label-address expressions must be validated before we claim
     * that a target has no computed-goto entry point. */
    for (i = 0U; i < program->expression_count; ++i) {
        const MinicExpression *expression = minic_c0_program_expression(program, i);
        if (expression == NULL) goto fallback;
        if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
            expression->value.label_statement_id < statement_count) {
            index->addressed[expression->value.label_statement_id] = 1U;
        }
    }
    next_incoming = 0U;
    for (i = 0U; i < statement_count; ++i) {
        const MinicStatement *source = minic_c0_program_statement(program, i);
        if (source == NULL) goto fallback;
        if (source->kind == MINIC_STATEMENT_GOTO &&
            source->target_statement < statement_count) {
            size_t target = source->target_statement;
            index->incoming[next_incoming].source_statement = i;
            index->incoming[next_incoming].next = index->heads[target];
            index->heads[target] = next_incoming++;
        } else if (source->kind == MINIC_STATEMENT_INLINE_ASM &&
                   source->inline_asm_id < program->inline_asm_count) {
            const MinicInlineAsm *code = &program->inline_asms[source->inline_asm_id];
            size_t label_index;
            if (!code->is_goto) continue;
            for (label_index = 0U; label_index < code->label_count; ++label_index) {
                size_t target = code->labels[label_index].target_statement;
                if (target >= statement_count) continue;
                index->incoming[next_incoming].source_statement = i;
                index->incoming[next_incoming].next = index->heads[target];
                index->heads[target] = next_incoming++;
            }
        }
    }
    if (next_incoming != incoming_count) goto fallback;
    index->incoming_count = incoming_count;
    index->program = program; /* Publish only after the entire graph is valid. */
    return index;

fallback:
    minic_core_reentry_index_destroy(index);
    return NULL;
}



bool minic_core_reentry_index_ready(const MinicCoreReentryIndex *index,
                                   const MinicC0Program *program) {
    return index != NULL && program != NULL && index->program == program;
}

void minic_core_reentry_index_check_discarded(
    MinicCoreReentryIndex *index, const MinicC0Program *program,
    const bool *membership, bool *unsafe) {
    size_t statement_index;
    if (unsafe == NULL) return;
    if (!minic_core_reentry_index_ready(index, program) || membership == NULL) {
        *unsafe = true;
        return;
    }
    /* Case/default labels are independently dispatched; direct internal
     * goto edges disappear along with the discarded subtree. */
    ++index->indexed_if_queries;
    for (statement_index = 0U; statement_index < program->statement_count && !*unsafe;
         ++statement_index) {
        const MinicStatement *statement;
        size_t edge;
        if (!membership[statement_index]) continue;
        statement = minic_c0_program_statement(program, statement_index);
        if (statement == NULL || statement->kind == MINIC_STATEMENT_CASE ||
            statement->kind == MINIC_STATEMENT_DEFAULT ||
            index->addressed[statement_index] != 0U) {
            *unsafe = true;
            break;
        }
        for (edge = index->heads[statement_index]; edge != SIZE_MAX;
             edge = index->incoming[edge].next) {
            size_t source = index->incoming[edge].source_statement;
            if (!membership[source]) {
                *unsafe = true;
                break;
            }
        }
    }
}

bool minic_core_reentry_index_unreferenced_loop_label(
    MinicCoreReentryIndex *index, const MinicC0Program *program,
    MinicStatementId label_id) {
    if (!minic_core_reentry_index_ready(index, program) ||
        label_id >= program->statement_count ||
        index->heads[label_id] != SIZE_MAX ||
        index->addressed[label_id] != 0U)
        return false;
    ++index->indexed_loop_queries;
    return true;
}

/* Source-label reentry is a graph query, not an O(program size) rescan for
 * every switch case. If the cache isn't complete, caller uses its original
 * conservative whole-program scan; no negative facts from a partial index. */
bool minic_core_reentry_index_switch_label_referenced(
    MinicCoreReentryIndex *index, const MinicC0Program *program,
    MinicStatementId label_id) {
    size_t edge;
    if (!minic_core_reentry_index_ready(index, program) ||
        label_id >= program->statement_count) return false;
    if (index->addressed[label_id] != 0U) return true;
    for (edge = index->heads[label_id]; edge != SIZE_MAX;
         edge = index->incoming[edge].next) {
        const MinicStatement *source = minic_c0_program_statement(
            program, index->incoming[edge].source_statement);
        if (source == NULL) return false;
        if ((source->kind == MINIC_STATEMENT_GOTO &&
             source->expression == MINIC_EXPRESSION_INVALID) ||
            (source->kind == MINIC_STATEMENT_INLINE_ASM &&
             source->inline_asm_id < program->inline_asm_count)) return true;
    }
    return false;
}
