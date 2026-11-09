/* The real Linux robust-list and alternate-signal-stack writers pass a
 * pointer through RAX to the private __put_user_8 helper. Their GNU asm is
 * intentionally not a normal SysV call: RCX is the user address/error and
 * RSP is an explicit +r operand. Keep the actual ABI and pointer payload. */
typedef unsigned long uintptr_t;
register uintptr_t current_stack_pointer asm("rsp");

__asm__(
    ".text\n"
    ".globl __put_user_8\n"
    "__put_user_8:\n"
    "  movq %rax, (%rcx)\n"
    "  movabsq $0x123456789abcdef0, %rbx\n"
    "  xorl %ecx, %ecx\n"
    "  ret\n"
    ".globl __put_user_nocheck_8\n"
    ".set __put_user_nocheck_8, __put_user_8\n");

static int write_robust_ptr(void **user, void *value) {
    int err;
    void *address = user;
    register void *physical_value asm("%rax") = value;
    asm volatile("call __put_user_%c[size]"
                 : "=c"(err), "+r"(current_stack_pointer)
                 : "0"(address), "r"(physical_value),
                   [size] "i"(sizeof(*user))
                 : "ebx");
    return err;
}

static int write_sigaltstack_ptr(void **user, void *value) {
    int err;
    void *address = user;
    register void *physical_value asm("%rax") = value;
    asm volatile("call __put_user_nocheck_%c[size]"
                 : "=c"(err), "+r"(current_stack_pointer)
                 : "0"(address), "r"(physical_value),
                   [size] "i"(sizeof(*user))
                 : "ebx");
    return err;
}

int main(void) {
    int first = 17;
    long second = 0x12345678L;
    void *slot_a = (void *)0;
    void *slot_b = (void *)0;
    int status_a = write_robust_ptr(&slot_a, &first);
    int status_b = write_sigaltstack_ptr(&slot_b, &second);
    return status_a == 0 && status_b == 0 &&
           slot_a == &first && slot_b == &second ? 42 : 1;
}
