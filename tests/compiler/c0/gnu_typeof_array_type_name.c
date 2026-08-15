extern __typeof__(unsigned long[0x00001000 / sizeof(long)]) overflow_stack;
extern __typeof__(unsigned long *[4]) pointer_table;

unsigned long typeof_array_size(void) {
    return sizeof(overflow_stack);
}

unsigned long typeof_pointer_array_size(void) {
    return sizeof(pointer_table);
}
