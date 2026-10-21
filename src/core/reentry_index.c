#include "core/reentry_index.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dedicated owner for read-only incoming AST edges and opt-in counters;
 * no target ABI or physical machine resource belongs in this layer. */
/* Read-only incoming control-flow references for one Core Function Set build.
 * Bounded to minic_prepare_core_function_set() by explicit begin/end calls.
 * Standalone users of minic_core_lower_function() keep the legacy proof path.
 * Invalid AST or allocation failure disables the index: never infer absence
 * of a goto, asm-goto, or label address from an incomplete index. */
typedef struct MinicCoreReentryIncoming {
    size_t source_statement;
    size_t next;
} MinicCoreReentryIncoming;

typedef struct MinicCoreReentryIndex {
    const MinicC0Program *program;
    size_t *heads;
    MinicCoreReentryIncoming *incoming;
    unsigned char *addressed;
    size_t incoming_count;
    unsigned long long indexed_if_queries;
    unsigned long long indexed_loop_queries;
} MinicCoreReentryIndex;

static MinicCoreReentryIndex minic_core_reentry_index;

void minic_core_lower_reentry_index_end(void) {
    if (getenv("MINIC_CORE_REENTRY_INDEX_PERF") != NULL &&
        minic_core_reentry_index.program != NULL) {
        (void)fprintf(stderr,
                      "MINIC_CORE_REENTRY_INDEX ready=1 incoming=%zu "
                      "indexed_if=%llu indexed_loop=%llu statements=%zu expressions=%zu\n",
                      minic_core_reentry_index.incoming_count,
                      minic_core_reentry_index.indexed_if_queries,
                      minic_core_reentry_index.indexed_loop_queries,
                      minic_core_reentry_index.program->statement_count,
                      minic_core_reentry_index.program->expression_count);
    }
    free(minic_core_reentry_index.heads);
    free(minic_core_reentry_index.incoming);
    free(minic_core_reentry_index.addressed);
    (void)memset(&minic_core_reentry_index, 0, sizeof(minic_core_reentry_index));
}

void minic_core_lower_reentry_index_begin(const MinicC0Program *program) {
    size_t statement_count;
    size_t incoming_count;
    size_t i;
    size_t next_incoming;
    MinicCoreReentryIndex *index = &minic_core_reentry_index;

    minic_core_lower_reentry_index_end();
    if (program == NULL || program->statement_count == 0U ||
        program->statement_count > SIZE_MAX / sizeof(*index->heads) ||
        program->statement_count > SIZE_MAX / sizeof(*index->addressed)) {
        return;
    }
    statement_count = program->statement_count;
    incoming_count = 0U;
    /* First pass validates all statements and computes exact edge capacity. */
    for (i = 0U; i < statement_count; ++i) {
        const MinicStatement *source = minic_c0_program_statement(program, i);
        if (source == NULL) {
            return;
        }
        if (source->kind == MINIC_STATEMENT_GOTO &&
            source->target_statement < statement_count) {
            if (incoming_count == SIZE_MAX) return;
            ++incoming_count;
        } else if (source->kind == MINIC_STATEMENT_INLINE_ASM &&
                   source->inline_asm_id < program->inline_asm_count) {
            const MinicInlineAsm *code = &program->inline_asms[source->inline_asm_id];
            size_t label_index;
            if (!code->is_goto) continue;
            if (code->label_count != 0U && code->labels == NULL) return;
            for (label_index = 0U; label_index < code->label_count; ++label_index) {
                if (code->labels[label_index].target_statement < statement_count) {
                    if (incoming_count == SIZE_MAX) return;
                    ++incoming_count;
                }
            }
        }
    }
    if (incoming_count > SIZE_MAX / sizeof(*index->incoming)) return;
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
    return;

fallback:
    minic_core_lower_reentry_index_end();
}



bool minic_core_reentry_index_ready(const MinicC0Program *program) {
    return program != NULL && minic_core_reentry_index.program == program;
}

void minic_core_reentry_index_check_discarded(
    const MinicC0Program *program, const bool *membership, bool *unsafe) {
    size_t index;
    if (unsafe == NULL) return;
    if (!minic_core_reentry_index_ready(program) || membership == NULL) {
        *unsafe = true;
        return;
    }
    /* Case/default labels are independently dispatched; direct internal
     * goto edges disappear along with the discarded subtree. */
    ++minic_core_reentry_index.indexed_if_queries;
    for (index = 0U; index < program->statement_count && !*unsafe; ++index) {
        const MinicStatement *statement;
        size_t edge;
        if (!membership[index]) continue;
        statement = minic_c0_program_statement(program, index);
        if (statement == NULL || statement->kind == MINIC_STATEMENT_CASE ||
            statement->kind == MINIC_STATEMENT_DEFAULT ||
            minic_core_reentry_index.addressed[index] != 0U) {
            *unsafe = true;
            break;
        }
        for (edge = minic_core_reentry_index.heads[index]; edge != SIZE_MAX;
             edge = minic_core_reentry_index.incoming[edge].next) {
            size_t source = minic_core_reentry_index.incoming[edge].source_statement;
            if (!membership[source]) {
                *unsafe = true;
                break;
            }
        }
    }
}

bool minic_core_reentry_index_unreferenced_loop_label(
    const MinicC0Program *program, MinicStatementId label_id) {
    if (!minic_core_reentry_index_ready(program) ||
        label_id >= program->statement_count ||
        minic_core_reentry_index.heads[label_id] != SIZE_MAX ||
        minic_core_reentry_index.addressed[label_id] != 0U)
        return false;
    ++minic_core_reentry_index.indexed_loop_queries;
    return true;
}
