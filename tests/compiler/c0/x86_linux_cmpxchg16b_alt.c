/* Original Linux 6.6 x86 per-CPU CMPXCHG16B alternative: the emulation
 * branch executes a private kernel call, while the replacement uses GS.
 * Assemble/inspect the ELF patch metadata, never execute this in user mode. */
typedef unsigned __int128 u128;
typedef unsigned long u64;
union halves { u128 full; struct { u64 low, high; }; };

void __update_cpu_freelist_fast(volatile u128 *per_cpu,
                                u128 *oldp, u128 desired) {
    union halves old = {.full = *oldp};
    union halves next = {.full = desired};
    asm volatile("# ALT: oldnstr\n"
                 "661:\n\tleaq %P[var], %%rsi; call this_cpu_cmpxchg16b_emu\n"
                 "662:\n"
                 "# ALT: padding\n"
                 ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
                     "((6651f-6641f)-(662b-661b)),0x90\n"
                 "663:\n"
                 ".pushsection .altinstructions,\"a\"\n"
                 " .long 661b - .\n"
                 " .long 6641f - .\n"
                 " .4byte ( 4*32+13)\n"
                 " .byte 663b-661b\n"
                 " .byte 6651f-6641f\n"
                 ".popsection\n"
                 ".pushsection .altinstr_replacement, \"ax\"\n"
                 "# ALT: replacement 1\n"
                 "6641:\n\tcmpxchg16b %%gs:%[var]\n"
                 "6651:\n.popsection\n"
                 : [var] "+m"(*per_cpu), "+a"(old.low), "+d"(old.high)
                 : "b"(next.low), "c"(next.high)
                 : "memory", "rsi");
    *oldp = old.full;
}
