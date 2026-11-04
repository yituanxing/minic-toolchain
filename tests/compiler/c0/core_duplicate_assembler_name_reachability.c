/* Two C declarations may legitimately share one GNU assembler symbol.
 * The external declaration comes first, but the later static definition
 * must still be retained when a Core CALL uses the shared symbol. */
extern int mc_duplicate_external(void) __asm__("mc_duplicate_asm");
static int mc_duplicate_local(void) __asm__("mc_duplicate_asm")
    __attribute__((unused));

static int mc_duplicate_local(void) {
    return 42;
}

/* A Module initializer edge is captured before executable Core blocks, so
 * its FunctionId must resolve all equivalent physical assembler names too. */
extern int mc_duplicate_address_external(void) __asm__("mc_duplicate_address_asm");
static int mc_duplicate_address_local(void) __asm__("mc_duplicate_address_asm")
    __attribute__((unused));
static int mc_duplicate_address_local(void) {
    return 7;
}
static int (*mc_duplicate_address_ptr)(void) = mc_duplicate_address_external;

/* Source GNU asm operands form a third ingress path, independent of a Core
 * CALL and of the Module global initializer graph. Its symbol is the physical
 * assembler spelling even when the source expression names an extern C id. */
extern int mc_duplicate_asm_root_external(void) __asm__("mc_duplicate_asm_root");
static int mc_duplicate_asm_root_local(void) __asm__("mc_duplicate_asm_root")
    __attribute__((unused));
static int mc_duplicate_asm_root_local(void) {
    return 9;
}

/* Deliberately uncalled: the source operand is collected before Core
 * reachability and preserves the symbol. This architecture-specific GNU
 * asm operand is enabled only for MiniC's five-ISA -S oracle; the native
 * executable checks the CALL and global initializer relocations instead. */
#ifdef MINIC_TEST_ASM_ROOT
static void mc_duplicate_asm_operand_anchor(void) __attribute__((unused));
static void mc_duplicate_asm_operand_anchor(void) {
    __asm__ volatile(".pushsection .rodata\n\t"
                     ".quad %0\n\t"
                     ".popsection" : : "i"(mc_duplicate_asm_root_external));
}
#endif

int main(void) {
    return mc_duplicate_external() == 42 && mc_duplicate_address_ptr() == 7
        ? 0 : 1;
}
