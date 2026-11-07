/* Original Linux debugreg.h source forms. Assemble only; privileged ISA. */
typedef unsigned long u64;
typedef unsigned int u32;

u64 dr0_read(void) { u64 v; asm("mov %%db0, %0" : "=r"(v)); return v; }
u64 dr1_read(void) { u64 v; asm("mov %%db1, %0" : "=r"(v)); return v; }
u64 dr2_read(void) { u64 v; asm("mov %%db2, %0" : "=r"(v)); return v; }
u64 dr3_read(void) { u64 v; asm("mov %%db3, %0" : "=r"(v)); return v; }
u64 dr6_read(void) { u64 v; asm("mov %%db6, %0" : "=r"(v)); return v; }
u64 dr7_read(void) { u64 v; asm volatile("mov %%db7, %0" : "=r"(v) : "m"(*(u32 *)0x1000UL)); return v; }

void dr0_write(u64 v) { asm("mov %0, %%db0" :: "r"(v)); }
void dr1_write(u64 v) { asm("mov %0, %%db1" :: "r"(v)); }
void dr2_write(u64 v) { asm("mov %0, %%db2" :: "r"(v)); }
void dr3_write(u64 v) { asm("mov %0, %%db3" :: "r"(v)); }
void dr6_write(u64 v) { asm("mov %0, %%db6" :: "r"(v)); }
void dr7_write(u64 v) { asm volatile("mov %0, %%db7" :: "r"(v), "m"(*(u32 *)0x1000UL)); }
