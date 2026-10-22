#include "frontend/initializer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Naive source-ordered reference independent of the reverse DSU algorithm. */
static void check_reference(const MinicArrayInitializerPlan *plan,
                            const MinicArrayInitializerResolved *r) {
    size_t i, action;
    const size_t extent = minic_array_initializer_plan_element_count(plan);
    size_t *counts = NULL;
    assert(r->element_count == extent && r->action_count == plan->action_count);
    if (plan->action_count != 0U) {
        counts = (size_t *)calloc(plan->action_count, sizeof(*counts));
        assert(counts != NULL);
    }
    for (i = 0U; i < extent; ++i) {
        size_t expected = MINIC_INITIALIZER_ACTION_INVALID;
        for (action = 0U; action < plan->action_count; ++action) {
            if (i >= plan->actions[action].first_index &&
                i <= plan->actions[action].last_index)
                expected = action;
        }
        assert(r->owners[i] == expected);
        if (expected != MINIC_INITIALIZER_ACTION_INVALID)
            ++counts[expected];
    }
    for (action = 0U; action < plan->action_count; ++action) {
        size_t seen = 0U, previous = MINIC_INITIALIZER_ACTION_INVALID;
        assert(r->final_counts[action] == counts[action]);
        for (i = r->first_owned[action];
             i != MINIC_INITIALIZER_ACTION_INVALID; i = r->next_owned[i]) {
            assert(i < extent && seen < extent);
            assert(r->owners[i] == action);
            assert(previous == MINIC_INITIALIZER_ACTION_INVALID || i > previous);
            previous = i;
            ++seen;
        }
        assert(seen == counts[action]);
    }
    free(counts);
}

static void test_fixed_and_invalid(void) {
    MinicArrayInitializerPlan plan;
    MinicArrayInitializerResolved r = {0};
    size_t id = 77U;
    minic_array_initializer_plan_initialize(&plan, 4U, false);
    assert(minic_array_initializer_plan_add_designated(&plan, 1U, 2U, &id) && id == 0U);
    assert(minic_array_initializer_plan_add_positional(&plan, &id) && id == 1U);
    assert(plan.next_index == 4U);
    assert(!minic_array_initializer_plan_add_positional(&plan, &id));
    assert(!minic_array_initializer_plan_add_designated(&plan, 4U, 4U, &id));
    assert(!minic_array_initializer_plan_add_designated(&plan, 3U, 2U, &id));
    assert(!minic_array_initializer_plan_add_designated(&plan, 0U, SIZE_MAX, &id));
    assert(!minic_array_initializer_plan_add_positional(NULL, &id));
    assert(!minic_array_initializer_plan_add_positional(&plan, NULL));
    assert(!minic_array_initializer_plan_resolve(NULL, &r));
    assert(!minic_array_initializer_plan_resolve(&plan, NULL));
    assert(minic_array_initializer_plan_resolve(&plan, &r));
    check_reference(&plan, &r);
    assert(r.owners[0] == MINIC_INITIALIZER_ACTION_INVALID);
    assert(r.owners[1] == 0U && r.owners[2] == 0U && r.owners[3] == 1U);
    minic_array_initializer_resolved_destroy(&r);
    minic_array_initializer_plan_destroy(&plan);
}

static void test_inferred_last_wins(void) {
    MinicArrayInitializerPlan plan;
    MinicArrayInitializerResolved r = {0};
    size_t id;
    minic_array_initializer_plan_initialize(&plan, 0U, true);
    assert(minic_array_initializer_plan_add_positional(&plan, &id) && id == 0U);
    assert(minic_array_initializer_plan_add_designated(&plan, 3U, 5U, &id) && id == 1U);
    assert(minic_array_initializer_plan_add_positional(&plan, &id) && id == 2U);
    assert(minic_array_initializer_plan_add_designated(&plan, 4U, 6U, &id) && id == 3U);
    assert(minic_array_initializer_plan_element_count(&plan) == 7U);
    assert(minic_array_initializer_plan_resolve(&plan, &r));
    check_reference(&plan, &r);
    assert(r.final_counts[0] == 1U && r.final_counts[1] == 1U);
    assert(r.final_counts[2] == 0U && r.final_counts[3] == 3U);
    minic_array_initializer_resolved_destroy(&r);
    minic_array_initializer_plan_destroy(&plan);
}

static uint32_t random_step(uint32_t *seed) {
    *seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
    return *seed;
}

static void test_dense_overlapping_ranges(void) {
    uint32_t seed = UINT32_C(0x52135487);
    size_t trial;
    for (trial = 0U; trial < 128U; ++trial) {
        MinicArrayInitializerPlan plan;
        MinicArrayInitializerResolved r = {0};
        size_t i, id;
        const size_t extent = 1U + (size_t)(random_step(&seed) % 96U);
        const size_t count = 1U + (size_t)(random_step(&seed) % 128U);
        minic_array_initializer_plan_initialize(&plan, extent, false);
        for (i = 0U; i < count; ++i) {
            size_t first = random_step(&seed) % extent;
            size_t last = random_step(&seed) % extent;
            if (first > last) {
                size_t swap = first;
                first = last;
                last = swap;
            }
            assert(minic_array_initializer_plan_add_designated(
                &plan, first, last, &id));
            assert(id == i);
        }
        assert(minic_array_initializer_plan_resolve(&plan, &r));
        check_reference(&plan, &r);
        minic_array_initializer_resolved_destroy(&r);
        minic_array_initializer_plan_destroy(&plan);
    }
}

static void test_empty(void) {
    MinicArrayInitializerPlan plan;
    MinicArrayInitializerResolved r = {0};
    minic_array_initializer_plan_initialize(&plan, 0U, true);
    assert(minic_array_initializer_plan_resolve(&plan, &r));
    check_reference(&plan, &r);
    minic_array_initializer_resolved_destroy(&r);
    minic_array_initializer_plan_destroy(&plan);
}

int main(void) {
    test_fixed_and_invalid();
    test_inferred_last_wins();
    test_dense_overlapping_ranges();
    test_empty();
    puts("PASS frontend/initializer-plan");
    return 0;
}
