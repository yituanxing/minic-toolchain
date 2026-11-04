#include "compiler/inline_specialize_common.h"
#include "frontend/function_body.h"

#include <stdlib.h>
#include <string.h>

void minic_inline_spec_key_clear(MinicInlineSpecializationKey *key) {
    size_t i;
    if (key == NULL) return;
    (void)memset(key, 0, sizeof(*key));
    for (i = 0U; i < MINIC_MAX_FUNCTION_PARAMETERS; ++i)
        key->symbolic_expression[i] = MINIC_EXPRESSION_INVALID;
}

bool minic_inline_spec_key_from_function(
    MinicInlineSpecializationKey *key, const MinicFunction *function,
    size_t parameter_count) {
    size_t i;
    if (key == NULL || parameter_count > MINIC_MAX_FUNCTION_PARAMETERS)
        return false;
    minic_inline_spec_key_clear(key);
    if (function == NULL || !function->is_integer_specialization)
        return true;
    if (function->parameter_count < parameter_count) return false;
    for (i = 0U; i < parameter_count; ++i) {
        key->integer_known[i] = function->specialization_integer_known[i];
        key->integer_bits[i] = function->specialization_integer_bits[i];
        key->symbolic_known[i] = function->specialization_symbolic_known[i];
        key->symbolic_expression[i] = function->specialization_symbolic_known[i]
            ? function->specialization_symbolic_expression[i]
            : MINIC_EXPRESSION_INVALID;
        key->boolean_range_known[i] =
            function->specialization_boolean_range_known[i];
    }
    return true;
}

bool minic_inline_spec_section_matches(
    const MinicFunction *candidate, const MinicFunction *source,
    const MinicFunction *caller) {
    const MinicFunction *owner;
    const char *wanted;
    size_t length;
    if (candidate == NULL || source == NULL) return false;
    owner = source->section_name != NULL ? source : caller;
    wanted = owner != NULL ? owner->section_name : NULL;
    length = wanted != NULL ? owner->section_name_length : 0U;
    if ((candidate->section_name == NULL) != (wanted == NULL) ||
        candidate->section_name_length != length) return false;
    return wanted == NULL || memcmp(candidate->section_name, wanted, length) == 0;
}

bool minic_inline_spec_sections_present(const MinicC0Program *program) {
    size_t i;
    if (program == NULL) return false;
    for (i = 0U; i < program->function_count; ++i)
        if (program->functions[i].section_name != NULL) return true;
    return false;
}

bool minic_inline_spec_key_matches(
    const MinicFunction *candidate, const MinicFunction *source,
    const MinicFunction *caller, MinicFunctionId source_id,
    const MinicInlineSpecializationKey *key, unsigned int fact_mask,
    size_t parameter_count) {
    size_t i;
    if (candidate == NULL || source == NULL || key == NULL ||
        parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (fact_mask & ~(unsigned int)MINIC_INLINE_SPEC_FACT_ALL) != 0U ||
        !candidate->is_integer_specialization ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count ||
        !minic_inline_spec_section_matches(candidate, source, caller))
        return false;
    for (i = 0U; i < parameter_count; ++i) {
        if ((fact_mask & MINIC_INLINE_SPEC_FACT_INTEGER) != 0U &&
            (candidate->specialization_integer_known[i] != key->integer_known[i] ||
             (key->integer_known[i] &&
              candidate->specialization_integer_bits[i] != key->integer_bits[i])))
            return false;
        if ((fact_mask & MINIC_INLINE_SPEC_FACT_SYMBOLIC) != 0U &&
            (candidate->specialization_symbolic_known[i] != key->symbolic_known[i] ||
             (key->symbolic_known[i] &&
              candidate->specialization_symbolic_expression[i] !=
                  key->symbolic_expression[i])))
            return false;
        if ((fact_mask & MINIC_INLINE_SPEC_FACT_BOOLEAN) != 0U &&
            candidate->specialization_boolean_range_known[i] !=
                key->boolean_range_known[i])
            return false;
    }
    return true;
}

MinicFunctionId minic_inline_spec_find_variant(
    const MinicC0Program *program, const MinicFunction *source,
    const MinicFunction *caller, MinicFunctionId source_id,
    const MinicInlineSpecializationKey *key, unsigned int fact_mask,
    size_t first_index) {
    size_t i;
    if (program == NULL || source == NULL || key == NULL ||
        first_index > program->function_count ||
        source_id >= program->function_count ||
        source->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS)
        return MINIC_FUNCTION_INVALID;
    for (i = first_index; i < program->function_count; ++i)
        if (minic_inline_spec_key_matches(
                &program->functions[i], source, caller, source_id, key,
                fact_mask, source->parameter_count))
            return (MinicFunctionId)i;
    return MINIC_FUNCTION_INVALID;
}

void minic_inline_initial_owners_destroy(MinicInlineInitialOwners *owners) {
    if (owners == NULL) return;
    free(owners->owners);
    (void)memset(owners, 0, sizeof(*owners));
}

bool minic_inline_initial_owners_build(
    const MinicC0Program *program, size_t function_limit,
    MinicInlineInitialOwners *owners) {
    MinicFunctionId *data;
    if (program == NULL || owners == NULL || owners->owners != NULL ||
        owners->ready || owners->expression_count != 0U ||
        function_limit > program->function_count)
        return false;
    owners->function_limit = function_limit;
    owners->expression_count = program->expression_count;
    if (!minic_inline_spec_sections_present(program) ||
        program->expression_count == 0U) return true;
    if (program->expression_count > SIZE_MAX / sizeof(*data)) return false;
    data = malloc(program->expression_count * sizeof(*data));
    if (data == NULL) return false;
    if (!minic_perf_program_build_expression_source_owners(
            program, function_limit, data, program->expression_count)) {
        free(data);
        return false;
    }
    owners->owners = data;
    owners->ready = true;
    return true;
}

MinicFunctionId minic_inline_initial_owners_get(
    const MinicInlineInitialOwners *owners, size_t expression_index) {
    MinicFunctionId owner;
    if (owners == NULL || !owners->ready || owners->owners == NULL ||
        expression_index >= owners->expression_count)
        return MINIC_FUNCTION_INVALID;
    /* The ownership oracle represents ambiguity with a numerical sentinel.
     * A later clone append may make that number look like a live FunctionId;
     * only canonical source IDs below the captured limit are authoritative. */
    owner = owners->owners[expression_index];
    return owner < owners->function_limit ? owner : MINIC_FUNCTION_INVALID;
}

void minic_inline_call_worklist_destroy(MinicInlineCallWorklist *worklist) {
    if (worklist == NULL) return;
    free(worklist->offsets);
    free(worklist->calls);
    free(worklist->valid_counts);
    (void)memset(worklist, 0, sizeof(*worklist));
}

bool minic_inline_call_worklist_build(
    const MinicC0Program *program, size_t source_function_limit,
    MinicInlineCallWorklist *worklist) {
    MinicFunctionId *owners = NULL;
    size_t *offsets = NULL, *calls = NULL, *writes = NULL;
    size_t *valid_counts = NULL;
    size_t expression_count, i, n;
    if (program == NULL || worklist == NULL || worklist->program != NULL ||
        worklist->offsets != NULL || worklist->calls != NULL ||
        worklist->valid_counts != NULL ||
        worklist->ready || source_function_limit > program->function_count)
        return false;
    worklist->program = program;
    expression_count = program->expression_count;
    worklist->snapshot_expression_count = expression_count;
    worklist->source_function_limit = source_function_limit;
    /* Index allocation is optional. The live full-scan fallback below still
     * discovers all calls if ownership cannot be established or memory runs
     * out. Building it cannot change whether legal C is accepted. */
    if (expression_count == 0U ||
        expression_count > SIZE_MAX / sizeof(*owners) ||
        expression_count > SIZE_MAX / sizeof(*calls) ||
        source_function_limit > SIZE_MAX / sizeof(*offsets) - 2U)
        return true;
    n = source_function_limit;
    owners = malloc(expression_count * sizeof(*owners));
    if (owners == NULL ||
        !minic_perf_program_build_expression_source_owners(
            program, n, owners, expression_count)) goto fallback;
    worklist->owner_ready = true;
    offsets = calloc(n + 2U, sizeof(*offsets));
    calls = malloc(expression_count * sizeof(*calls));
    writes = malloc((n + 1U) * sizeof(*writes));
    valid_counts = calloc(n + 1U, sizeof(*valid_counts));
    if (offsets == NULL || calls == NULL || writes == NULL ||
        valid_counts == NULL) goto fallback;
    for (i = 0U; i < expression_count; ++i) {
        if (program->expressions[i].kind == MINIC_EXPRESSION_CALL) {
            MinicFunctionId owner = owners[i];
            size_t bucket = owner < n ? (size_t)owner : n;
            ++offsets[bucket + 1U];
            ++worklist->call_count;
            if (program->expressions[i].value.call.function_id !=
                MINIC_FUNCTION_INVALID) {
                ++valid_counts[bucket];
                ++worklist->valid_call_count;
            }
        }
    }
    for (i = 0U; i < n + 1U; ++i) offsets[i + 1U] += offsets[i];
    (void)memcpy(writes, offsets, (n + 1U) * sizeof(*writes));
    for (i = 0U; i < expression_count; ++i) {
        if (program->expressions[i].kind == MINIC_EXPRESSION_CALL) {
            MinicFunctionId owner = owners[i];
            size_t bucket = owner < n ? (size_t)owner : n;
            calls[writes[bucket]++] = i;
        }
    }
    worklist->offsets = offsets;
    worklist->calls = calls;
    worklist->valid_counts = valid_counts;
    worklist->ready = true;
    free(writes);
    free(owners);
    return true;
fallback:
    free(writes);
    free(calls);
    free(offsets);
    free(valid_counts);
    free(owners);
    worklist->call_count = 0U;
    worklist->valid_call_count = 0U;
    worklist->owner_ready = false;
    return true;
}

void minic_inline_call_cursor_begin(
    const MinicInlineCallWorklist *worklist,
    MinicFunctionId canonical_source_id, MinicInlineCallCursor *cursor) {
    size_t n;
    if (cursor == NULL) return;
    (void)memset(cursor, 0, sizeof(*cursor));
    cursor->worklist = worklist;
    if (worklist == NULL || !worklist->ready) return;
    n = worklist->source_function_limit;
    cursor->common_cursor = worklist->offsets[n];
    cursor->common_end = worklist->offsets[n + 1U];
    if (canonical_source_id < n) {
        cursor->own_cursor = worklist->offsets[canonical_source_id];
        cursor->own_end = worklist->offsets[canonical_source_id + 1U];
    }
    cursor->late_cursor = worklist->snapshot_expression_count;
    cursor->skipped_other_owner = worklist->valid_call_count -
        worklist->valid_counts[n] -
        (canonical_source_id < n
            ? worklist->valid_counts[canonical_source_id] : 0U);
}

bool minic_inline_call_cursor_next(
    MinicInlineCallCursor *cursor, const MinicC0Program *program,
    MinicExpressionId *expression_id) {
    const MinicInlineCallWorklist *worklist;
    if (cursor == NULL || program == NULL || expression_id == NULL ||
        (worklist = cursor->worklist) == NULL || worklist->program != program)
        return false;
    if (worklist->ready &&
        program->expression_count >= worklist->snapshot_expression_count) {
        if (cursor->own_cursor < cursor->own_end ||
            cursor->common_cursor < cursor->common_end) {
            if (cursor->common_cursor == cursor->common_end ||
                (cursor->own_cursor < cursor->own_end &&
                 worklist->calls[cursor->own_cursor] <
                     worklist->calls[cursor->common_cursor]))
                *expression_id = worklist->calls[cursor->own_cursor++];
            else
                *expression_id = worklist->calls[cursor->common_cursor++];
            return true;
        }
        while (cursor->late_cursor < program->expression_count) {
            size_t i = cursor->late_cursor++;
            if (program->expressions[i].kind == MINIC_EXPRESSION_CALL) {
                *expression_id = i;
                return true;
            }
        }
        return false;
    }
    while (cursor->full_cursor < program->expression_count) {
        size_t i = cursor->full_cursor++;
        if (program->expressions[i].kind == MINIC_EXPRESSION_CALL) {
            *expression_id = i;
            return true;
        }
    }
    return false;
}
