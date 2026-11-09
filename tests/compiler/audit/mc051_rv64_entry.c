/* Freestanding Linux RV64 entry for the MC-051 mixed-TU QEMU probe. */
extern int main(void);

__attribute__((noreturn)) void _start(void) {
    register long status __asm__("a0") = (long)main();
    register long syscall __asm__("a7") = 93L; /* Linux exit */
    __asm__ volatile("ecall" : : "r"(status), "r"(syscall) : "memory");
    __builtin_unreachable();
}
