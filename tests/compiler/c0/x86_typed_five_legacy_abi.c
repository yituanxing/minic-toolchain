/* Five x86 Linux Source -> Core -> Machine ASM families.  GNU and MiniC
 * both compile this .i, with all physical ISA/ELF effects still present. */
struct __attribute__((aligned(16))) X86FxSaveArea {
    unsigned char bytes[512];
};
static struct X86FxSaveArea fx32, fx64;
static unsigned long identity_source = 42UL;

static void do_fences(void) {
    __asm__ volatile("lock; addl $0,-4(%%rsp)" ::: "memory", "cc");
    __asm__ volatile("mfence" ::: "memory");
    __asm__ volatile("lfence" ::: "memory");
    __asm__ volatile("sfence" ::: "memory");
    __asm__ volatile("pause");
    __asm__ volatile("rep; nop");
    __asm__ volatile("fninit");
}

static void do_fxsave(void) {
    __asm__ volatile("fxsave %0" : "+m"(fx32));
    __asm__ volatile("fxsaveq %[fx]" : [fx] "=m"(fx64));
}

static unsigned long get_here(void) {
    unsigned long here;
    __asm__("lea 0(%%rip), %0" : "=r"(here));
    return here;
}

static unsigned int get_rights(void) {
    unsigned short selector = 0U;
    unsigned int rights;
    __asm__ volatile(
        "lar %[old_ss], %[ar]\n\t"
        "jz 1f\n\t"
        "xorl %[ar], %[ar]\n\t"
        "1:"
        : [ar] "=r"(rights) : [old_ss] "rm"(selector));
    return rights;
}

static void read_identity(void) {
    __asm__ volatile("mov %[address], %%r9" :
                     : [address] "g"(identity_source) : "r9", "memory");
}

int main(void) {
    do_fences();
    do_fxsave();
    read_identity();
    (void)get_rights();
    return get_here() != 0UL && identity_source == 42UL ? 42 : 1;
}
