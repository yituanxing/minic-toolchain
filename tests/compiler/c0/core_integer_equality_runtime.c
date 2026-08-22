#include <stdio.h>

int core_m5b_global = 13;

int core_m5b_equal(int expected);
void core_m5b_set_if_equal(int value, int expected);

int main(void) {
    int before_equal;
    int before_unequal;

    before_equal = core_m5b_equal(13);
    before_unequal = core_m5b_equal(12);
    core_m5b_set_if_equal(21, 12);
    core_m5b_set_if_equal(29, 13);
    (void)printf("%d %d %d\n", before_equal, before_unequal, core_m5b_global);
    return 0;
}
