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

/* Find the next element not assigned to a later initializer action.
 * This iterative path compression avoids recursion for large range chains. */
static size_t initializer_find_unclaimed(size_t *successor,
                                         size_t extent, size_t index) {
    size_t root = index;
    while (root < extent && successor[root] != root)
        root = successor[root];
    while (index < extent && successor[index] != index) {
        const size_t next = successor[index];
        successor[index] = root;
        index = next;
    }
    return root;
}

/* Reverse ownership resolution with an interval skip-set. Every slot is
 * claimed at most once, including repeated [0 ... n] overwrites, rather than
 * checking every action for each slot. The resulting forward chains recover
 * action source order and increasing element order for runtime evaluation. */
bool minic_array_initializer_plan_resolve(
    const MinicArrayInitializerPlan *plan, MinicArrayInitializerResolved *result) {
    MinicArrayInitializerResolved temp = {0};
    size_t *successor = NULL;
    size_t extent, action_id, index;
    if (plan == NULL || result == NULL)
        return false;
    *result = temp;
    extent = minic_array_initializer_plan_element_count(plan);
    if (extent > SIZE_MAX / sizeof(size_t) ||
        plan->action_count > SIZE_MAX / sizeof(size_t) ||
        (plan->action_count != 0U && plan->actions == NULL))
        return false;
    temp.element_count = extent;
    temp.action_count = plan->action_count;
    if (extent != 0U) {
        temp.owners = (size_t *)malloc(extent * sizeof(size_t));
        temp.next_owned = (size_t *)malloc(extent * sizeof(size_t));
        successor = (size_t *)malloc(extent * sizeof(size_t));
        if (temp.owners == NULL || temp.next_owned == NULL || successor == NULL)
            goto fail;
        for (index = 0U; index < extent; ++index) {
            temp.owners[index] = MINIC_INITIALIZER_ACTION_INVALID;
            temp.next_owned[index] = MINIC_INITIALIZER_ACTION_INVALID;
            successor[index] = index;
        }
    }
    if (plan->action_count != 0U) {
        temp.final_counts = (size_t *)calloc(plan->action_count, sizeof(size_t));
        temp.first_owned = (size_t *)malloc(plan->action_count * sizeof(size_t));
        if (temp.final_counts == NULL || temp.first_owned == NULL)
            goto fail;
        for (action_id = 0U; action_id < plan->action_count; ++action_id)
            temp.first_owned[action_id] = MINIC_INITIALIZER_ACTION_INVALID;
    }
    for (action_id = plan->action_count; action_id != 0U; --action_id) {
        const MinicArrayInitializerAction *action = &plan->actions[action_id - 1U];
        size_t at;
        if (action->first_index > action->last_index ||
            action->last_index >= extent)
            goto fail;
        at = initializer_find_unclaimed(successor, extent, action->first_index);
        while (at < extent && at <= action->last_index) {
            temp.owners[at] = action_id - 1U;
            ++temp.final_counts[action_id - 1U];
            successor[at] = initializer_find_unclaimed(successor, extent, at + 1U);
            at = initializer_find_unclaimed(successor, extent, at);
        }
    }
    for (index = extent; index != 0U; --index) {
        const size_t element = index - 1U;
        const size_t owner = temp.owners[element];
        if (owner != MINIC_INITIALIZER_ACTION_INVALID) {
            temp.next_owned[element] = temp.first_owned[owner];
            temp.first_owned[owner] = element;
        }
    }
    free(successor);
    *result = temp;
    return true;
fail:
    free(successor);
    minic_array_initializer_resolved_destroy(&temp);
    return false;
}

void minic_array_initializer_resolved_destroy(MinicArrayInitializerResolved *resolved) {
    if (resolved == NULL)
        return;
    free(resolved->owners);
    free(resolved->final_counts);
    free(resolved->first_owned);
    free(resolved->next_owned);
    *resolved = (MinicArrayInitializerResolved){0};
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
