struct core_m13_node {
    struct core_m13_node *next;
    struct core_m13_node *prev;
};

int core_m13_integer_not_equal(int left, int right) {
    return left != right;
}

int core_m13_pointer_not_equal(int *left, int *right) {
    return left != right;
}

int core_m13_member_pointer_not_equal(struct core_m13_node *node, struct core_m13_node *other) {
    return node->next != other;
}

int core_m13_list_condition(struct core_m13_node *new_node,
                            struct core_m13_node *prev,
                            struct core_m13_node *next) {
    if (__builtin_expect(
            !!(next->prev == prev && prev->next == next && new_node != prev && new_node != next),
            1))
        return 1;
    return 0;
}
