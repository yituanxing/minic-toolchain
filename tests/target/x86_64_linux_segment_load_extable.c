/* Linux 6.6.143 loadsegment(): exact GNU extended ASM plus exception entry.
 * Kernel-only selector writes; assemble, never execute in userspace. */
typedef unsigned short u16;

#define LOADSEG(name, segment) \
void name(u16 selector) { \
    asm volatile("1: movw %0, %%" #segment "\n" \
        "2:\n" \
        " .pushsection \"__ex_table\",\"a\"\n" \
        " .balign 4\n" \
        " .long (1b) - .\n" \
        " .long (2b) - .\n" \
        " .long 5\n" \
        " .popsection\n" : : "rm"(selector) : "memory"); \
}

LOADSEG(load_fs,fs)
LOADSEG(load_gs,gs)
LOADSEG(load_ds,ds)
LOADSEG(load_es,es)
LOADSEG(load_ss,ss)
