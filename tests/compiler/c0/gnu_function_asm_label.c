extern int renamed_api(int value) __asm__("" "__real_renamed_api");

int call_renamed_api(void) {
    return renamed_api(7);
}

int has_renamed_api_address(void) {
    return &renamed_api != 0;
}
