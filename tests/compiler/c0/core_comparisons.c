/* Scalar and pointer comparisons share one semantic fixture, including
 * qualified members, null/void pointer conversions and short-circuit CFG. */
struct node { struct node *next, *prev; };
extern int comparison_global;
int global_equal(int expected) { return comparison_global == expected; }
void set_if_equal(int value, int expected) {
    if (comparison_global == expected) comparison_global = value;
}
int integer_ne(int a, int b) { return a != b; }
int pointer_eq(int *a, int *b) { return a == b; }
int pointer_ne(int *a, int *b) { return a != b; }
int member_eq(struct node *a, struct node *b) { return a->next == b; }
int member_ne(struct node *a, struct node *b) { return a->next != b; }
int list_condition(struct node *node, struct node *prev, struct node *next) {
    if (__builtin_expect(!!(next->prev == prev && prev->next == next &&
                            node != prev && node != next), 1))
        return 1;
    return 0;
}
int list_first(const struct node *a, const struct node *b) { return a->prev == b; }
int list_last(const struct node *a, const struct node *b) { return a->next == b; }
int qualified_eq(struct node *a, const struct node *b) { return a == b; }
int qualified_ne(struct node *a, const struct node *b) { return a != b; }
int explicit_cast_eq(const struct node *a, const struct node *b) {
    return (const struct node *)a->prev == b;
}
int null_eq(const struct node *a) { return a == 0; }
int void_eq(struct node *a, const void *b) { return a == b; }
