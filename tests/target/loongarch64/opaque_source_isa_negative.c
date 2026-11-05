/* The legacy qualified-ASM contract rejects this unsealed two-instruction
 * payload. Typed MachineIR must reject it and leave no partial .s. */
void loong_opaque_unqualified_program(void) {
    __asm__ __volatile__("idle 1; break 0");
}
