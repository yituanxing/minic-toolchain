/* The actual Linux SMAP and REP MOVSB ALTERNATIVE section grammar.
 * These kernel instructions must only be assembled/inspected, not run. */
register unsigned long current_stack_pointer asm("rsp");

#define SMAP_ALTERNATIVE(replacement) \
    "# ALT: oldnstr\n" "661:\n\t" "" "\n662:\n" \
    "# ALT: padding\n" \
    ".skip -(((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")) > 0) * " \
    "((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")),0x90\n" \
    "663" ":\n" \
    ".pushsection .altinstructions,\"a\"\n" \
    " .long 661b - .\n" \
    " .long " "664""1""f - .\n" \
    " .4byte " "( 9*32+20)" "\n" \
    " .byte " "663""b-661b" "\n" \
    " .byte " "665""1""f-""664""1""f" "\n" \
    ".popsection\n" \
    ".pushsection .altinstr_replacement, \"ax\"\n" \
    "# ALT: replacement " "1" "\n" \
    "664""1"":\n\t" replacement "\n" "665""1" ":\n" \
    ".popsection\n"

static inline void linux_clac(void) {
    asm volatile(SMAP_ALTERNATIVE(".byte 0x0f,0x01,0xca") : : : "memory");
}
static inline void linux_stac(void) {
    asm volatile(SMAP_ALTERNATIVE(".byte 0x0f,0x01,0xcb") : : : "memory");
}
unsigned long linux_smap_save(void) {
    unsigned long flags;
    asm volatile("# smap_save\n\t" SMAP_ALTERNATIVE(
        "pushf; pop %0; .byte 0x0f,0x01,0xca\n\t")
        : "=rm"(flags) : : "memory", "cc");
    return flags;
}
void linux_smap_restore(unsigned long flags) {
    asm volatile("# smap_restore\n\t" SMAP_ALTERNATIVE(
        "push %0; popf\n\t") : : "g"(flags) : "memory", "cc");
}
extern unsigned long rep_movs_alternative(void *, const void *, unsigned);
unsigned long linux_copy_user_generic(void *to, const void *from,
                                      unsigned long len) {
    linux_stac();
    asm volatile(
        "1:\n\t"
        "# ALT: oldnstr\n" "661:\n\t" "rep movsb" "\n662:\n" "# ALT: padding\n" ".skip -(((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")) > 0) * " "((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")),0x90\n" "663" ":\n" ".pushsection .altinstructions,\"a\"\n" " .long 661b - .\n" " .long " "664""1""f - .\n" " .4byte " "(((1 << 0) << 16) | ((18*32+ 4)))" "\n" " .byte " "663""b-661b" "\n" " .byte " "665""1""f-""664""1""f" "\n" ".popsection\n" ".pushsection .altinstr_replacement, \"ax\"\n" "# ALT: replacement " "1" "\n" "664""1"":\n\t" "call rep_movs_alternative" "\n" "665""1" ":\n" ".popsection\n"
        "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n" " .balign 4\n" " .long (" "1b" ") - .\n" " .long (" "2b" ") - .\n" " .long " "3" " \n" " .popsection\n"
        : "+c"(len), "+D"(to), "+S"(from), "+r"(current_stack_pointer)
        : : "memory", "rax");
    linux_clac();
    return len;
}
