struct core_m21_node {
    struct core_m21_node *next;
    struct core_m21_node **pprev;
};

int core_m21_pointer_if(int *pointer) {
    if (pointer)
        return 7;
    return 3;
}

int core_m21_pointer_not(int *pointer) {
    if (!pointer)
        return 11;
    return 5;
}

void core_m21_hlist_del(struct core_m21_node *node) {
    struct core_m21_node *next = node->next;
    struct core_m21_node **pprev = node->pprev;

    *pprev = next;
    if (next)
        next->pprev = pprev;
}
