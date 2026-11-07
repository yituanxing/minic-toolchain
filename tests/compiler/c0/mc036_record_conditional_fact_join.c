/* MC-036: both aggregate conditional arms must start from the same facts.
 * The false arm's x=2 must not change the facts observed after the true arm.
 * GNU C statement expressions intentionally retain source-side effects. */
struct Mc036Record { int value; };

static int mc036_simple(int c) {
    int x = 0;
    struct Mc036Record a = { 1 }, b = { 2 };
    struct Mc036Record r = c ? ({ x = 1; a; }) : ({ x = 2; b; });
    if (x == 2) return r.value + 10;
    return r.value;
}

static int mc036_condition_effect(int c) {
    int x = 0;
    struct Mc036Record a = { 4 }, b = { 5 };
    struct Mc036Record r = (x = c + 3, c)
        ? ({ x = 6; a; }) : ({ x = 8; b; });
    return r.value + x;
}

static int mc036_nested(int c, int d) {
    int x = 0;
    struct Mc036Record a = { 1 }, b = { 2 }, z = { 3 };
    struct Mc036Record r = c
        ? ({ x = d ? 4 : 5; a; })
        : ({ x = 9; d ? b : z; });
    return r.value + x;
}

static int mc036_loop(int c) {
    int sum = 0;
    int i;
    struct Mc036Record a = { 1 }, b = { 2 };
    for (i = 0; i < 4; ++i) {
        int x = 0;
        struct Mc036Record r = (i & 1) ? ({ x = 1; a; }) : ({ x = 2; b; });
        if (i == 2) continue;
        sum += r.value + x;
        if (c && i == 3) break;
    }
    return sum;
}

int main(void) {
    if (mc036_simple(1) != 1 || mc036_simple(0) != 12) return 1;
    if (mc036_condition_effect(1) != 10 ||
        mc036_condition_effect(0) != 13) return 2;
    if (mc036_nested(1, 1) != 5 || mc036_nested(1, 0) != 6 ||
        mc036_nested(0, 1) != 11 || mc036_nested(0, 0) != 12) return 3;
    if (mc036_loop(0) != 8 || mc036_loop(1) != 8) return 4;
    return 0;
}
