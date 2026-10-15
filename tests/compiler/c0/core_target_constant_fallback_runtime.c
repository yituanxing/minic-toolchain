#include <stdio.h>

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

unsigned long core_m10_pointer_size(void);
void core_m10_init_list(struct list_head *list);

void core_m10_compiletime_error_next(void) {
    puts("BAD-next");
}

void core_m10_compiletime_error_prev(void) {
    puts("BAD-prev");
}

int main(void) {
    struct list_head list = {0, 0};
    core_m10_init_list(&list);
    printf("%lu %d %d\n", core_m10_pointer_size(), list.next == &list, list.prev == &list);
    return 0;
}
