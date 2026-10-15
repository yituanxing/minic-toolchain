extern int core_m5b_global;

int core_m5b_equal(int expected) {
    return core_m5b_global == expected;
}

void core_m5b_set_if_equal(int value, int expected) {
    if (core_m5b_global == expected)
        core_m5b_global = value;
}

struct core_m11_node {
    struct core_m11_node *next;
    struct core_m11_node *prev;
};

int core_m11_pointer_equal(int *left, int *right) {
    return left == right;
}

int core_m11_member_pointer_equal(struct core_m11_node *node, struct core_m11_node *expected) {
    return node->next == expected;
}
