/* GNU LoongArch records locally synthesized `. -` anchors with STV_HIDDEN.
 * Both 32- and 64-bit widths must preserve the relocation-symbol visibility. */
.section .debug_info,"",@progbits
  .long unresolved_target - .
  .quad unresolved_target - .
