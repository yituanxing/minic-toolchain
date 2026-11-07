/* A register-or-small-immediate operand on an empty GNU template is still
 * a compiler ordering effect. It must not be stolen by nonempty ASM baking. */
void core_asm_empty_rk_immediate(void) {
    __asm__ __volatile__("" : : "rK"(0));
}
