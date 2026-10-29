/* GNU extended asm must not inherit basic asm's implicit volatile flag. */
int extended_plain(int value) {
    int result;
    __asm__("" : "=r"(result) : "r"(value));
    return result;
}

int extended_explicit_volatile(int value) {
    int result;
    __asm__ volatile("" : "=r"(result) : "r"(value));
    return result;
}
