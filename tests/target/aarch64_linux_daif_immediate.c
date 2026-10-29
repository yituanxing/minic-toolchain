/* Exact DAIFSet/DAIFClr instruction forms. Kernel EL1 only; do not execute
 * this test in a user-mode emulator. The compiler must keep all four writes. */
void minic_linux_daif_immediates(void) {
    __asm__ volatile("msr daifset, #3" : : : "memory");
    __asm__ volatile("msr daifclr, #3" : : : "memory");
    __asm__ volatile("msr\tdaifset, #0xf\t\t// local_daif_mask\n"
                     : : : "memory");
    __asm__ volatile("msr daifclr, #0xf" : : : "memory");
}
