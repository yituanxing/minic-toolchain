/* Linux's complete feature-gated STAC/CLAC records, compile-only. */
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

void linux_owner_clac(void) {
    asm volatile(SMAP_ALTERNATIVE(".byte 0x0f,0x01,0xca") : : : "memory");
}

void linux_owner_stac(void) {
    asm volatile(SMAP_ALTERNATIVE(".byte 0x0f,0x01,0xcb") : : : "memory");
}
