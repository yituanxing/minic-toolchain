static unsigned long read_cycle_like(void) {
    return ({
        register unsigned long value;
        __asm__ __volatile__("csrr %0, " "0xc01" : "=r"(value) : : "memory");
        value;
    });
}

int main(void) {
    /* Keep the register-output producer reachable: dead static functions are
     * intentionally pruned by Core module reachability. */
    return (int)read_cycle_like();
}
