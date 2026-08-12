void __attribute__((weak)) calibration_delay_done(void);
void optional_hook(void) __attribute__((__weak__));
void later_weak(void);
void __attribute__((weak)) later_weak(void);

int __attribute__((weak)) weak_definition(void) {
    return 7;
}

int strong_definition(void) {
    return 9;
}

void invoke_hooks(void) {
    calibration_delay_done();
    optional_hook();
    later_weak();
}
