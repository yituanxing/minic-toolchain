#include "frontend/initializer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_fixed_bound_positional_and_designated_sequence(void) {
    MinicArrayInitializerPlan plan;
    size_t designated_action;
    size_t positional_action;
    size_t owner;

    minic_array_initializer_plan_initialize(&plan, 4U, false);
    assert(minic_array_initializer_plan_element_count(&plan) == 4U);

    assert(minic_array_initializer_plan_add_designated(&plan, 1U, 2U, &designated_action));
    assert(designated_action == 0U);
    assert(plan.next_index == 3U);

    assert(minic_array_initializer_plan_add_positional(&plan, &positional_action));
    assert(positional_action == 1U);
    assert(plan.next_index == 4U);
    assert(plan.action_count == 2U);

    assert(minic_array_initializer_plan_final_owner(&plan, 0U, &owner));
    assert(owner == MINIC_INITIALIZER_ACTION_INVALID);
    assert(minic_array_initializer_plan_final_owner(&plan, 1U, &owner));
    assert(owner == designated_action);
    assert(minic_array_initializer_plan_final_owner(&plan, 2U, &owner));
    assert(owner == designated_action);
    assert(minic_array_initializer_plan_final_owner(&plan, 3U, &owner));
    assert(owner == positional_action);

    assert(minic_array_initializer_plan_action_final_count(&plan, designated_action) == 2U);
    assert(minic_array_initializer_plan_action_final_count(&plan, positional_action) == 1U);

    assert(!minic_array_initializer_plan_add_positional(&plan, &owner));
    assert(plan.action_count == 2U);
    assert(plan.next_index == 4U);
    assert(!minic_array_initializer_plan_add_designated(&plan, 4U, 4U, &owner));
    assert(!minic_array_initializer_plan_add_designated(&plan, 3U, 2U, &owner));
    assert(!minic_array_initializer_plan_add_designated(&plan, 0U, SIZE_MAX, &owner));
    assert(plan.action_count == 2U);

    assert(!minic_array_initializer_plan_final_owner(&plan, 4U, &owner));
    assert(!minic_array_initializer_plan_action_owns(&plan, 99U, 1U));
    assert(minic_array_initializer_plan_action_final_count(&plan, 99U) == 0U);

    minic_array_initializer_plan_destroy(&plan);
}

static void test_inferred_bound_overlap_and_last_wins(void) {
    MinicArrayInitializerPlan plan;
    size_t first_action;
    size_t range_action;
    size_t continued_action;
    size_t overwrite_action;
    size_t owner;

    minic_array_initializer_plan_initialize(&plan, 0U, true);
    assert(minic_array_initializer_plan_element_count(&plan) == 0U);

    assert(minic_array_initializer_plan_add_positional(&plan, &first_action));
    assert(first_action == 0U);
    assert(minic_array_initializer_plan_element_count(&plan) == 1U);

    assert(minic_array_initializer_plan_add_designated(&plan, 3U, 5U, &range_action));
    assert(range_action == 1U);
    assert(plan.next_index == 6U);
    assert(minic_array_initializer_plan_element_count(&plan) == 6U);

    assert(minic_array_initializer_plan_add_positional(&plan, &continued_action));
    assert(continued_action == 2U);
    assert(plan.next_index == 7U);
    assert(minic_array_initializer_plan_element_count(&plan) == 7U);

    assert(minic_array_initializer_plan_add_designated(&plan, 4U, 6U, &overwrite_action));
    assert(overwrite_action == 3U);
    assert(plan.next_index == 7U);
    assert(minic_array_initializer_plan_element_count(&plan) == 7U);

    assert(minic_array_initializer_plan_final_owner(&plan, 0U, &owner));
    assert(owner == first_action);
    assert(minic_array_initializer_plan_final_owner(&plan, 1U, &owner));
    assert(owner == MINIC_INITIALIZER_ACTION_INVALID);
    assert(minic_array_initializer_plan_final_owner(&plan, 2U, &owner));
    assert(owner == MINIC_INITIALIZER_ACTION_INVALID);
    assert(minic_array_initializer_plan_final_owner(&plan, 3U, &owner));
    assert(owner == range_action);
    assert(minic_array_initializer_plan_final_owner(&plan, 4U, &owner));
    assert(owner == overwrite_action);
    assert(minic_array_initializer_plan_final_owner(&plan, 5U, &owner));
    assert(owner == overwrite_action);
    assert(minic_array_initializer_plan_final_owner(&plan, 6U, &owner));
    assert(owner == overwrite_action);

    assert(minic_array_initializer_plan_action_final_count(&plan, first_action) == 1U);
    assert(minic_array_initializer_plan_action_final_count(&plan, range_action) == 1U);
    assert(minic_array_initializer_plan_action_final_count(&plan, continued_action) == 0U);
    assert(minic_array_initializer_plan_action_final_count(&plan, overwrite_action) == 3U);

    assert(minic_array_initializer_plan_action_owns(&plan, range_action, 3U));
    assert(!minic_array_initializer_plan_action_owns(&plan, range_action, 4U));
    assert(!minic_array_initializer_plan_action_owns(&plan, continued_action, 6U));
    assert(minic_array_initializer_plan_action_owns(&plan, overwrite_action, 6U));

    minic_array_initializer_plan_destroy(&plan);
}

static void test_invalid_inputs_do_not_publish_actions(void) {
    MinicArrayInitializerPlan plan;
    size_t action_id;

    minic_array_initializer_plan_initialize(&plan, 3U, false);

    action_id = 77U;
    assert(!minic_array_initializer_plan_add_designated(&plan, 2U, 1U, &action_id));
    assert(action_id == 77U);
    assert(plan.action_count == 0U);
    assert(plan.next_index == 0U);

    assert(!minic_array_initializer_plan_add_designated(&plan, 0U, SIZE_MAX, &action_id));
    assert(action_id == 77U);
    assert(plan.action_count == 0U);
    assert(plan.next_index == 0U);

    assert(!minic_array_initializer_plan_add_designated(&plan, 3U, 3U, &action_id));
    assert(action_id == 77U);
    assert(plan.action_count == 0U);
    assert(plan.next_index == 0U);

    assert(!minic_array_initializer_plan_add_positional(NULL, &action_id));
    assert(!minic_array_initializer_plan_add_positional(&plan, NULL));
    assert(!minic_array_initializer_plan_add_designated(NULL, 0U, 0U, &action_id));
    assert(!minic_array_initializer_plan_final_owner(NULL, 0U, &action_id));
    assert(!minic_array_initializer_plan_final_owner(&plan, 0U, NULL));

    minic_array_initializer_plan_destroy(&plan);
}

int main(void) {
    test_fixed_bound_positional_and_designated_sequence();
    test_inferred_bound_overlap_and_last_wins();
    test_invalid_inputs_do_not_publish_actions();
    puts("PASS frontend/initializer-plan");
    return 0;
}
