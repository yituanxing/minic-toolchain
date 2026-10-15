struct core_m15_node {
    struct core_m15_node *next;
    struct core_m15_node *prev;
};

void *core_m15_void_add(void *base, long index) {
    return base + index;
}

int *core_m15_int_add(int *base, long index) {
    return base + index;
}

int *core_m15_int_add_commuted(long index, int *base) {
    return index + base;
}

void core_m15_list_poison(struct core_m15_node *entry) {
    entry->next = (void *)0x100 + 0UL;
    entry->prev = (void *)0x122 + 0UL;
}
