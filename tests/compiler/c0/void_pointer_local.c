static int is_null(void *pointer) {
    return pointer == (void *)0;
}

int main(void) {
    void *pointer = (void *)0;
    const void *constant_pointer = pointer;
    return is_null(pointer) && constant_pointer == (void *)0 ? 0 : 1;
}
