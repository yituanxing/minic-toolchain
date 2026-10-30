#include "core/local_facts.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Own only fact-table state, snapshots, invalidation/escape, and safe
 * reads. Whole-program zero invariants still consult CFG reentry in
 * core_lower.c, not a machine backend or this state-only module. */
/* LINUX_LOCAL_INTEGER_CONSTANTS_V0
 *
 * This is intentionally a tiny straight-line fact table, not an optimizer.
 * A fact is admitted only for an unescaped non-volatile direct local integer.
 * Unknown CFG joins discard all facts.  The purpose is to preserve C/GNU
 * compile-time reachability idioms after their constant value has travelled
 * through a local object (notably Linux BUILD_BUG_ON/compiletime_assert). */
bool minic_core_local_fact_index(const MinicCoreLowerContext *context,
                                      MinicLocalId local_id,
                                      size_t *index) {
    if (context == NULL || context->source_function == NULL || index == NULL ||
        local_id < context->source_function->local_begin) {
        return false;
    }
    *index = local_id - context->source_function->local_begin;
    return *index < context->source_function->local_count &&
           context->local_integer_constants != NULL;
}



bool minic_core_inline_spec_fact_trace_enabled(const MinicCoreLowerContext *context) {
    const char *text;
    char *end;
    unsigned long long source_id;

    if (context == NULL || context->source_function == NULL ||
        !context->source_function->is_integer_specialization) {
        return false;
    }
    text = getenv("MINIC_INLINE_SPEC_FACT_TRACE_SOURCE");
    if (text == NULL || *text == '\0') {
        return false;
    }
    end = NULL;
    source_id = strtoull(text, &end, 10);
    return end != text && end != NULL && *end == '\0' &&
           source_id == (unsigned long long)context->source_function->specialization_source;
}

/* M179_TERMINATING_GUARD_FACTS: Core builds branch bodies before it emits
 * the condition block.  Keep one copy of the straight-line facts that reach an
 * uncertain if so condition lowering sees the correct incoming state.  When a
 * no-else true arm terminates (the common `if (err) return/goto` guard), the
 * only continuation is the false edge, so facts surviving condition evaluation
 * remain valid there instead of being discarded at a fictitious merge. */
MinicCoreLocalIntegerConstant *minic_core_local_facts_snapshot(
    const MinicCoreLowerContext *context) {
    MinicCoreLocalIntegerConstant *copy;
    size_t count;

    if (context == NULL || context->source_function == NULL) {
        return NULL;
    }
    count = context->source_function->local_count;
    if (count == 0U) {
        return NULL;
    }
    if (context->local_integer_constants == NULL ||
        count > SIZE_MAX / sizeof(*copy)) {
        return NULL;
    }
    copy = (MinicCoreLocalIntegerConstant *)malloc(count * sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }
    (void)memcpy(copy,
                 context->local_integer_constants,
                 count * sizeof(*copy));
    return copy;
}

void minic_core_local_facts_restore(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *snapshot) {
    size_t count;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || snapshot == NULL) {
        return;
    }
    count = context->source_function->local_count;
    if (count == 0U || count > SIZE_MAX / sizeof(*snapshot)) {
        return;
    }
    (void)memcpy(context->local_integer_constants,
                 snapshot,
                 count * sizeof(*snapshot));
}

bool minic_core_local_facts_meet(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *left,
    const MinicCoreLocalIntegerConstant *right) {
    size_t index;

    if (context == NULL || context->source_function == NULL) return false;
    if (context->source_function->local_count == 0U) return true;
    if (context->local_integer_constants == NULL || left == NULL || right == NULL)
        return false;
    for (index = 0U; index < context->source_function->local_count; ++index) {
        const MinicCoreLocalIntegerConstant *lhs = &left[index];
        const MinicCoreLocalIntegerConstant *rhs = &right[index];
        MinicCoreLocalIntegerConstant merged = *lhs;
        bool same_value = lhs->known && rhs->known &&
            !lhs->escaped && !rhs->escaped &&
            minic_type_equal(lhs->value.type, rhs->value.type) &&
            lhs->value.bits == rhs->value.bits;

        merged.escaped = lhs->escaped || rhs->escaped;
        merged.program_zero_checked =
            lhs->program_zero_checked && rhs->program_zero_checked;
        merged.program_zero_stable =
            lhs->program_zero_stable && rhs->program_zero_stable;
        merged.specialization_modified =
            lhs->specialization_modified || rhs->specialization_modified;
        merged.specialization_known =
            lhs->specialization_known && rhs->specialization_known &&
            minic_type_equal(lhs->specialization_value.type,
                             rhs->specialization_value.type) &&
            lhs->specialization_value.bits == rhs->specialization_value.bits;
        merged.known = same_value && !merged.escaped;
        merged.possible_one_bits_valid = !merged.escaped &&
            (lhs->known || lhs->possible_one_bits_valid) &&
            (rhs->known || rhs->possible_one_bits_valid);
        merged.possible_one_bits = merged.possible_one_bits_valid
            ? (lhs->known ? lhs->value.bits : lhs->possible_one_bits) |
              (rhs->known ? rhs->value.bits : rhs->possible_one_bits)
            : UINT64_MAX;
        merged.nonnegative_range_valid = !merged.escaped &&
            lhs->nonnegative_range_valid && rhs->nonnegative_range_valid;
        if (merged.nonnegative_range_valid) {
            merged.nonnegative_min = lhs->nonnegative_min < rhs->nonnegative_min
                ? lhs->nonnegative_min : rhs->nonnegative_min;
            merged.nonnegative_max = lhs->nonnegative_max > rhs->nonnegative_max
                ? lhs->nonnegative_max : rhs->nonnegative_max;
        }
        context->local_integer_constants[index] = merged;
    }
    return true;
}

void minic_core_local_facts_clear_known(MinicCoreLowerContext *context) {
    size_t index;

    if (getenv("MINIC_LOCAL_FACT_TRACE") != NULL && context != NULL &&
        context->source_function != NULL) {
        (void)fprintf(stderr,
                      "LOCAL_FACT_CLEAR function=%s\n",
                      context->source_function->name != NULL ? context->source_function->name : "?");
    }
    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL) {
        return;
    }
    for (index = 0U; index < context->source_function->local_count; ++index) {
        MinicCoreLocalIntegerConstant *fact = &context->local_integer_constants[index];
        if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
            context->source_function->name != NULL &&
            strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0 &&
            fact->known) {
            (void)fprintf(stderr, "SHMEM_FACT_EVENT event=clear local=%zu bits=%llu escaped=%d\n",
                (size_t)(context->source_function->local_begin + index),
                (unsigned long long)fact->value.bits, fact->escaped ? 1 : 0);
        }
        if (fact->specialization_known && !fact->specialization_modified &&
            !fact->escaped) {
            fact->value = fact->specialization_value;
            fact->known = true;
        } else if (fact->program_zero_stable && !fact->escaped) {
            fact->known = true;
        } else {
            fact->known = false;
        }
        fact->possible_one_bits_valid = fact->known && !fact->escaped;
        fact->possible_one_bits = fact->possible_one_bits_valid
            ? fact->value.bits : UINT64_MAX;
        fact->nonnegative_range_valid = false;
        if (fact->specialization_known && minic_core_inline_spec_fact_trace_enabled(context)) {
            (void)fprintf(stderr,
                          "INLINE_SPEC_FACT event=clear function=%s source=%zu local=%zu known=%d modified=%d escaped=%d bits=%" PRIu64 "\n",
                          context->source_function->name != NULL
                              ? context->source_function->name
                              : "?",
                          (size_t)context->source_function->specialization_source,
                          (size_t)(context->source_function->local_begin + index),
                          fact->known ? 1 : 0,
                          fact->specialization_modified ? 1 : 0,
                          fact->escaped ? 1 : 0,
                          fact->specialization_value.bits);
        }
    }
}

void minic_core_local_fact_invalidate(MinicCoreLowerContext *context,
                                           MinicLocalId local_id) {
    size_t index;
    if (minic_core_local_fact_index(context, local_id, &index)) {
        MinicCoreLocalIntegerConstant *fact = &context->local_integer_constants[index];
        fact->known = false;
        fact->possible_one_bits_valid = false;
        fact->nonnegative_range_valid = false;
        if (fact->specialization_known) {
            fact->specialization_modified = true;
            if (minic_core_inline_spec_fact_trace_enabled(context)) {
                (void)fprintf(stderr,
                              "INLINE_SPEC_FACT event=invalidate function=%s source=%zu local=%zu bits=%" PRIu64 "\n",
                              context->source_function->name != NULL
                                  ? context->source_function->name
                                  : "?",
                              (size_t)context->source_function->specialization_source,
                              (size_t)local_id,
                              fact->specialization_value.bits);
            }
        }
    }
}

void minic_core_local_fact_escape(MinicCoreLowerContext *context,
                                       MinicLocalId local_id) {
    size_t index;
    if (minic_core_local_fact_index(context, local_id, &index)) {
        MinicCoreLocalIntegerConstant *fact = &context->local_integer_constants[index];
        fact->known = false;
        fact->escaped = true;
        fact->possible_one_bits_valid = false;
        fact->nonnegative_range_valid = false;
        if (fact->specialization_known) {
            fact->specialization_modified = true;
            if (minic_core_inline_spec_fact_trace_enabled(context)) {
                (void)fprintf(stderr,
                              "INLINE_SPEC_FACT event=escape function=%s source=%zu local=%zu bits=%" PRIu64 "\n",
                              context->source_function->name != NULL
                                  ? context->source_function->name
                                  : "?",
                              (size_t)context->source_function->specialization_source,
                              (size_t)local_id,
                              fact->specialization_value.bits);
            }
        }
    }
}

bool minic_core_local_fact_get(const MinicCoreLowerContext *context,
                                    MinicLocalId local_id,
                                    MinicConstValue *value) {
    const MinicLocal *local;
    size_t index;

    if (minic_core_local_fact_index(context, local_id, &index) &&
        context->local_integer_constants[index].specialization_known &&
        minic_core_inline_spec_fact_trace_enabled(context)) {
        const MinicCoreLocalIntegerConstant *fact = &context->local_integer_constants[index];
        (void)fprintf(stderr,
                      "INLINE_SPEC_FACT event=get function=%s source=%zu local=%zu known=%d modified=%d escaped=%d bits=%" PRIu64 "\n",
                      context->source_function->name != NULL
                          ? context->source_function->name
                          : "?",
                      (size_t)context->source_function->specialization_source,
                      (size_t)local_id,
                      fact->known ? 1 : 0,
                      fact->specialization_modified ? 1 : 0,
                      fact->escaped ? 1 : 0,
                      fact->specialization_value.bits);
    }
    if (value == NULL || !minic_core_local_fact_index(context, local_id, &index) ||
        !context->local_integer_constants[index].known ||
        context->local_integer_constants[index].escaped || context->body == NULL ||
        context->body->program == NULL) {
        return false;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    if (local == NULL || minic_type_is_volatile(local->type)) {
        return false;
    }
    *value = context->local_integer_constants[index].value;
    return true;
}

bool minic_core_local_fact_possible_one_bits(
    const MinicCoreLowerContext *context, MinicLocalId local_id,
    uint64_t *bits) {
    const MinicLocal *local;
    const MinicCoreLocalIntegerConstant *fact;
    size_t index;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || bits == NULL ||
        !minic_core_local_fact_index(context, local_id, &index)) {
        return false;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    fact = &context->local_integer_constants[index];
    if (local == NULL || local->is_array ||
        !minic_type_is_integer(local->type) ||
        minic_type_is_bool_integer(local->type) ||
        minic_type_is_volatile(local->type) || fact->escaped ||
        (!fact->known && !fact->possible_one_bits_valid)) {
        return false;
    }
    *bits = fact->known ? fact->value.bits : fact->possible_one_bits;
    return true;
}

bool minic_core_local_fact_or_constant(
    MinicCoreLowerContext *context, MinicLocalId local_id,
    const MinicConstValue *rhs) {
    const MinicLocal *local;
    MinicCoreLocalIntegerConstant *fact;
    MinicConstValue converted;
    size_t index;
    uint64_t previous;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || context->target == NULL ||
        rhs == NULL ||
        !minic_core_local_fact_index(context, local_id, &index)) {
        return false;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    fact = &context->local_integer_constants[index];
    if (local == NULL || local->is_array ||
        !minic_type_is_integer(local->type) ||
        minic_type_is_bool_integer(local->type) ||
        minic_type_is_volatile(local->type) || fact->escaped ||
        (!fact->known && !fact->possible_one_bits_valid) ||
        !minic_const_value_convert_integer(context->body->program,
                                           context->target, rhs,
                                           local->type, &converted)) {
        return false;
    }
    previous = fact->known ? fact->value.bits : fact->possible_one_bits;
    fact->possible_one_bits = previous | converted.bits;
    fact->possible_one_bits_valid = true;
    fact->nonnegative_range_valid = false;
    if (fact->known) {
        fact->value.bits |= converted.bits;
    }
    if (converted.bits != 0U) {
        fact->program_zero_stable = false;
    }
    return true;
}

bool minic_core_local_fact_nonnegative_range_get(
    const MinicCoreLowerContext *context, MinicLocalId local_id,
    uint64_t *minimum, uint64_t *maximum) {
    const MinicLocal *local;
    const MinicCoreLocalIntegerConstant *fact;
    unsigned int width;
    size_t index;
    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || context->target == NULL ||
        minimum == NULL || maximum == NULL ||
        !minic_core_local_fact_index(context, local_id, &index)) return false;
    local = minic_c0_program_local(context->body->program, local_id);
    fact = &context->local_integer_constants[index];
    if (local == NULL || local->is_array || !minic_type_is_integer(local->type) ||
        minic_type_is_bool_integer(local->type) ||
        minic_type_is_volatile(local->type) || fact->escaped ||
        !minic_target_info_integer_width(context->target,
            context->body->program, local->type, &width) ||
        width == 0U || width > 64U) return false;
    if (fact->known) {
        uint64_t value = fact->value.bits;
        uint64_t width_mask = width == 64U ? UINT64_MAX :
            (UINT64_C(1) << width) - UINT64_C(1);
        value &= width_mask;
        if (minic_type_is_signed_integer(local->type) &&
            (value & (UINT64_C(1) << (width - 1U))) != 0U) return false;
        *minimum = *maximum = value;
        return true;
    }
    if (!fact->nonnegative_range_valid) return false;
    *minimum = fact->nonnegative_min;
    *maximum = fact->nonnegative_max;
    return true;
}

bool minic_core_local_fact_nonnegative_range_set(
    MinicCoreLowerContext *context, MinicLocalId local_id,
    uint64_t minimum, uint64_t maximum) {
    const MinicLocal *local;
    MinicCoreLocalIntegerConstant *fact;
    unsigned int width;
    uint64_t signed_limit;
    size_t index;
    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || context->target == NULL ||
        minimum > maximum ||
        !minic_core_local_fact_index(context, local_id, &index)) return false;
    local = minic_c0_program_local(context->body->program, local_id);
    fact = &context->local_integer_constants[index];
    if (local == NULL || local->is_array || !minic_type_is_integer(local->type) ||
        minic_type_is_bool_integer(local->type) ||
        minic_type_is_volatile(local->type) || fact->escaped ||
        !minic_target_info_integer_width(context->target,
            context->body->program, local->type, &width) ||
        width == 0U || width > 64U) return false;
    signed_limit = minic_type_is_signed_integer(local->type)
        ? (UINT64_C(1) << (width - 1U)) - UINT64_C(1)
        : (width == 64U ? UINT64_MAX :
           (UINT64_C(1) << width) - UINT64_C(1));
    if (maximum > signed_limit) return false;
    fact->nonnegative_min = minimum;
    fact->nonnegative_max = maximum;
    fact->nonnegative_range_valid = true;
    return true;
}



void minic_core_local_null_pointer_set(MinicCoreLowerContext *context,
                                        MinicLocalId local_id) {
    const MinicLocal *local;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        !minic_core_local_fact_index(context, local_id, &index) ||
        context->local_integer_constants[index].escaped) {
        return;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    if (local == NULL || local->is_array || minic_type_is_volatile(local->type) ||
        !minic_type_is_pointer(local->type)) {
        context->local_integer_constants[index].known = false;
        return;
    }
    context->local_integer_constants[index].value.type = minic_type_int();
    context->local_integer_constants[index].value.bits = 0U;
    context->local_integer_constants[index].known = true;
}

bool minic_core_local_null_pointer_get(const MinicCoreLowerContext *context,
                                        MinicLocalId local_id) {
    const MinicLocal *local;
    size_t index;

    if (!minic_core_local_fact_index(context, local_id, &index) ||
        !context->local_integer_constants[index].known ||
        context->local_integer_constants[index].escaped || context->body == NULL ||
        context->body->program == NULL) {
        return false;
    }
    local = minic_c0_program_local(context->body->program, local_id);
    return local != NULL && !local->is_array && !minic_type_is_volatile(local->type) &&
           minic_type_is_pointer(local->type) &&
           minic_type_is_integer(context->local_integer_constants[index].value.type) &&
           context->local_integer_constants[index].value.bits == 0U;
}

