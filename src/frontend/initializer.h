#ifndef MINIC_FRONTEND_INITIALIZER_H
#define MINIC_FRONTEND_INITIALIZER_H

#include <stdbool.h>
#include <stddef.h>

#define MINIC_INITIALIZER_ACTION_INVALID ((size_t) - 1)

typedef struct MinicArrayInitializerAction {
    size_t first_index;
    size_t last_index;
} MinicArrayInitializerAction;

typedef struct MinicArrayInitializerPlan {
    MinicArrayInitializerAction *actions;
    size_t action_count;
    size_t action_capacity;
    size_t declared_count;
    size_t inferred_count;
    size_t next_index;
    bool infer_bound;
} MinicArrayInitializerPlan;

void minic_array_initializer_plan_initialize(MinicArrayInitializerPlan *plan,
                                             size_t declared_count,
                                             bool infer_bound);
void minic_array_initializer_plan_destroy(MinicArrayInitializerPlan *plan);

bool minic_array_initializer_plan_add_positional(MinicArrayInitializerPlan *plan,
                                                 size_t *action_id);
bool minic_array_initializer_plan_add_designated(MinicArrayInitializerPlan *plan,
                                                 size_t first_index,
                                                 size_t last_index,
                                                 size_t *action_id);

size_t minic_array_initializer_plan_element_count(const MinicArrayInitializerPlan *plan);
bool minic_array_initializer_plan_final_owner(const MinicArrayInitializerPlan *plan,
                                              size_t element_index,
                                              size_t *action_id);
bool minic_array_initializer_plan_action_owns(const MinicArrayInitializerPlan *plan,
                                              size_t action_id,
                                              size_t element_index);
size_t minic_array_initializer_plan_action_final_count(const MinicArrayInitializerPlan *plan,
                                                       size_t action_id);

#endif
