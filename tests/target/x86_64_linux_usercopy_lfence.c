/* Linux user_access_begin's original no-op -> LFENCE mitigation patch. */
int linux_user_access_lfence(int can_access) {
    if (!can_access) return 0;
    asm volatile(
        "# ALT: oldnstr\n" "661:\n\t" "" "\n662:\n"
        "# ALT: padding\n"
        ".skip -(((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")) > 0) * "
        "((" "665""1""f-""664""1""f" ")-(" "662b-661b" ")),0x90\n"
        "663" ":\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n"
        " .long " "664""1""f - .\n"
        " .4byte " "(20*32+ 2)" "\n"
        " .byte " "663""b-661b" "\n"
        " .byte " "665""1""f-""664""1""f" "\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement " "1" "\n"
        "664""1"":\n\t" "lfence" "\n" "665""1" ":\n"
        ".popsection\n" : : : "memory");
    return 1;
}
