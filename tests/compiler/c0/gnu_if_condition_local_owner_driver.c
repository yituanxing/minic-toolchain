#include <stdio.h>

extern int diag_get(int n);
extern int diag_get_direct(int n);
extern int diag_get_nested(int n);
extern int diag_get_small(int n);
extern int future_local_runtime_gate;
extern int future_local_after_impure_guard(int n);

static int bad_calls;
void __compiletime_assert_645(void) { ++bad_calls; }

int main(void) {
    int a = diag_get(1);
    int b = diag_get_direct(1);
    int c = diag_get_nested(1);
    int d = diag_get_small(1);
    future_local_runtime_gate = 0;
    int future0 = future_local_after_impure_guard(-3);
    future_local_runtime_gate = 1;
    int future1 = future_local_after_impure_guard(4);
    printf("diag=%d direct=%d nested=%d small=%d future0=%d future1=%d bad_calls=%d\n",
           a, b, c, d, future0, future1, bad_calls);
    return a == 2 && b == 3 && c == 4 && d == 5 &&
           future0 == 3 && future1 == 19 && bad_calls == 0 ? 0 : 1;
}
