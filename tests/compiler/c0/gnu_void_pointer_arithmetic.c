unsigned long gnu_void_pointer_add(void) {
    return (unsigned long)((void *)0x100 + 0x23UL);
}

unsigned long gnu_void_pointer_subtract(void) {
    return (unsigned long)((void *)0x180 - 0x20UL);
}

int main(void) {
    return (gnu_void_pointer_add() == 0x123UL && gnu_void_pointer_subtract() == 0x160UL) ? 0 : 1;
}

long gnu_void_pointer_difference(void *left, void *right) {
    return left - right;
}

long linux_void_pointer_difference(void *ptr, unsigned char *data) {
    return ptr - (void *)data;
}

typedef int gnu_callback_type(int value);

long gnu_function_pointer_difference(gnu_callback_type *left, gnu_callback_type *right) {
    return left - right;
}
