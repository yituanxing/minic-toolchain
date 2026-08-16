#include <stdio.h>

int core_diff_math(int x, int y);
int core_diff_branch(int x);
int core_diff_zero(int x);
int core_diff_ninth(int a0,
                    int a1,
                    int a2,
                    int a3,
                    int a4,
                    int a5,
                    int a6,
                    int a7,
                    int a8);
int core_diff_pointer_zero(int *value);

int main(void) {
    static const int values[] = {-7, -1, 0, 1, 9};
    size_t left;
    size_t right;
    int pointed;

    for (left = 0U; left < sizeof(values) / sizeof(values[0]); ++left) {
        for (right = 0U; right < sizeof(values) / sizeof(values[0]); ++right) {
            (void)printf("math %d %d %d\n",
                         values[left],
                         values[right],
                         core_diff_math(values[left], values[right]));
        }
    }
    for (left = 0U; left < sizeof(values) / sizeof(values[0]); ++left) {
        (void)printf("branch %d %d\n", values[left], core_diff_branch(values[left]));
        (void)printf("zero %d %d\n", values[left], core_diff_zero(values[left]));
    }
    (void)printf("ninth %d\n", core_diff_ninth(1, 2, 3, 4, 5, 6, 7, 8, 91));
    pointed = -1234;
    (void)printf("pointer-nonnull %d\n", core_diff_pointer_zero(&pointed));
    (void)printf("pointer-null %d\n", core_diff_pointer_zero((int *)0));
    return 0;
}
