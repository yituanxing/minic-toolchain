/* Exact Linux 6.6 arch/loongarch/vdso/vgetcpu.i CPU-ID clock form.
 * Unlike the nonvolatile VDSO base form, this source has one memory clobber. */
static __inline__ __attribute__((__gnu_inline__, __always_inline__))
int linux_read_cpu_id(void) {
    int cpu_id;
    __asm__ __volatile__("rdtime.d $zero, %0\n"
                         : "=r"(cpu_id) : : "memory");
    return cpu_id;
}

int linux_cpu_id(void) { return linux_read_cpu_id(); }
