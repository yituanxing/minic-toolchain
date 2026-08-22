#include <stdio.h>

struct core_m21_node {
    struct core_m21_node *next;
    struct core_m21_node **pprev;
};

int core_m21_pointer_if(int *pointer);
int core_m21_pointer_not(int *pointer);
void core_m21_hlist_del(struct core_m21_node *node);

int main(void) {
    int value = 1;
    struct core_m21_node *first;
    struct core_m21_node a;
    struct core_m21_node b;
    int first_updated;
    int back_link_updated;
    int tail_cleared;

    printf("if=%d,%d\n", core_m21_pointer_if(&value), core_m21_pointer_if(0));
    printf("not=%d,%d\n", core_m21_pointer_not(&value), core_m21_pointer_not(0));

    first = &a;
    a.next = &b;
    a.pprev = &first;
    b.next = 0;
    b.pprev = &a.next;
    core_m21_hlist_del(&a);
    first_updated = first == &b;
    back_link_updated = b.pprev == &first;

    first = &a;
    a.next = 0;
    a.pprev = &first;
    core_m21_hlist_del(&a);
    tail_cleared = first == 0;

    printf("hlist=%d,%d,%d\n", first_updated, back_link_updated, tail_cleared);
    return 0;
}
