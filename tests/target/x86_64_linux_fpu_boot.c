/* Linux FPU discovery and interrupt/idle machine transitions. Compile and
 * GNU-assemble this fixture; NEVER execute the privileged CLI/STI/HLT
 * functions in a normal user process. The x87 status probes are a genuine
 * pair of 16-bit GNU +m output locations, not plain compiler barriers. */
typedef unsigned short minic_u16;

int minic_linux_x87_probe(minic_u16 *status, minic_u16 *control) {
    __asm__ volatile("fninit ; fnstsw %0 ; fnstcw %1"
                     : "+m"(*status), "+m"(*control));
    return *status == 0 && (*control & 0x103f) == 0x003f;
}

void minic_linux_x87_reset(void) {
    __asm__ volatile("fninit");
}

void minic_linux_native_irq_disable(void) {
    __asm__ volatile("cli" : : : "memory");
}

void minic_linux_native_irq_enable(void) {
    __asm__ volatile("sti" : : : "memory");
}

void minic_linux_native_safe_halt(void) {
    __asm__ volatile("sti; hlt" : : : "memory");
}

void minic_linux_native_halt(void) {
    __asm__ volatile("hlt");
}
