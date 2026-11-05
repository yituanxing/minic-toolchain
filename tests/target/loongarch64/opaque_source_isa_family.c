/* Original LoongArch Linux zero-operand source-ASM forms. In MachineIR they
 * must retain the source-owned template, exact volatile/barrier effect and
 * qualified native opcode; no generic multi-instruction ASM is admitted. */
void loong_opaque_idle(void) { __asm__ __volatile__("idle 0"); }
void loong_opaque_tlbrd(void) { __asm__ __volatile__("tlbrd"); }
void loong_opaque_tlbclr(void) { __asm__ __volatile__("tlbclr"); }
void loong_opaque_tlbflush(void) { __asm__ __volatile__("tlbflush"); }
void loong_opaque_tlbsrch(void) { __asm__ __volatile__("tlbsrch"); }
void loong_opaque_tlbwr(void) { __asm__ __volatile__("tlbwr"); }
void loong_opaque_tlbfill(void) { __asm__ __volatile__("tlbfill"); }
void loong_opaque_barrier(void) {
    __asm__ __volatile__("dbar 0" ::: "memory");
    __asm__ __volatile__("dbar 0x700" ::: "memory");
    __asm__ __volatile__("dbar 0\n\t" ::: "memory");
    __asm__ __volatile__("\tibar 0\n");
    __asm__ __volatile__("ibar 0 \t\ndbar 0 \t\n" ::: "memory");
}
void loong_opaque_hotplug(void) {
    __asm__ __volatile__("nop\n\t");
    __asm__ __volatile__("idle 0\n\t");
}
