#include <stdio.h>

int core_hybrid_core(int value);
int core_hybrid_fallback_load(int *value);

int main(void) {
    int value;

    value = 37;
    (void)printf("%d %d\n", core_hybrid_core(5), core_hybrid_fallback_load(&value));
    return 0;
}
