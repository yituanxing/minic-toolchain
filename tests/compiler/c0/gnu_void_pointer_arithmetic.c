unsigned long gnu_void_pointer_add(void) {
    return (unsigned long)((void *)0x100 + 0x23UL);
}

unsigned long gnu_void_pointer_subtract(void) {
    return (unsigned long)((void *)0x180 - 0x20UL);
}

int main(void) {
    return (gnu_void_pointer_add() == 0x123UL &&
            gnu_void_pointer_subtract() == 0x160UL)
               ? 0
               : 1;
}
