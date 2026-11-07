/* Real GNU/MiniC runtime differential for Core's *& lvalue cancellation. */
struct Pair { int first; int second; };

static int direct_write(void) {
    int x = 1;
    *(&x) = 2;
    return x == 2;
}

static int nested_address(void) {
    int x = 3;
    int *p = &*(&x);
    *p = 7;
    return x == 7;
}

static int branch_after_write(void) {
    int x = 4;
    *(&x) = 5;
    if (x == 4) return 0;
    return x == 5;
}

static int member_write(void) {
    struct Pair p = { 1, 2 };
    *(&p.first) = 8;
    return p.first == 8 && p.second == 2;
}

static int array_write(void) {
    int values[2] = { 1, 2 };
    *(&values[0]) = 9;
    return values[0] == 9 && values[1] == 2;
}

static int volatile_write(void) {
    volatile int x = 1;
    *(&x) = 6;
    return x == 6;
}

int main(void) {
    return !(direct_write() && nested_address() && branch_after_write() &&
             member_write() && array_write() && volatile_write());
}
