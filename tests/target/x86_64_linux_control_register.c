/* Linux control registers: machine instruction, never a dummy C load. */
typedef unsigned long u64;
typedef unsigned int u32;

static u64 read_cr0(void) {
    u64 value;
    __asm__ volatile("mov %%cr0,%0\n\t" : "=r"(value) : "m"(*(u32 *)0x1000UL));
    return value;
}
static u64 read_cr2(void) {
    u64 value;
    __asm__ volatile("mov %%cr2,%0\n\t" : "=r"(value) : "m"(*(u32 *)0x1000UL));
    return value;
}
static u64 read_cr3(void) {
    u64 value;
    __asm__ volatile("mov %%cr3,%0\n\t" : "=r"(value) : "m"(*(u32 *)0x1000UL));
    return value;
}
static u64 read_cr4(void) {
    u64 value;
    __asm__ volatile("mov %%cr4,%0\n\t" : "=r"(value) : "m"(*(u32 *)0x1000UL));
    return value;
}
static void write_cr2(u64 value) {
    __asm__ volatile("mov %0,%%cr2" : : "r"(value) : "memory");
}
static void write_cr3(u64 value) {
    __asm__ volatile("mov %0,%%cr3" : : "r"(value) : "memory");
}

u64 minic_linux_cr_read_all(void) {
    return read_cr0() ^ read_cr2() ^ read_cr3() ^ read_cr4();
}

void minic_linux_cr_write_pair(u64 a, u64 b) {
    write_cr2(a);
    write_cr3(b);
}
