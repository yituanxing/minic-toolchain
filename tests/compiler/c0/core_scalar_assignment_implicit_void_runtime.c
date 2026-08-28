#include <stdio.h>

struct core_m4_list_head {
    struct core_m4_list_head *next;
    struct core_m4_list_head *prev;
};

void core_m4_init_list_head(struct core_m4_list_head *list);
void core_m4_pointer_store(const void **slot, const void *value);
void core_m4_empty_void(void);

int main(void) {
    struct core_m4_list_head head;
    const void *slot;
    int value;

    head.next = 0;
    head.prev = 0;
    slot = 0;
    value = 23;
    core_m4_init_list_head(&head);
    core_m4_pointer_store(&slot, &value);
    core_m4_empty_void();
    (void)printf("%d %d %d\n", head.next == &head, head.prev == &head, slot == &value);
    return 0;
}
