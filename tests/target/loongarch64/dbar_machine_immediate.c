/* The GNU `I` operand is sealed at the source boundary and becomes a typed
 * MachineIR immediate. It is not interpreted as an arbitrary ASM string. */
void minic_loong_dbar_18(void) {
    __asm__ __volatile__("dbar %0 " : : "I"(0x12) : "memory");
}

void minic_loong_dbar_1792(void) {
    __asm__ __volatile__("dbar %0" : : "I"(0x700) : "memory");
}

#if defined(MINIC_BAD_DBAR_SUFFIX)
int main(void) {
    __asm__ __volatile__("dbar %0; syscall" : : "I"(18) : "memory");
    return 0;
}
#elif defined(MINIC_BAD_DBAR_CONSTRAINT)
int main(void) {
    __asm__ __volatile__("dbar %0" : : "r"(18) : "memory");
    return 0;
}
#elif defined(MINIC_BAD_DBAR_DYNAMIC)
int main(int value) {
    __asm__ __volatile__("dbar %0" : : "I"(value) : "memory");
    return 0;
}
#endif
