/* MC-062: the GNU 'm' operand may name any addressable lvalue. The MiniC
 * machine adapter currently hardcodes __alt_reloc_selftest_addr. Compile
 * without ALT_GOOD for failing source; ALT_GOOD for positive control. */
extern void __alt_reloc_selftest(void);
#ifdef ALT_GOOD
unsigned long __alt_reloc_selftest_addr;
#define ALT_MEMORY __alt_reloc_selftest_addr
#else
unsigned long other_alt_reloc_addr;
#define ALT_MEMORY other_alt_reloc_addr
#endif
void alt_reloc_probe(void) {
    __asm__ volatile(
        "# ALT: oldnstr\n"
        "661:\n"
        "662:\n"
        "# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b))>0)*((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        ".long 661b - .\n"
        ".long 6641f - .\n"
        ".4byte (3*32+21)\n"
        ".byte 663b-661b\n"
        ".byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement,\"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n"
        "lea %[mem],%%rdi; call __alt_reloc_selftest;\n"
        "6651:\n"
        ".popsection\n"
        : : [mem] "m" (ALT_MEMORY) : "rdi");
}
