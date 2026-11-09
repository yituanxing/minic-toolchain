int main(void) {
    extern const void __start_notes __attribute__((weak));
    extern const void __stop_notes __attribute__((weak));
    const void *first = &__start_notes;
    const void *last = &__stop_notes;

    return first == last ? 0 : 1;
}
