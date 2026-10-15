struct core_m4_list_head {
    struct core_m4_list_head *next;
    struct core_m4_list_head *prev;
};

#define CORE_M4_WRITE_ONCE(x, value)                                                               \
    do {                                                                                           \
        *(volatile __typeof__(x) *)&(x) = (value);                                                 \
    } while (0)

void core_m4_init_list_head(struct core_m4_list_head *list) {
    CORE_M4_WRITE_ONCE(list->next, list);
    CORE_M4_WRITE_ONCE(list->prev, list);
}

void core_m4_pointer_store(const void **slot, const void *value) {
    *slot = value;
}

void core_m4_empty_void(void) {}
