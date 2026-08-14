_Static_assert(__builtin_types_compatible_p(__typeof__(__builtin_unreachable()), void),
               "__builtin_unreachable must have void type");

void linux_bug_shape(int condition) {
    if (condition) {
        asm volatile("");
        __builtin_unreachable();
    }
}

int ordinary_path(int value) {
    if (value)
        return value;
    __builtin_unreachable();
}
