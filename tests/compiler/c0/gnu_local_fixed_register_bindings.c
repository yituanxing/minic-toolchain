long local_fixed_syscall_like(long first, long second) {
    register long a0v asm("a0") = first;
    register long a1v asm("a1") = second;
    register long result asm("a0");
    register long nr asm("a7") = 169;

    asm volatile("add %0, %1, %2 # nr=%3" : "=r"(result) : "r"(a0v), "r"(a1v), "r"(nr) : "memory");
    return result;
}

void *local_fixed_pointer_like(void *input) {
    register void *a0v asm("a0") = input;
    register void *result asm("a0");

    asm volatile("mv %0, %1" : "=r"(result) : "r"(a0v) : "memory");
    return result;
}
