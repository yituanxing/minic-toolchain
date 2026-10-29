/* x86-64 Linux head64 startup selectors: inspect -S / GAS only.
 * The operation changes DS, SS and ES and is not safe in user mode. */
void minic_test_linux_boot_segments(void) {
    asm volatile("movl %%eax, %%ds\n"
                 "movl %%eax, %%ss\n"
                 "movl %%eax, %%es\n"
                 : : "a"(3 * 8) : "memory");
}
