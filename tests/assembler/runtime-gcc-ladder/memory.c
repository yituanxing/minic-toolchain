/* Stage 3: heap allocation, libc memcpy/qsort/memcmp and indexed loops. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_ints(const void *lhs, const void *rhs) {
    int a = *(const int *)lhs;
    int b = *(const int *)rhs;
    return (a > b) - (a < b);
}

int main(void) {
    const int input[] = {9, 1, 8, 2, 7, 3, 6, 4,
                         5, 0, 19, 11, 18, 12, 17, 13};
    const size_t count = sizeof(input) / sizeof(input[0]);
    int *values = (int *)malloc(sizeof(input));
    if (values == NULL)
        return 21;
    memcpy(values, input, sizeof(input));
    qsort(values, count, sizeof(values[0]), compare_ints);
    int sum = 0;
    for (size_t i = 0; i < count; ++i)
        sum += values[i];
    int copy[sizeof(input) / sizeof(input[0])];
    memcpy(copy, values, sizeof(input));
    int ok = sum == 135 && copy[0] == 0 && copy[count - 1] == 19 &&
             memcmp(copy, values, sizeof(input)) == 0;
    printf("LADDER_MEMORY sum=%d status=%s\n", sum,
           ok ? "PASS" : "FAIL");
    free(values);
    return ok ? 0 : 22;
}
