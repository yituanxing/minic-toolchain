#include <stdio.h>

int core_m12_short_circuit_and(int left);
int core_m12_wrapped_pointer_and(int *left, int *middle, int *right);
static int rhs_calls;

int core_m12_rhs(void) {
    ++rhs_calls;
    return 1;
}

int main(void) {
    int a = 1;
    int b = 2;
    int first = core_m12_short_circuit_and(0);
    int calls_after_false = rhs_calls;
    int second = core_m12_short_circuit_and(1);
    int wrapped_true = core_m12_wrapped_pointer_and(&a, &a, &a);
    int wrapped_false = core_m12_wrapped_pointer_and(&a, &a, &b);
    printf("%d %d %d %d %d %d\n",
           first,
           calls_after_false,
           second,
           rhs_calls,
           wrapped_true,
           wrapped_false);
    return 0;
}
