/* Original Linux debugreg.h MOV DR forms: keep every legal debug register,
 * including DR7's fake memory dependency at address 0x1000. Kernel-only;
 * this fixture must be assembled/disassembled, never executed. */
typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned short u16;

u64 linux_read_dr0(void) { u64 v; asm("mov %%db0, %0" : "=r" (v)); return v; }
u64 linux_read_dr1(void) { u64 v; asm("mov %%db1, %0" : "=r" (v)); return v; }
u64 linux_read_dr2(void) { u64 v; asm("mov %%db2, %0" : "=r" (v)); return v; }
u64 linux_read_dr3(void) { u64 v; asm("mov %%db3, %0" : "=r" (v)); return v; }
u64 linux_read_dr6(void) { u64 v; asm("mov %%db6, %0" : "=r" (v)); return v; }
u64 linux_read_dr7(void) {
    u64 v;
    asm volatile("mov %%db7, %0" : "=r" (v) : "m" (*(u32 *)0x1000UL));
    return v;
}
void linux_write_dr0(u64 v) { asm("mov %0, %%db0" :: "r" (v)); }
void linux_write_dr1(u64 v) { asm("mov %0, %%db1" :: "r" (v)); }
void linux_write_dr2(u64 v) { asm("mov %0, %%db2" :: "r" (v)); }
void linux_write_dr3(u64 v) { asm("mov %0, %%db3" :: "r" (v)); }
void linux_write_dr6(u64 v) { asm("mov %0, %%db6" :: "r" (v)); }
void linux_write_dr7(u64 v) {
    asm volatile("mov %0, %%db7" :: "r" (v), "m" (*(u32 *)0x1000UL));
}

/* Long-mode segment selector snapshots and GS pointer slot writes are a
 * separate full ISA/ABI family; no CPU registers are touched at runtime. */
u16 linux_read_ss(void) {
    u16 v; asm("mov %%ss,%0" : "=r" (v) :: "memory"); return v;
}
u16 linux_read_cs(void) {
    u16 v; asm("mov %%cs,%0" : "=r" (v) :: "memory"); return v;
}
void linux_load_fs_with_fixup(u16 selector) {
    asm volatile("1: movw %0, %%fs\n"
        "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (2b) - .\n"
        " .long 5\n"
        " .popsection\n" : : "rm" (selector) : "memory");
}
void linux_load_gs_with_fixup(u16 selector) {
    asm volatile("1: movw %0, %%gs\n"
        "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (2b) - .\n"
        " .long 5\n"
        " .popsection\n" : : "rm" (selector) : "memory");
}
struct task { unsigned long flags; };
extern struct task *linux_current_task;
void linux_store_percpu_pointer(struct task *t) {
    u64 raw = (u64)t;
    asm("movq %[val], %%gs:%[var]" : [var] "+m" (linux_current_task)
        : [val] "re" (raw));
}
