typedef struct {
    unsigned value;
} one_t;
typedef struct {
    unsigned lo;
    unsigned hi;
} two_t;
static one_t make_one(unsigned value) {
    one_t out = {value};
    return out;
}
one_t choose_one(int condition, const one_t *stored) {
    return condition ? *stored : make_one(7U);
}
two_t assign_and_return(two_t *dst, two_t *src) {
    return *dst = *src;
}
int main(void) {
    one_t one = {3U};
    two_t a = {1U, 2U};
    two_t b = {4U, 5U};
    one_t r = choose_one(1, &one);
    two_t p = assign_and_return(&a, &b);
    return (int)(r.value + p.lo + p.hi - 12U);
}
