#include <stdio.h>

extern int runtime_guard(int);
extern int effectful_condition(int);
extern int entry_goto_dead_source_arm(int);

static int side_calls;
static int condition_calls;
static int condition_result;

void visible_side_effect(int value) { side_calls += value; }
int observable_condition(void) { ++condition_calls; return condition_result; }

int main(void) {
    int result = 0;
    result += runtime_guard(7);
    result += runtime_guard(-3);
    condition_result = 1;
    result += effectful_condition(5);
    condition_result = 0;
    result += effectful_condition(100);
    result += entry_goto_dead_source_arm(42);
    result += entry_goto_dead_source_arm(2);
    printf("%d %d %d\n", result, side_calls, condition_calls);
    return (result == 153 && side_calls == 54 && condition_calls == 2) ? 0 : 1;
}
