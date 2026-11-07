/* Runtime path facts must not leak across a ?:, && or || expression merge.
 * Keep each caller argument runtime-dependent instead of a folded literal. */
static int ternary_value(int condition) {
    int x = 0;
    condition ? (x = 1) : 0;
    return x;
}
static int ternary_guard(int condition) {
    int x = 0;
    condition ? (x = 1) : 0;
    if (x) return 1;
    return 0;
}
static int logical_and_value(int condition) {
    int x = 0;
    condition && (x = 1);
    return x;
}
static int logical_and_guard(int condition) {
    int x = 0;
    condition && (x = 1);
    if (x) return 1;
    return 0;
}
static int logical_or_value(int condition) {
    int x = 0;
    condition || (x = 1);
    return x;
}
static int logical_or_guard(int condition) {
    int x = 0;
    condition || (x = 1);
    if (x) return 1;
    return 0;
}
static int ternary_equal_value(int condition) {
    int x = 0;
    condition ? (x = 1) : (x = 1);
    return x;
}
static int ternary_equal_guard(int condition) {
    int x = 0;
    condition ? (x = 1) : (x = 1);
    if (x) return 1;
    return 0;
}
static int gnu_omitted_guard(int condition) {
    int x = 0;
    condition ?: (x = 1);
    if (x) return 1;
    return 0;
}
static int void_conditional_guard(int condition) {
    int x = 0;
    condition ? (void)(x = 1) : (void)0;
    if (x) return 1;
    return 0;
}
static int left_effect_before_short_circuit(int condition) {
    int x = 0;
    (x = 1) && (condition && (x = 2));
    return x;
}
static int nested_logical(int condition, int nested_condition) {
    int x = 0;
    condition && (nested_condition && (x = 1) || (x = 2));
    return x;
}
int main(int argc, char **argv) {
    volatile int dynamic_zero = argc == 99;
    volatile int dynamic_one = argc > 0;
    int zero = dynamic_zero;
    int one = dynamic_one;
    (void)argv;
    if (ternary_value(zero) != 0) return 11;
    if (ternary_value(one) != 1) return 12;
    if (ternary_guard(zero) != 0) return 13;
    if (ternary_guard(one) != 1) return 14;
    if (logical_and_value(zero) != 0) return 21;
    if (logical_and_value(one) != 1) return 22;
    if (logical_and_guard(zero) != 0) return 23;
    if (logical_and_guard(one) != 1) return 24;
    if (logical_or_value(zero) != 1) return 31;
    if (logical_or_value(one) != 0) return 32;
    if (logical_or_guard(zero) != 1) return 33;
    if (logical_or_guard(one) != 0) return 34;
    if (ternary_equal_value(zero) != 1) return 41;
    if (ternary_equal_value(one) != 1) return 43;
    if (ternary_equal_guard(zero) != 1) return 44;
    if (ternary_equal_guard(one) != 1) return 45;
    if (gnu_omitted_guard(zero) != 1) return 46;
    if (gnu_omitted_guard(one) != 0) return 47;
    if (void_conditional_guard(zero) != 0) return 48;
    if (void_conditional_guard(one) != 1) return 49;
    if (left_effect_before_short_circuit(zero) != 1) return 50;
    if (left_effect_before_short_circuit(one) != 2) return 51;
    if (nested_logical(zero, zero) != 0) return 52;
    if (nested_logical(one, zero) != 2) return 53;
    if (nested_logical(one, one) != 1) return 54;
    return 42;
}
