/* Source-owned ASM must pass through MIR, not be dropped as a textual
 * backend special case. 'nop' exists on both GNU AArch64 and LoongArch64.
 * Empty volatile memory barriers must remain effectful but text-free. */
volatile long mir_inline_sink;

__attribute__((noinline))
long mir_inline(long value) {
    asm volatile("nop" ::: "memory");
    mir_inline_sink = value + 1;
    asm volatile("" ::: "memory");
    return mir_inline_sink;
}

int main(void) { return (int)mir_inline(41); }
