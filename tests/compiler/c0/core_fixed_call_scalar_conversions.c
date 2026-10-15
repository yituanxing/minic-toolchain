static unsigned int core_m3_echo_unsigned(unsigned int value) {
    return value;
}

static const volatile void *core_m3_echo_qualified(const volatile void *value) {
    return value;
}

static const void *core_m3_echo_pointer(const void *value) {
    return value;
}

static _Bool core_m3_echo_bool(_Bool value) {
    return value;
}

static _Bool core_m3_kasan_check(const volatile void *address, unsigned int size) {
    return 1;
}

unsigned int core_m3_integer_conversion(void) {
    return core_m3_echo_unsigned(-1);
}

const volatile void *core_m3_pointer_qualification(const void *value) {
    return core_m3_echo_qualified(value);
}

const void *core_m3_null_pointer(void) {
    return core_m3_echo_pointer(0);
}

_Bool core_m3_pointer_bool(const void *value) {
    return core_m3_echo_bool(value);
}

unsigned long core_m3_read_word_at_a_time(const void *address) {
    core_m3_kasan_check(address, 1);
    return *(unsigned long *)address;
}
