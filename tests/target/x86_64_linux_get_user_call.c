/* Linux x86 exception-table checked and nocheck get_user helper ABI.
 * Assembly-only: helpers require kernel privilege and kernel fixup tables. */
register unsigned long current_stack_pointer asm("rsp");

int linux_get_user_byte(const unsigned char *user, unsigned char *out) {
    int error;
    register unsigned char value asm("%rdx");
    asm volatile("call __get_user_%c[size]"
        : "=a" (error), "=r" (value), "+r" (current_stack_pointer)
        : "0" (user), [size] "i" (sizeof(*user)));
    *out = value;
    return error;
}

int linux_get_user_half(const unsigned short *user, unsigned short *out) {
    int error;
    register unsigned short value asm("%rdx");
    asm volatile("call __get_user_%c[size]"
        : "=a" (error), "=r" (value), "+r" (current_stack_pointer)
        : "0" (user), [size] "i" (sizeof(*user)));
    *out = value;
    return error;
}

int linux_get_user_word(const unsigned int *user, unsigned int *out) {
    int error;
    register unsigned int value asm("%rdx");
    asm volatile("call __get_user_nocheck_%c[size]"
        : "=a" (error), "=r" (value), "+r" (current_stack_pointer)
        : "0" (user), [size] "i" (sizeof(*user)));
    *out = value;
    return error;
}

int linux_get_user_quad(const unsigned long *user, unsigned long *out) {
    int error;
    register unsigned long value asm("%rdx");
    asm volatile("call __get_user_nocheck_%c[size]"
        : "=a" (error), "=r" (value), "+r" (current_stack_pointer)
        : "0" (user), [size] "i" (sizeof(*user)));
    *out = value;
    return error;
}
