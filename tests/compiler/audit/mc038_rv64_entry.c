/* Freestanding Linux RV64 entry for the mixed-TU aggregate oracle. */
extern int main(void);

__attribute__((noreturn)) void _start(void) {
    register long status __asm__("a0") = (long)main();
    register long syscall __asm__("a7") = 93L;
    __asm__ volatile("ecall" : : "r"(status), "r"(syscall) : "memory");
    __builtin_unreachable();
}
