static unsigned long read_cycle_like(void) {
    return ({
        register unsigned long value;
        __asm__ __volatile__("csrr %0, " "0xc01" : "=r"(value) : : "memory");
        value;
    });
}

int main(void) {
    return 0;
}
