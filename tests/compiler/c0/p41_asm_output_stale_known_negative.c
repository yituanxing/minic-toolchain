/* P41: an output constraint writes an ordinary C local, so a prior Core
 * value fact cannot satisfy a later GNU `i` immediate constraint. */
int p41_asm_output_stale_known(void) {
    int x = 1;
    __asm__ volatile("addi %0, zero, 2" : "=r"(x));
    __asm__ volatile(".byte %c0" : : "i"(x));
    return x;
}
