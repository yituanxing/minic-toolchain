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
    plan->final_owner_by_index = NULL;
    plan->final_next_by_index = NULL;
    plan->final_first_by_action = NULL;
    plan->final_count_by_action = NULL;
    plan->finalized = false;
}

void minic_array_initializer_plan_destroy(MinicArrayInitializerPlan *plan) {
    if (plan == NULL) {
        return;
    }
    free(plan->actions);
    free(plan->final_owner_by_index);
    free(plan->final_next_by_index);
    free(plan->final_first_by_action);
    free(plan->final_count_by_action);
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
    /* Additional initializer actions invalidate cached last-writer ownership. */
    free(plan->final_owner_by_index);
    free(plan->final_next_by_index);
    free(plan->final_first_by_action);
    free(plan->final_count_by_action);
    plan->final_owner_by_index = NULL;
    plan->final_next_by_index = NULL;
    plan->final_first_by_action = NULL;
    plan->final_count_by_action = NULL;
    plan->finalized = false;
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


/* Disjoint-set successor skip: find the first array index not assigned by
 * a later source initializer. Sentinel index == extent is never writable. */
static size_t initializer_unclaimed_next(size_t *parents, size_t index) {
    size_t root = index;
    while (parents[root] != root)
        root = parents[root];
    while (parents[index] != index) {
        size_t next = parents[index];
        parents[index] = root;
        index = next;
    }
    return root;
}

bool minic_array_initializer_plan_finalize(MinicArrayInitializerPlan *plan) {
    size_t extent, action_count, index, action_id;
    size_t *owners = NULL, *next_owned = NULL, *first_owned = NULL;
    size_t *owned_counts = NULL, *successor = NULL;
    if (plan == NULL) return false;
    if (plan->finalized) return true;
    extent = minic_array_initializer_plan_element_count(plan);
    action_count = plan->action_count;
    if (extent == SIZE_MAX || extent > (SIZE_MAX / sizeof(size_t)) - 1U ||
        action_count > SIZE_MAX / sizeof(size_t))
        return false;
    successor = (size_t *)malloc((extent + 1U) * sizeof(*successor));
    if (successor == NULL) goto failed;
    if (extent != 0U) {
        owners = (size_t *)malloc(extent * sizeof(*owners));
        next_owned = (size_t *)malloc(extent * sizeof(*next_owned));
        if (owners == NULL || next_owned == NULL) goto failed;
    }
    if (action_count != 0U) {
        first_owned = (size_t *)malloc(action_count * sizeof(*first_owned));
        owned_counts = (size_t *)calloc(action_count, sizeof(*owned_counts));
        if (first_owned == NULL || owned_counts == NULL) goto failed;
    }
    for (index = 0U; index <= extent; ++index)
        successor[index] = index;
    for (index = 0U; index < extent; ++index) {
        owners[index] = MINIC_INITIALIZER_ACTION_INVALID;
        next_owned[index] = MINIC_INITIALIZER_ACTION_INVALID;
    }
    for (action_id = 0U; action_id < action_count; ++action_id)
        first_owned[action_id] = MINIC_INITIALIZER_ACTION_INVALID;
    /* Reverse action processing ensures last designation wins; a claimed
     * slot is removed from the successor set and never visited again. */
    for (action_id = action_count; action_id > 0U; --action_id) {
        const MinicArrayInitializerAction *action = &plan->actions[action_id - 1U];
        size_t current;
        if (action->first_index > action->last_index ||
            action->last_index >= extent)
            goto failed;
        current = initializer_unclaimed_next(successor, action->first_index);
        while (current <= action->last_index) {
            owners[current] = action_id - 1U;
            successor[current] = initializer_unclaimed_next(successor, current + 1U);
            current = initializer_unclaimed_next(successor, current);
        }
    }
    /* Link the final elements for each original source action in ascending
     * destination index order, with no runtime O(actions * extent) scan. */
    for (index = extent; index > 0U; --index) {
        size_t owner = owners[index - 1U];
        if (owner != MINIC_INITIALIZER_ACTION_INVALID) {
            next_owned[index - 1U] = first_owned[owner];
            first_owned[owner] = index - 1U;
            owned_counts[owner] += 1U;
        }
    }
    free(successor);
    plan->final_owner_by_index = owners;
    plan->final_next_by_index = next_owned;
    plan->final_first_by_action = first_owned;
    plan->final_count_by_action = owned_counts;
    plan->finalized = true;
    return true;
failed:
    free(successor);
    free(owners);
    free(next_owned);
    free(first_owned);
    free(owned_counts);
    return false;
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
    if (plan->finalized) {
        *action_id = plan->final_owner_by_index[element_index];
        return true;
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
    if (plan->finalized)
        return plan->final_count_by_action[action_id];
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
