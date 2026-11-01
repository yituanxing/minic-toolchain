/* MC-057 RV32 psABI: a GNU empty struct return has zero physical words.
 * Build both the zero-sized GNU extension and a one-byte Record sibling. */
#ifdef MC057_NONEMPTY_CONTROL
struct MC057_Empty { unsigned char marker; };
#else
struct MC057_Empty {};
#endif

struct MC057_Empty mc057_direct(void) {
    struct MC057_Empty object = {};
    return object;
}

struct MC057_Empty mc057_branches(int select) {
    if (select == 0) return mc057_direct();
    if (select > 0) {
        struct MC057_Empty object = {};
        return object;
    }
    return (struct MC057_Empty){};
}

struct MC057_Empty mc057_forward(struct MC057_Empty value, int select) {
    if (select != 0) return value;
    return mc057_branches(select);
}

struct MC057_Empty mc057_indirect(int select) {
    struct MC057_Empty (*factory)(int) = mc057_branches;
    return factory(select);
}

int mc057_sum(int first, struct MC057_Empty ignored_first,
              int second, struct MC057_Empty ignored_second, int third) {
    (void)ignored_first;
    (void)ignored_second;
    return first + second + third;
}

int main(void) {
    struct MC057_Empty direct = mc057_direct();
    struct MC057_Empty branched = mc057_branches(1);
    struct MC057_Empty passed = mc057_forward(direct, 1);
    struct MC057_Empty indirect = mc057_indirect(-1);
#ifdef MC057_NONEMPTY_CONTROL
    if (direct.marker != 0 || branched.marker != 0 ||
        passed.marker != 0 || indirect.marker != 0) return 1;
#endif
    if (mc057_sum(7, direct, 11, passed, 24) != 42) return 2;
    if (mc057_sum(17, branched, 9, indirect, 16) != 42) return 3;
    mc057_branches(0);
    mc057_branches(-1);
    return 42;
}
