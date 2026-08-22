#include <stdint.h>
#include <stdio.h>

struct core_m15_node {
    struct core_m15_node *next;
    struct core_m15_node *prev;
};

void *core_m15_void_add(void *base, long index);
int *core_m15_int_add(int *base, long index);
int *core_m15_int_add_commuted(long index, int *base);
void core_m15_list_poison(struct core_m15_node *entry);

int main(void) {
    int values[8] = {0};
    struct core_m15_node node = {0};
    char *byte_base = (char *)&values[2];

    core_m15_list_poison(&node);
    printf("%td %td %td %lu %lu\n",
           (char *)core_m15_void_add(byte_base, 3) - byte_base,
           core_m15_int_add(&values[4], -2) - &values[4],
           core_m15_int_add_commuted(3, &values[1]) - &values[1],
           (unsigned long)(uintptr_t)node.next,
           (unsigned long)(uintptr_t)node.prev);
    return 0;
}
