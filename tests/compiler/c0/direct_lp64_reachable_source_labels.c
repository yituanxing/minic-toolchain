/* A machine block can be reached without an ordinary CFG predecessor:
 * - a static &&label relocation, - a dynamic block address, and
 * - a forward goto into an otherwise dead lexical switch case.
 * All labels must remain roots when direct LP64 emitters skip dead blocks. */
static int static_dispatch(int n) {
    static void *const labels[] = {&&first, &&second, &&third};
    if (n < 0 || n > 2) return 0;
    goto *labels[n];
first:
    return 10;
second:
    return 20;
third:
    return 42;
}

static int dynamic_dispatch(int n) {
    void *address = &&miss;
    if (n != 0) address = &&hit;
    goto *address;
hit:
    return 42;
miss:
    return 7;
}

static int external_switch_reentry(int enter) {
    int result = 0;
    if (enter) goto FORWARD;
    switch (sizeof(void *)) {
    case 1: FORWARD: result = 43; break;
    case 8: result = 42; break;
    default: result = 99; break;
    }
    return result;
}

int main(void) {
    return static_dispatch(0) == 10 && static_dispatch(1) == 20 &&
           static_dispatch(2) == 42 && static_dispatch(3) == 0 &&
           dynamic_dispatch(0) == 7 && dynamic_dispatch(1) == 42 &&
           external_switch_reentry(0) == 42 &&
           external_switch_reentry(1) == 43 ? 42 : 1;
}
