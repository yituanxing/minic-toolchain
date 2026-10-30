/* GNU/MiniC strict differential: an impure condition may leave unrelated
 * automatic locals unchanged. The Core IF guard must retain those incoming
 * facts without retaining locals modified by aliases, assignments or loops. */
#include <stdio.h>

static volatile int global_volatile_flag;
static int call_count;

__attribute__((noinline)) static int touch_external(int x) {
    ++call_count;
    return x;
}

static int nested_pure_ternary(int flag) {
    int lower = 1;
    if (flag ? !__builtin_constant_p((long long)lower >= 0) : 0)
        return -11;
    return 11;
}

static int nested_volatile_ternary(int flag) {
    int lower = 1;
    global_volatile_flag = flag;
    if (global_volatile_flag
        ? !__builtin_constant_p((long long)lower >= 0) : 0)
        return -12;
    return 12;
}

static int nested_call_ternary(int flag) {
    int lower = 1;
    if (touch_external(flag)
        ? !__builtin_constant_p((long long)lower >= 0) : 0)
        return -13;
    return 13;
}

static int conditional_assign_phi(int flag) {
    int x = 1;
    if (flag)
        x = 2;
    return flag ? x == 2 : x == 1;
}

static int branch_phi(int left, int right) {
    int x = 1;
    if (left)
        x = 2;
    if (right)
        x = 3;
    return left ? (right ? x == 3 : x == 2)
                : (right ? x == 3 : x == 1);
}

static int loop_phi(int n) {
    int x = 1;
    for (int i = 0; i < n; ++i)
        x += i;
    return n ? x >= 1 : x == 1;
}

static int short_circuit_mutation(int flag) {
    int changed = 0;
    int x = 5;
    if (flag && (++changed, x == 5))
        x = 7;
    return x * 10 + changed;
}

static int alias_local_mutation(int flag) {
    int x = 1;
    int *p = &x;
    if (flag)
        *p = 2;
    if (flag ? x == 2 : x == 1)
        return 1;
    return -1;
}

static int label_reentry(int flag) {
    int x = 1;
    if (flag)
        goto label;
    x = 2;
label:
    return flag ? x == 1 : x == 2;
}

static int builtin_unevaluated_side_effect(int flag) {
    int x = 1;
    if (flag ? __builtin_constant_p((++x, x)) : 0)
        return -1;
    return x;
}

static int ternary_increment(int flag) {
    int x = 1;
    if (flag ? ++x == 2 : x == 1)
        return x;
    return -1;
}

__attribute__((noinline)) static void touch_local_pointer(int *value) {
    *value = 4;
}

static int statement_local_escape(int flag) {
    int result = flag ? ({
        int temporary = 1;
        touch_local_pointer(&temporary);
        temporary;
    }) : 3;
    return flag ? result == 4 : result == 3;
}

static int statement_local_join(int flag) {
    int result = ({
        int temporary = 1;
        if (flag)
            temporary = 2;
        temporary;
    });
    return flag ? result == 2 : result == 1;
}

static int statement_local_shadow(int flag) {
    int value = 10;
    if (flag ? ({ int value = 1; value == 1; }) : value == 10)
        return value;
    return -1;
}

static int statement_nested_block_owner(int flag) {
    int out = 0;
    if (flag ? ({
        int value = 0;
        if (flag) {
            int nested = 2;
            value = nested;
        }
        value == 2;
    }) : 0)
        out = 1;
    return flag ? out : !out;
}

int main(void) {
    int pure_zero = nested_pure_ternary(0);
    int pure_one = nested_pure_ternary(1);
    int volatile_zero = nested_volatile_ternary(0);
    int volatile_one = nested_volatile_ternary(1);
    int call_zero = nested_call_ternary(0);
    int call_one = nested_call_ternary(1);
    int observed_calls = call_count;
    printf("pure=%d,%d volatile=%d,%d call=%d,%d call_count=%d\n",
        pure_zero, pure_one, volatile_zero, volatile_one,
        call_zero, call_one, observed_calls);
    printf("phi=%d,%d multi=%d,%d,%d,%d loop=%d,%d short=%d,%d alias=%d,%d label=%d,%d\n",
        conditional_assign_phi(0), conditional_assign_phi(1),
        branch_phi(0,0), branch_phi(0,1), branch_phi(1,0), branch_phi(1,1),
        loop_phi(0), loop_phi(3),
        short_circuit_mutation(0), short_circuit_mutation(1),
        alias_local_mutation(0), alias_local_mutation(1),
        label_reentry(0), label_reentry(1));
    printf("unevaluated=%d,%d increment=%d,%d\n",
        builtin_unevaluated_side_effect(0), builtin_unevaluated_side_effect(1),
        ternary_increment(0), ternary_increment(1));
    printf("stmt_escape=%d,%d stmt_join=%d,%d shadow=%d,%d nested=%d,%d\n",
        statement_local_escape(0), statement_local_escape(1),
        statement_local_join(0), statement_local_join(1),
        statement_local_shadow(0), statement_local_shadow(1),
        statement_nested_block_owner(0), statement_nested_block_owner(1));
    return 0;
}
