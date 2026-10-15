#include <stdio.h>

int core_m9_short_circuit_or(int left);
static int rhs_calls;

int core_m9_rhs(void) {
    ++rhs_calls;
    return 0;
}

int main(void) {
    int first = core_m9_short_circuit_or(1);
    int calls_after_true = rhs_calls;
    int second = core_m9_short_circuit_or(0);
    printf("%d %d %d %d\n", first, calls_after_true, second, rhs_calls);
    return 0;
}
