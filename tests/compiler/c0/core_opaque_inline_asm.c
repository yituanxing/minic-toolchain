struct core_m18_node {
    struct core_m18_node *next;
    struct core_m18_node *prev;
};

void core_m18_fence_only(void) {
    __asm__ __volatile__("fence rw,w" : : : "memory");
}

void core_m18_release_shape(struct core_m18_node *entry) {
    (*(struct core_m18_node *volatile *)&(entry->prev)) = entry;
    __asm__ __volatile__("fence rw,w" : : : "memory");
    (*(struct core_m18_node *volatile *)&(entry->next)) = entry;
}

void core_m18_two_fences(void) {
    __asm__ __volatile__("fence rw,w" : : : "memory");
    __asm__ __volatile__("fence r,rw" : : : "memory");
}
