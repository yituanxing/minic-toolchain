/* Stage 2: recursive calls, branches, switch dispatch and observable stdio. */
#include <stdio.h>

__attribute__((noinline)) static int ladder_fib(int n) {
    if (n < 2)
        return n;
    return ladder_fib(n - 1) + ladder_fib(n - 2);
}

__attribute__((noinline)) static int ladder_classify(int n) {
    switch (n) {
    case 3: return 30;
    case 8: return 80;
    case 11: return 110;
    default: return -1;
    }
}

int main(void) {
    int value = ladder_fib(10) + ladder_classify(3) +
                ladder_classify(8);
    int ok = value == 165 && ladder_classify(99) == -1;
    printf("LADDER_CONTROL value=%d status=%s\n", value,
           ok ? "PASS" : "FAIL");
    return ok ? 0 : 18;
}
