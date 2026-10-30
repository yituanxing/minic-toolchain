#include <stdio.h>

extern unsigned min_guard(unsigned);
extern unsigned long clamp_guard(unsigned long);
extern int known_local_guard(void);
extern int runtime_ternary_builtin_guard(int);
extern int runtime_and_builtin_guard(int);
extern int side_effect_guard(int *);
extern int volatile_guard(volatile int *);
extern int runtime_guard(int);
extern int side_effect_runtime_guard(int *, int);

static int runtime_calls;
void ordinary_runtime_call(void) { ++runtime_calls; }

int main(void) {
    int side_value = 9;
    volatile int volatile_value = 13;
    unsigned min_small = min_guard(7);
    unsigned min_large = min_guard(50000);
    unsigned long clamp_small = clamp_guard(9);
    unsigned long clamp_large = clamp_guard(50000);
    int known = known_local_guard();
    int side_result = side_effect_guard(&side_value);
    int volatile_result = volatile_guard(&volatile_value);
    int runtime_small = runtime_guard(7);
    int runtime_hit = runtime_guard(73);
    int side_runtime = 4;
    int side_runtime_false = side_effect_runtime_guard(&side_runtime, 0);
    int side_runtime_true = side_effect_runtime_guard(&side_runtime, 1);
    int ternary_false = runtime_ternary_builtin_guard(0);
    int ternary_true = runtime_ternary_builtin_guard(1);
    int and_false = runtime_and_builtin_guard(0);
    int and_true = runtime_and_builtin_guard(1);

    printf("min=%u,%u clamp=%lu,%lu known=%d side=%d,%d volatile=%d "
           "runtime=%d,%d calls=%d ternary=%d,%d and=%d,%d "
           "side-runtime=%d,%d,%d\n",
           min_small, min_large, clamp_small, clamp_large, known,
           side_result, side_value, volatile_result,
           runtime_small, runtime_hit, runtime_calls,
           ternary_false, ternary_true, and_false, and_true,
           side_runtime_false, side_runtime_true, side_runtime);
    return (min_small != 7 || min_large != 16384 ||
            clamp_small != 9 || clamp_large != 370 || known != 1 ||
            side_result != 9 || side_value != 9 || volatile_result != 13 ||
            runtime_small != 8 || runtime_hit != 17 || runtime_calls != 1 ||
            ternary_false != 12 || ternary_true != 12 ||
            and_false != 11 || and_true != 11 ||
            side_runtime_false != 9 || side_runtime_true != 5 || side_runtime != 5);
}
