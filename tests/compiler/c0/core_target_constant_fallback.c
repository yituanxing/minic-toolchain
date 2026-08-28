struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

unsigned long core_m10_pointer_size(void) {
    return sizeof(void *);
}

void core_m10_init_list(struct list_head *list) {
    do {
        do {
            __attribute__((noreturn, error("Unsupported access size for WRITE_ONCE"))) extern void
            core_m10_compiletime_error_next(void);
            if (!((sizeof(list->next) == sizeof(char) || sizeof(list->next) == sizeof(short) ||
                   sizeof(list->next) == sizeof(int) || sizeof(list->next) == sizeof(long)) ||
                  sizeof(list->next) == sizeof(long long)))
                core_m10_compiletime_error_next();
        } while (0);
        do {
            *(volatile __typeof__(list->next) *)&(list->next) = list;
        } while (0);
    } while (0);
    do {
        do {
            __attribute__((noreturn, error("Unsupported access size for WRITE_ONCE"))) extern void
            core_m10_compiletime_error_prev(void);
            if (!((sizeof(list->prev) == sizeof(char) || sizeof(list->prev) == sizeof(short) ||
                   sizeof(list->prev) == sizeof(int) || sizeof(list->prev) == sizeof(long)) ||
                  sizeof(list->prev) == sizeof(long long)))
                core_m10_compiletime_error_prev();
        } while (0);
        do {
            *(volatile __typeof__(list->prev) *)&(list->prev) = list;
        } while (0);
    } while (0);
}
