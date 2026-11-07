/* Linux 6.6 MDS buffer-clear and WBINVD cache-flush hardware effects.
 * Never execute on an unprivileged host. */
void linux_clear_cpu_buffers(void) {
    static const unsigned short ds = 3 * 8;
    asm volatile("verw %[ds]" : : [ds] "m" (ds) : "cc");
}

void linux_flush_cpu_caches(void) {
    asm volatile("wbinvd" : : : "memory");
}

void linux_swap_kernel_gs_base(void) {
    asm volatile("swapgs" : : : "memory");
}
