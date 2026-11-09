#include "compiler/inline_specialize_common.h"
#include "frontend/function_body.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Opaque assembly's unparsed symbol references keep C names alive, including
 * when the same name is also a source for inline specializations. */
static bool minic_asm_text_mentions_symbol(const char *text,
                                           size_t text_length,
                                           const char *name,
                                           size_t name_length) {
    size_t offset;
    if (text == NULL || name == NULL || name_length == 0U ||
        name_length > text_length) {
        return false;
    }
    for (offset = 0U; offset <= text_length - name_length; ++offset) {
        if (memcmp(text + offset, name, name_length) == 0)
            return true;
    }
    return false;
}

bool minic_program_has_opaque_asm_symbol(const MinicC0Program *program,
                                         const char *name, size_t name_length) {
    size_t index;
    if (program == NULL || name == NULL || name_length == 0U)
        return true;
    for (index = 0U; index < program->file_asm_count; ++index) {
        const MinicFileAsm *source_asm = &program->file_asms[index];
        if (minic_asm_text_mentions_symbol(
                source_asm->text, source_asm->length, name, name_length))
            return true;
    }
    for (index = 0U; index < program->inline_asm_count; ++index) {
        const MinicInlineAsm *source_asm = &program->inline_asms[index];
        if (minic_asm_text_mentions_symbol(
                source_asm->template_text, source_asm->template_length,
                name, name_length))
            return true;
    }
    return false;
}

bool minic_inline_source_has_opaque_asm_reference(
    const MinicC0Program *program, const MinicFunction *source) {
    if (source == NULL) return true;
    return minic_program_has_opaque_asm_symbol(
        program, source->name, source->name_length);
}

bool minic_inline_call_plan_initialize(
    MinicInlineCallPlan *plan, const MinicC0Program *program) {
    if (plan == NULL || program == NULL || plan->program != NULL ||
        plan->selected != NULL || plan->selected_count != 0U ||
        plan->source_owners != NULL || plan->source_owner_count != 0U ||
        plan->source_function_limit != 0U)
        return false;
    plan->program = program;
    return true;
}

MinicFunctionId minic_inline_call_plan_get(
    const MinicInlineCallPlan *plan, MinicExpressionId expression_id) {
    const MinicC0Program *program;
    if (plan == NULL || (program = plan->program) == NULL ||
        expression_id >= program->expression_count ||
        program->expressions[expression_id].kind != MINIC_EXPRESSION_CALL)
        return MINIC_FUNCTION_INVALID;
    if (expression_id < plan->selected_count)
        return plan->selected[expression_id];
    /* A newly appended expression has never been specialized. The AST is
     * canonical and can safely supply its original target without copying. */
    return program->expressions[expression_id].value.call.function_id;
}

bool minic_inline_call_plan_set(
    MinicInlineCallPlan *plan, MinicExpressionId expression_id,
    MinicFunctionId function_id) {
    const MinicC0Program *program;
    size_t i, wanted_count;
    MinicFunctionId *selected;
    if (plan == NULL || (program = plan->program) == NULL ||
        expression_id >= program->expression_count ||
        program->expressions[expression_id].kind != MINIC_EXPRESSION_CALL ||
        function_id >= program->function_count ||
        expression_id == SIZE_MAX)
        return false;
    if (expression_id >= plan->selected_count) {
        wanted_count = (size_t)expression_id + 1U;
        if (wanted_count > SIZE_MAX / sizeof(*selected))
            return false;
        /* Realloc never commits partial state on OOM. Initialize every
         * unplanned earlier ID from the canonical arena, which may itself
         * have moved while new clones were appended. */
        selected = realloc(plan->selected, wanted_count * sizeof(*selected));
        if (selected == NULL) return false;
        for (i = plan->selected_count; i < wanted_count; ++i)
            selected[i] = program->expressions[i].kind == MINIC_EXPRESSION_CALL
                ? program->expressions[i].value.call.function_id
                : MINIC_FUNCTION_INVALID;
        plan->selected = selected;
        plan->selected_count = wanted_count;
    }
    plan->selected[expression_id] = function_id;
    return true;
}

void minic_inline_call_plan_destroy(MinicInlineCallPlan *plan) {
    if (plan == NULL) return;
    free(plan->selected);
    free(plan->source_owners);
    (void)memset(plan, 0, sizeof(*plan));
}

/* The canonical FunctionBody graph is immutable throughout the three inline
 * specialization passes.  Their generated functions are marked specialized
 * and borrow existing bodies; appended symbolic expressions do not become
 * children of canonical source bodies.  This snapshot therefore needs one
 * verified owner walk, not a full traversal for every direct/transitive pass.
 * Unknown and ambiguous ownership is normalized to INVALID so a later clone
 * count cannot accidentally turn the old ambiguity sentinel into an owner.
 * If a future producer adds an ordinary function, use the original oracle. */
bool minic_inline_call_plan_copy_source_owners(
    MinicInlineCallPlan *plan, size_t function_limit,
    MinicFunctionId *owners, size_t owner_count) {
    const MinicC0Program *program;
    size_t i;
    if (plan == NULL || (program = plan->program) == NULL ||
        (owner_count != 0U && owners == NULL) ||
        owner_count != program->expression_count ||
        function_limit > program->function_count)
        return false;
    if (plan->source_owners != NULL &&
        function_limit >= plan->source_function_limit &&
        owner_count >= plan->source_owner_count) {
        bool source_prefix_stable = true;
        for (i = plan->source_function_limit; i < function_limit; ++i) {
            if (!program->functions[i].is_integer_specialization) {
                source_prefix_stable = false;
                break;
            }
        }
        if (source_prefix_stable) {
            (void)memcpy(owners, plan->source_owners,
                         plan->source_owner_count * sizeof(*owners));
            for (i = plan->source_owner_count; i < owner_count; ++i)
                owners[i] = MINIC_FUNCTION_INVALID;
            /* Opt-in exact original-oracle check for frozen Linux/clone-heavy
             * acceptance. Never let an invalidated source graph silently
             * reuse stale ownership during a diagnostic run. */
            if (getenv("MINIC_INLINE_OWNER_CACHE_VERIFY") != NULL) {
                MinicFunctionId *fresh;
                bool matches = true;
                if (owner_count > SIZE_MAX / sizeof(*fresh)) return false;
                fresh = malloc((owner_count == 0U ? 1U : owner_count) *
                               sizeof(*fresh));
                if (fresh == NULL) return false;
                if (!minic_perf_program_build_expression_source_owners(
                        program, function_limit, fresh, owner_count)) {
                    free(fresh);
                    return false;
                }
                for (i = 0U; i < owner_count; ++i) {
                    const MinicFunctionId actual = fresh[i] < function_limit
                        ? fresh[i] : MINIC_FUNCTION_INVALID;
                    if (owners[i] != actual) {
                        (void)fprintf(stderr,
                            "MINIC_INLINE_OWNER_CACHE_MISMATCH expression=%zu "
                            "cached=%zu actual=%zu limit=%zu\n",
                            i, (size_t)owners[i], (size_t)actual, function_limit);
                        matches = false;
                        break;
                    }
                }
                free(fresh);
                if (!matches) return false;
            }
            return true;
        }
    }
    if (!minic_perf_program_build_expression_source_owners(
            program, function_limit, owners, owner_count))
        return false;
    /* Optional cache: OOM must retain exactly the old verified owner result. */
    if (plan->source_owners == NULL && owner_count != 0U &&
        owner_count <= SIZE_MAX / sizeof(*owners)) {
        MinicFunctionId *snapshot = malloc(owner_count * sizeof(*owners));
        if (snapshot != NULL) {
            for (i = 0U; i < owner_count; ++i)
                snapshot[i] = owners[i] < function_limit
                    ? owners[i] : MINIC_FUNCTION_INVALID;
            plan->source_owners = snapshot;
            plan->source_owner_count = owner_count;
            plan->source_function_limit = function_limit;
        }
    }
    return true;
}

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
    MinicInlineCallPlan *call_plan,
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
    if (!minic_inline_call_plan_copy_source_owners(
            call_plan, function_limit, data, program->expression_count)) {
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
    MinicInlineCallPlan *call_plan,
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
        !minic_inline_call_plan_copy_source_owners(
            call_plan, n, owners, expression_count)) goto fallback;
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
