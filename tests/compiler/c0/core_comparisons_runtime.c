#include <stdio.h>
struct node { struct node *next, *prev; };
int comparison_global = 13;
int global_equal(int), integer_ne(int, int);
void set_if_equal(int, int);
int pointer_eq(int *, int *), pointer_ne(int *, int *);
int member_eq(struct node *, struct node *), member_ne(struct node *, struct node *);
int list_condition(struct node *, struct node *, struct node *);
int list_first(const struct node *, const struct node *);
int list_last(const struct node *, const struct node *);
int qualified_eq(struct node *, const struct node *);
int qualified_ne(struct node *, const struct node *);
int explicit_cast_eq(const struct node *, const struct node *);
int null_eq(const struct node *);
int void_eq(struct node *, const void *);

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "comparison failed: %s\n", #expr); return 1; } } while (0)
int main(void) {
    int a = 1, b = 2;
    struct node first, head, added;
    first.next = first.prev = &head;
    head.next = head.prev = &first;
    added.next = added.prev = &added;

    CHECK(global_equal(13) && !global_equal(12));
    set_if_equal(21, 12);
    CHECK(comparison_global == 13);
    set_if_equal(29, 13);
    CHECK(comparison_global == 29);
    CHECK(!integer_ne(3, 3) && integer_ne(3, 4));
    CHECK(pointer_eq(&a, &a) && !pointer_eq(&a, &b));
    CHECK(!pointer_ne(&a, &a) && pointer_ne(&a, &b));
    CHECK(member_eq(&first, &head) && !member_eq(&first, &first));
    CHECK(!member_ne(&first, &head) && member_ne(&first, &first));
    CHECK(list_condition(&added, &first, &head));
    CHECK(!list_condition(&first, &first, &head));
    CHECK(list_first(&first, &head) && !list_first(&first, &first));
    CHECK(list_last(&first, &head));
    CHECK(qualified_eq(&first, &first) && !qualified_eq(&first, &head));
    CHECK(qualified_ne(&first, &head) && !qualified_ne(&first, &first));
    CHECK(explicit_cast_eq(&first, &head));
    CHECK(!null_eq(&first) && null_eq(0));
    CHECK(void_eq(&first, &first) && !void_eq(&first, &head));
    puts("CORE_COMPARISONS=PASS");
    return 0;
}
