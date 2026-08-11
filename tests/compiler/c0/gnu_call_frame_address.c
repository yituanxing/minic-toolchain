extern int clobber_ra(int value);

void *capture_return_after_call(int value) {
    clobber_ra(value);
    return __builtin_return_address(0);
}

void *capture_frame_address(void) {
    return __builtin_frame_address(0);
}

unsigned long linux_return_address_shape(void) {
    return (unsigned long)__builtin_return_address(0);
}
