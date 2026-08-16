#include <stdio.h>

struct CoreHybridLayout {
    char prefix;
    int value;
};

int core_hybrid_core(int value);
int core_hybrid_call(int value);
int *core_hybrid_field(struct CoreHybridLayout *item);
int core_hybrid_fallback_load(int *value);

int main(void) {
    struct CoreHybridLayout layout;
    int value;

    layout.prefix = 7;
    layout.value = 53;
    value = 37;
    (void)printf("%d %d %d %d\n",
                 core_hybrid_core(5),
                 core_hybrid_call(10),
                 *core_hybrid_field(&layout),
                 core_hybrid_fallback_load(&value));
    return 0;
}
