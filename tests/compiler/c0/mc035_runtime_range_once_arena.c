/* MC-035: the scalar range action is materialized through an extra Local and
 * LVALUE_READ. The Program.expression arena can grow between those two nodes. */
#ifndef MC035_PREFIX_PHASE
#define MC035_PREFIX_PHASE 2
#endif

int testcase(int seed) {
#if MC035_PREFIX_PHASE >= 1
    seed + 0;
#endif
#if MC035_PREFIX_PHASE >= 2
    seed + 1;
#endif
#if MC035_PREFIX_PHASE >= 3
    seed + 2;
#endif
#if MC035_PREFIX_PHASE >= 4
    seed + 3;
#endif
#if MC035_PREFIX_PHASE >= 5
    seed + 4;
#endif
    int target[16] = {0, [1 ... 15] = seed + 1};
    return target[0] + target[1] + target[15];
}

static int once_count;
static int once(int seed) {
    ++once_count;
    return seed + 5;
}

int main(void) {
    int range[4] = {[0 ... 3] = once(7)};
    if (testcase(3) != 8)
        return 1;
    if (once_count != 1 || range[0] != 12 || range[1] != 12 ||
        range[2] != 12 || range[3] != 12)
        return 2;
    return 0;
}
