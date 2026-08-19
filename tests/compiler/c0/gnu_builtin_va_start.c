typedef __builtin_va_list va_list;

static int probe_va_start(int fixed, ...) {
    va_list args;
    __builtin_va_start(args, fixed);
    if (!args)
        return 1;
    __builtin_va_end(args);
    return 0;
}

int main(void) {
    return probe_va_start(7, 11, 13);
}
