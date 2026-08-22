struct core_m16_node {
    struct core_m16_node *next;
    struct core_m16_node *prev;
};

int core_m16_list_is_first(const struct core_m16_node *list, const struct core_m16_node *head) {
    return list->prev == head;
}

int core_m16_list_is_last(const struct core_m16_node *list, const struct core_m16_node *head) {
    return list->next == head;
}

int core_m16_qualified_equal(struct core_m16_node *left, const struct core_m16_node *right) {
    return left == right;
}

int core_m16_qualified_not_equal(struct core_m16_node *left, const struct core_m16_node *right) {
    return left != right;
}

int core_m16_explicit_qualified_member_cast(const struct core_m16_node *list,
                                            const struct core_m16_node *head) {
    return (const struct core_m16_node *)list->prev == head;
}

int core_m16_null_equal(const struct core_m16_node *node) {
    return node == 0;
}

int core_m16_void_equal(struct core_m16_node *left, const void *right) {
    return left == right;
}
