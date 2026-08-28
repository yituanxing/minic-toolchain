#include <stdio.h>

struct core_m20_node {
    struct core_m20_node *next;
};

unsigned long core_m20_for_assign(struct core_m20_node *head);
unsigned long core_m20_postincrement(unsigned long value);
unsigned long core_m20_list_count_nodes(struct core_m20_node *head);

int main(void) {
    struct core_m20_node empty;
    struct core_m20_node head;
    struct core_m20_node a;
    struct core_m20_node b;
    struct core_m20_node c;

    empty.next = &empty;
    head.next = &a;
    a.next = &b;
    b.next = &c;
    c.next = &head;

    printf("post=%lu,%lu\n", core_m20_postincrement(0), core_m20_postincrement(41));
    printf("assign=%lu,%lu\n", core_m20_for_assign(&empty), core_m20_for_assign(&head));
    printf("count=%lu,%lu\n", core_m20_list_count_nodes(&empty), core_m20_list_count_nodes(&head));
    return 0;
}
