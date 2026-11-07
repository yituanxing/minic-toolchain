/* GNU output-bearing asm is an observable value producer even when the
 * source statement itself is not volatile. */
int core_asm_nonvolatile_rk_output(void) {
    int result;
    __asm__("addi %0, zero, %1" : "=r"(result) : "rK"(0));
    return result;
}
