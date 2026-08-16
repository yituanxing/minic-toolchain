struct CoreDiffLayout {
    char prefix;
    int value;
};

int core_diff_math(int x, int y) {
    return -(x + y);
}

int core_diff_branch(int x) {
    if (x) {
        return 7;
    }
    return -3;
}

int core_diff_zero(int x) {
    return !x;
}

int core_diff_ninth(int a0,
                    int a1,
                    int a2,
                    int a3,
                    int a4,
                    int a5,
                    int a6,
                    int a7,
                    int a8) {
    return a8;
}

int core_diff_pointer_zero(int *value) {
    return !value;
}

int core_diff_call_target(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

int core_diff_call(int value) {
    return core_diff_call_target(value, 2, 3, 4, 5, 6);
}

int *core_diff_field(struct CoreDiffLayout *item) {
    return &item->value;
}

int core_diff_pointer_call_target(int *value) {
    return !value;
}

int core_diff_field_call(struct CoreDiffLayout *item) {
    return core_diff_pointer_call_target(&item->value);
}

void core_diff_call_nop(void) {
    return;
}

int core_diff_void_call(int value) {
    core_diff_call_nop();
    return -value;
}

static int core_diff_static(int value) {
    return -value;
}

int __attribute__((weak)) core_diff_weak(int value) {
    return value + 1;
}

__attribute__((visibility("hidden"))) int core_diff_hidden(int value) {
    return !value;
}

int __attribute__((section(".core.diff.text"))) core_diff_section(int value) {
    return value;
}

int core_diff_asm(int value) __asm__("core_diff_asm_alias");
int core_diff_asm(int value) {
    return value;
}

int core_diff_asm_call(int value) {
    return core_diff_asm(value);
}
