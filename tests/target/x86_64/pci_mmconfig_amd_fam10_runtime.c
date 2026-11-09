/* Linux 6.6 x86 PCI MMCONFIG's six original AMD Fam10h hardware forms.
 * GNU's address input is `r`, never the ordinary `m` MMIO constraint;
 * hardware must access memory through AL/AX/EAX, not a generic GPR. */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

u8 mmio_config_readb(void *pos) {
    u8 val;
    __asm__ volatile("movb (%1),%%al" : "=a"(val) : "r"(pos));
    return val;
}

u16 mmio_config_readw(void *pos) {
    u16 val;
    __asm__ volatile("movw (%1),%%ax" : "=a"(val) : "r"(pos));
    return val;
}

u32 mmio_config_readl(void *pos) {
    u32 val;
    __asm__ volatile("movl (%1),%%eax" : "=a"(val) : "r"(pos));
    return val;
}

void mmio_config_writeb(void *pos, u8 val) {
    __asm__ volatile("movb %%al,(%1)" : : "a"(val), "r"(pos) : "memory");
}

void mmio_config_writew(void *pos, u16 val) {
    __asm__ volatile("movw %%ax,(%1)" : : "a"(val), "r"(pos) : "memory");
}

void mmio_config_writel(void *pos, u32 val) {
    __asm__ volatile("movl %%eax,(%1)" : : "a"(val), "r"(pos) : "memory");
}

int main(void) {
    u32 memory[3] = {0U, 0U, 0U};
    mmio_config_writeb(&memory[0], 0x5aU);
    mmio_config_writew(&memory[1], 0x3355U);
    mmio_config_writel(&memory[2], 0x87654321U);
    if (mmio_config_readb(&memory[0]) != 0x5aU) return 1;
    if (mmio_config_readw(&memory[1]) != 0x3355U) return 2;
    if (mmio_config_readl(&memory[2]) != 0x87654321U) return 3;
    return 0;
}
