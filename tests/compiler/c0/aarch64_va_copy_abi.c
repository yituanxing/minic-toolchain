/* GNU↔MiniC AAPCS64 ABI oracle. No libc dependency beyond the CRT:
 * both callers and callees may receive the real 32B state by reference. */
typedef __builtin_va_list va_list;

static int probe_nonvariadic_copy(va_list incoming) {
    va_list independent;
    int before, first, after, second;
    __builtin_va_copy(independent, incoming);
    before = __builtin_va_arg(incoming, int);
    first = __builtin_va_arg(independent, int);
    after = __builtin_va_arg(incoming, int);
    second = __builtin_va_arg(independent, int);
    __builtin_va_end(independent);
    return before == 17 && first == 17 && after == 19 && second == 19;
}

static int probe_variadic_copy(int fixed, ...) {
    va_list original, independent;
    int a, b, c, d;
    __builtin_va_start(original, fixed);
    __builtin_va_copy(independent, original);
    a = __builtin_va_arg(original, int);
    b = __builtin_va_arg(independent, int);
    c = __builtin_va_arg(original, int);
    d = __builtin_va_arg(independent, int);
    __builtin_va_end(independent);
    __builtin_va_end(original);
    return fixed == 7 && a == 17 && b == 17 && c == 19 && d == 19;
}

static int probe_passed_copy(int fixed, ...) {
    va_list original;
    int good;
    __builtin_va_start(original, fixed);
    good = fixed == 7 && probe_nonvariadic_copy(original);
    __builtin_va_end(original);
    return good;
}

int main(void) {
    return probe_variadic_copy(7, 17, 19) &&
           probe_passed_copy(7, 17, 19) ? 42 : 1;
}
