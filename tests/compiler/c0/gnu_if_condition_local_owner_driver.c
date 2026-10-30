#include <stdio.h>

extern int diag_get(int n);
extern int diag_get_direct(int n);
extern int diag_get_nested(int n);
extern int diag_get_small(int n);

static int bad_calls;
void __compiletime_assert_645(void) { ++bad_calls; }

int main(void) {
    int a = diag_get(1);
    int b = diag_get_direct(1);
    int c = diag_get_nested(1);
    int d = diag_get_small(1);
    printf("diag=%d direct=%d nested=%d small=%d bad_calls=%d\n",
           a, b, c, d, bad_calls);
    return 0;
}
