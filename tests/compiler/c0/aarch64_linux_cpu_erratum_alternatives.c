/* Exact Linux 6.6 preprocessed GNU inline assembler from
 * arch/arm64/mm/context.c post_ttbr_update_workaround() and
 * arch/arm64/kernel/proton-pack.c spectre_v4_enable_hw_mitigation().
 * A boot-time alternative is an executable pair plus 12B patch metadata. */

void minic_post_ttbr_update_workaround(void) {
    asm(".if ""1"" == 1\n"
        "661:\n\t" "nop; nop; nop" "\n"
        "662:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .word 661b - .\n"
        " .word 663f - .\n"
        " .hword " "86" "\n"
        " .byte 662b-661b\n"
        " .byte 664f-663f\n"
        ".popsection\n"
        ".subsection 1\n"
        "663:\n\t" "ic iallu; dsb nsh; isb" "\n"
        "664:\n\t"
        ".org\t. - (664b-663b) + (662b-661b)\n\t"
        ".org\t. - (662b-661b) + (664b-663b)\n\t"
        ".previous\n"
        ".endif\n");
}

void minic_spectre_v4_enable_hw_mitigation(void) {
    asm volatile(".if ""1"" == 1\n"
        "661:\n\t" "dsb nsh\nisb\n" "\n"
        "662:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .word 661b - .\n"
        " .word 663f - .\n"
        " .hword " "42" "\n"
        " .byte 662b-661b\n"
        " .byte 664f-663f\n"
        ".popsection\n"
        ".subsection 1\n"
        "663:\n\t"
        ".inst " "(0xd5000000 | (((0) << 19) | ((3) << 16) | ((3) << 12) | (((0)) << 8) | (((7)) << 5)) | ((31) & 0x1f))" "\n\t"
        "nop\n" "\n"
        "664:\n\t"
        ".org\t. - (664b-663b) + (662b-661b)\n\t"
        ".org\t. - (662b-661b) + (664b-663b)\n\t"
        ".previous\n"
        ".endif\n");
}
