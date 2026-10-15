struct core_m17_node {
    struct core_m17_node *next;
    struct core_m17_node *prev;
};

int core_m17_scalar_value(int value) {
    return ({ value; });
}

struct core_m17_node *core_m17_read_once_shape(const struct core_m17_node *head) {
    return ({ (*(struct core_m17_node *const volatile *)&(head->next)); });
}

int core_m17_list_empty(const struct core_m17_node *head) {
    return ({ (*(struct core_m17_node *const volatile *)&(head->next)); }) == head;
}

int core_m17_prefix_store(int *value) {
    return ({
        *value = 17;
        *value;
    });
}

int core_m17_prefix_call_target(int value) {
    return value + 3;
}

int core_m17_prefix_call(int value) {
    return ({
        core_m17_prefix_call_target(value);
        value;
    });
}
