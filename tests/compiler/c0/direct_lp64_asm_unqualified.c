/* The capacity fix must not weaken the target ASM allowlist. */
int direct_lp64_asm_unqualified(void) {
    __asm__ __volatile__("not_a_supported_machine_opcode");
    return 0;
}
