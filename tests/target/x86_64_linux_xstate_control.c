/* Exact Linux XCR0/XSS access contract. The two operations are privileged
 * machine effects, so this fixture is for ELF relocation/disassembly only. */
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

uint64_t minic_linux_read_xstate(uint32_t selector) {
    uint32_t low, high;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(selector));
    return low + ((uint64_t)high << 32);
}

void minic_linux_write_xstate(uint32_t selector, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("xsetbv" : : "a"(low), "d"(high), "c"(selector));
}

uint64_t minic_linux_xstate_roundtrip(uint32_t selector, uint64_t value) {
    minic_linux_write_xstate(selector, value);
    return minic_linux_read_xstate(selector);
}
