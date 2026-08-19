#include "frontend/initializer.h"

#include <stdint.h>
#include <stdlib.h>

static bool grow_actions(MinicArrayInitializerPlan *plan) {
    MinicArrayInitializerAction *new_actions;
    size_t new_capacity;

    if (plan->action_count < plan->action_capacity) {
        return true;
    }
    new_capacity = plan->action_capacity == 0U ? 8U : plan->action_capacity * 2U;
    if (new_capacity < plan->action_capacity || new_capacity > SIZE_MAX / sizeof(*plan->actions)) {
        return false;
    }
    new_actions = (MinicArrayInitializerAction *)realloc(plan->actions,
                                                         new_capacity * sizeof(*plan->actions));
    if (new_actions == NULL) {
        return false;
    }
    plan->actions = new_actions;
    plan->action_capacity = new_capacity;
    return true;
}

void minic_array_initializer_plan_initialize(MinicArrayInitializerPlan *plan,
                                             size_t declared_count,
                                             bool infer_bound) {
    if (plan == NULL) {
        return;
    }
    plan->actions = NULL;
    plan->action_count = 0U;
    plan->action_capacity = 0U;
    plan->declared_count = declared_count;
    plan->inferred_count = 0U;
    plan->next_index = 0U;
    plan->infer_bound = infer_bound;
}

void minic_array_initializer_plan_destroy(MinicArrayInitializerPlan *plan) {
    if (plan == NULL) {
        return;
    }
    free(plan->actions);
    minic_array_initializer_plan_initialize(plan, 0U, false);
}

static bool add_action(MinicArrayInitializerPlan *plan,
                       size_t first_index,
                       size_t last_index,
                       size_t *action_id) {
    size_t next_index;

    if (plan == NULL || action_id == NULL || first_index > last_index || last_index == SIZE_MAX ||
        (!plan->infer_bound && last_index >= plan->declared_count) || !grow_actions(plan)) {
        return false;
    }
    next_index = last_index + 1U;
    *action_id = plan->action_count;
    plan->actions[plan->action_count].first_index = first_index;
    plan->actions[plan->action_count].last_index = last_index;
    plan->action_count += 1U;
    plan->next_index = next_index;
    if (next_index > plan->inferred_count) {
        plan->inferred_count = next_index;
    }
    return true;
}

bool minic_array_initializer_plan_add_positional(MinicArrayInitializerPlan *plan,
                                                 size_t *action_id) {
    if (plan == NULL || plan->next_index == SIZE_MAX) {
        return false;
    }
    return add_action(plan, plan->next_index, plan->next_index, action_id);
}

bool minic_array_initializer_plan_add_designated(MinicArrayInitializerPlan *plan,
                                                 size_t first_index,
                                                 size_t last_index,
                                                 size_t *action_id) {
    return add_action(plan, first_index, last_index, action_id);
}

size_t minic_array_initializer_plan_element_count(const MinicArrayInitializerPlan *plan) {
    if (plan == NULL) {
        return 0U;
    }
    return plan->infer_bound ? plan->inferred_count : plan->declared_count;
}

bool minic_array_initializer_plan_final_owner(const MinicArrayInitializerPlan *plan,
                                              size_t element_index,
                                              size_t *action_id) {
    size_t index;
    size_t element_count;

    if (plan == NULL || action_id == NULL) {
        return false;
    }
    element_count = minic_array_initializer_plan_element_count(plan);
    if (element_index >= element_count) {
        return false;
    }
    for (index = plan->action_count; index > 0U; --index) {
        const MinicArrayInitializerAction *action;

        action = &plan->actions[index - 1U];
        if (element_index >= action->first_index && element_index <= action->last_index) {
            *action_id = index - 1U;
            return true;
        }
    }
    *action_id = MINIC_INITIALIZER_ACTION_INVALID;
    return true;
}

bool minic_array_initializer_plan_action_owns(const MinicArrayInitializerPlan *plan,
                                              size_t action_id,
                                              size_t element_index) {
    size_t owner;

    return plan != NULL && action_id < plan->action_count &&
           minic_array_initializer_plan_final_owner(plan, element_index, &owner) &&
           owner == action_id;
}

size_t minic_array_initializer_plan_action_final_count(const MinicArrayInitializerPlan *plan,
                                                       size_t action_id) {
    const MinicArrayInitializerAction *action;
    size_t element_count;
    size_t index;
    size_t count;

    if (plan == NULL || action_id >= plan->action_count) {
        return 0U;
    }
    action = &plan->actions[action_id];
    element_count = minic_array_initializer_plan_element_count(plan);
    count = 0U;
    for (index = action->first_index; index <= action->last_index && index < element_count;
         ++index) {
        if (minic_array_initializer_plan_action_owns(plan, action_id, index)) {
            count += 1U;
        }
        if (index == SIZE_MAX) {
            break;
        }
    }
    return count;
}
