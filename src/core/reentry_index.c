#include "core/reentry_index.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read-only incoming control-flow references for one Core Function Set build.
 * Owned by minic_prepare_core_function_set() and explicitly passed to lowerings.
 * Standalone users of minic_core_lower_function() keep the legacy proof path.
 * Invalid AST or allocation failure disables the index: never infer absence
 * of a goto, asm-goto, or label address from an incomplete index. No target
 * ABI or physical machine resource belongs in this layer. */
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

static bool core_reentry_query_contains(
    const MinicCoreReentryQuery *query, MinicStatementId target,
    size_t statement_count) {
    if (query == NULL || target >= statement_count) return false;
    return query->membership != NULL
        ? query->membership[target] : target == query->label_id;
}

static unsigned int core_reentry_indexed_target(
    const MinicCoreReentryIndex *index,
    const MinicC0Program *program,
    const MinicCoreReentryQuery *query,
    MinicStatementId target) {
    const MinicStatement *statement;
    unsigned int reasons = 0U;
    size_t edge;

    statement = minic_c0_program_statement(program, target);
    if (statement == NULL)
        return MINIC_CORE_REENTRY_REASON_INDETERMINATE;
    if ((query->checks & MINIC_CORE_REENTRY_CHECK_CASE_DISPATCH) != 0U &&
        (statement->kind == MINIC_STATEMENT_CASE ||
         statement->kind == MINIC_STATEMENT_DEFAULT))
        reasons |= MINIC_CORE_REENTRY_REASON_CASE_DISPATCH;
    if ((query->checks & MINIC_CORE_REENTRY_CHECK_LABEL_ADDRESS) != 0U &&
        index->addressed[target] != 0U)
        reasons |= MINIC_CORE_REENTRY_REASON_LABEL_ADDRESS;
    if ((query->checks & MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP) == 0U)
        return reasons;

    for (edge = index->heads[target]; edge != SIZE_MAX;
         edge = index->incoming[edge].next) {
        size_t source_id = index->incoming[edge].source_statement;
        const MinicStatement *source;
        if (core_reentry_query_contains(query, source_id,
                                        program->statement_count))
            continue;
        source = minic_c0_program_statement(program, source_id);
        if (source == NULL)
            return reasons | MINIC_CORE_REENTRY_REASON_INDETERMINATE;
        if (source->kind == MINIC_STATEMENT_GOTO)
            reasons |= MINIC_CORE_REENTRY_REASON_DIRECT_GOTO;
        else if (source->kind == MINIC_STATEMENT_INLINE_ASM)
            reasons |= MINIC_CORE_REENTRY_REASON_ASM_GOTO;
        else
            reasons |= MINIC_CORE_REENTRY_REASON_INDETERMINATE;
    }
    return reasons;
}

static unsigned int core_reentry_fallback(
    const MinicC0Program *program,
    const MinicCoreReentryQuery *query) {
    unsigned int reasons = 0U;
    size_t i;

    if ((query->checks & MINIC_CORE_REENTRY_CHECK_CASE_DISPATCH) != 0U) {
        for (i = 0U; i < program->statement_count; ++i) {
            const MinicStatement *statement;
            if (!core_reentry_query_contains(query, i, program->statement_count))
                continue;
            statement = minic_c0_program_statement(program, i);
            if (statement == NULL)
                return reasons | MINIC_CORE_REENTRY_REASON_INDETERMINATE;
            if (statement->kind == MINIC_STATEMENT_CASE ||
                statement->kind == MINIC_STATEMENT_DEFAULT)
                reasons |= MINIC_CORE_REENTRY_REASON_CASE_DISPATCH;
        }
    }
    if ((query->checks & MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP) != 0U) {
        for (i = 0U; i < program->statement_count; ++i) {
            const MinicStatement *source =
                minic_c0_program_statement(program, i);
            if (source == NULL)
                return reasons | MINIC_CORE_REENTRY_REASON_INDETERMINATE;
            if (core_reentry_query_contains(query, i, program->statement_count))
                continue;
            if (source->kind == MINIC_STATEMENT_GOTO &&
                core_reentry_query_contains(
                    query, source->target_statement, program->statement_count)) {
                reasons |= MINIC_CORE_REENTRY_REASON_DIRECT_GOTO;
            } else if (source->kind == MINIC_STATEMENT_INLINE_ASM) {
                const MinicInlineAsm *code;
                size_t label_index;
                if (source->inline_asm_id >= program->inline_asm_count)
                    return reasons | MINIC_CORE_REENTRY_REASON_INDETERMINATE;
                code = &program->inline_asms[source->inline_asm_id];
                if (!code->is_goto) continue;
                if (code->label_count != 0U && code->labels == NULL)
                    return reasons | MINIC_CORE_REENTRY_REASON_INDETERMINATE;
                for (label_index = 0U; label_index < code->label_count; ++label_index)
                    if (core_reentry_query_contains(
                            query, code->labels[label_index].target_statement,
                            program->statement_count))
                        reasons |= MINIC_CORE_REENTRY_REASON_ASM_GOTO;
            }
        }
    }
    if ((query->checks & MINIC_CORE_REENTRY_CHECK_LABEL_ADDRESS) != 0U) {
        for (i = 0U; i < program->expression_count; ++i) {
            const MinicExpression *expression =
                minic_c0_program_expression(program, i);
            if (expression == NULL)
                return reasons | MINIC_CORE_REENTRY_REASON_INDETERMINATE;
            if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
                core_reentry_query_contains(
                    query, expression->value.label_statement_id,
                    program->statement_count))
                reasons |= MINIC_CORE_REENTRY_REASON_LABEL_ADDRESS;
        }
    }
    return reasons;
}

unsigned int minic_core_reentry_query(
    MinicCoreReentryIndex *index, const MinicC0Program *program,
    const MinicCoreReentryQuery *query) {
    const unsigned int all_checks =
        MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP |
        MINIC_CORE_REENTRY_CHECK_LABEL_ADDRESS |
        MINIC_CORE_REENTRY_CHECK_CASE_DISPATCH;
    unsigned int reasons = 0U;
    size_t target;

    if (program == NULL || query == NULL ||
        query->checks == 0U || (query->checks & ~all_checks) != 0U ||
        (query->membership == NULL &&
         query->label_id >= program->statement_count) ||
        (query->membership != NULL &&
         query->label_id != MINIC_STATEMENT_INVALID))
        return MINIC_CORE_REENTRY_REASON_INDETERMINATE;

    if (!minic_core_reentry_index_ready(index, program))
        return core_reentry_fallback(program, query);

    if (query->purpose == MINIC_CORE_REENTRY_PURPOSE_IF_DISCARDED)
        ++index->indexed_if_queries;

    if (query->membership == NULL) {
        reasons = core_reentry_indexed_target(
            index, program, query, query->label_id);
    } else {
        for (target = 0U; target < program->statement_count; ++target) {
            if (!query->membership[target]) continue;
            reasons |= core_reentry_indexed_target(
                index, program, query, target);
            if ((reasons & MINIC_CORE_REENTRY_REASON_INDETERMINATE) != 0U)
                break;
        }
    }
    /* Preserve the old performance counter's semantics: only successful,
     * unreferenced synthetic loop-label queries count as indexed successes.
     * Source-shape uniqueness is still checked by the loop owner. */
    if (query->purpose == MINIC_CORE_REENTRY_PURPOSE_LOOP_LABEL &&
        reasons == 0U)
        ++index->indexed_loop_queries;
    return reasons;
}
