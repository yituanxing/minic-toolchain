/* Linux-style always-inline wrapper has the same C name as the target opcode.
 * The `i` operand is not valid in its generic function body; constant
 * callsites specialize before the Core reachability closure. */
static __inline__ __attribute__((__gnu_inline__, __always_inline__))
void invtlb(unsigned int op, unsigned int info, unsigned long address) {
    __asm__ __volatile__("invtlb %0, %1, %2\n\t" : :
        "i"(op), "r"(info), "r"(address) : "memory");
}

void emit_invtlb_constant(unsigned int info, unsigned long address) {
    invtlb(0U, info, address);
}
