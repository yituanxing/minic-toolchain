unsigned long core_m23b_const_parameter(const unsigned long value) {
    return value + 1UL;
}

unsigned long core_m23b_const_pointer_parameter(int *const pointer) {
    return (unsigned long)(*pointer);
}
