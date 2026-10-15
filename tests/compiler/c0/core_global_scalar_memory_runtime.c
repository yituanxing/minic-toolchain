#include <stdio.h>

int core_m5_global = 11;

int core_m5_global_load(void);
void core_m5_global_store(int value);

int main(void) {
    int before;
    int after;

    before = core_m5_global_load();
    core_m5_global_store(29);
    after = core_m5_global_load();
    (void)printf("%d %d\n", before, after);
    return 0;
}
