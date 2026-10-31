/* Original Linux x86 __put_user_N helper calling convention. Compile-only;
 * it needs an in-kernel exception-fixup helper and must not run in userland. */
register unsigned long current_stack_pointer asm("rsp");

int linux_put_user_byte(unsigned char *address, unsigned char value) {
    int err;
    void *user = address;
    register unsigned char payload asm("%rax");
    payload = value;
    asm volatile("call __put_user_%c[size]"
        : "=c" (err), "+r" (current_stack_pointer)
        : "0" (user), "r" (payload), [size] "i" (sizeof(*address)) : "ebx");
    return err;
}

int linux_put_user_half(unsigned short *address, unsigned short value) {
    int err;
    void *user = address;
    register unsigned short payload asm("%rax");
    payload = value;
    asm volatile("call __put_user_%c[size]"
        : "=c" (err), "+r" (current_stack_pointer)
        : "0" (user), "r" (payload), [size] "i" (sizeof(*address)) : "ebx");
    return err;
}

int linux_put_user_word(unsigned int *address, unsigned int value) {
    int err;
    void *user = address;
    register unsigned int payload asm("%rax");
    payload = value;
    asm volatile("call __put_user_%c[size]"
        : "=c" (err), "+r" (current_stack_pointer)
        : "0" (user), "r" (payload), [size] "i" (sizeof(*address)) : "ebx");
    return err;
}

int linux_put_user_quad(unsigned long *address, unsigned long value) {
    int err;
    void *user = address;
    register unsigned long payload asm("%rax");
    payload = value;
    asm volatile("call __put_user_%c[size]"
        : "=c" (err), "+r" (current_stack_pointer)
        : "0" (user), "r" (payload), [size] "i" (sizeof(*address)) : "ebx");
    return err;
}

/* FPU signal frames and other nocheck accessors share the same kernel
 * RCX/RAX/RSP/EBX ABI but select __put_user_nocheck_N helpers. */
int linux_put_user_nocheck_half(unsigned short *address,
                               unsigned short value) {
    int err;
    void *user = address;
    register unsigned short payload asm("%rax");
    payload = value;
    asm volatile("call __put_user_nocheck_%c[size]"
        : "=c" (err), "+r" (current_stack_pointer)
        : "0" (user), "r" (payload), [size] "i" (sizeof(*address)) : "ebx");
    return err;
}

int linux_put_user_nocheck_word(unsigned int *address,
                               unsigned int value) {
    int err;
    void *user = address;
    register unsigned int payload asm("%rax");
    payload = value;
    asm volatile("call __put_user_nocheck_%c[size]"
        : "=c" (err), "+r" (current_stack_pointer)
        : "0" (user), "r" (payload), [size] "i" (sizeof(*address)) : "ebx");
    return err;
}
