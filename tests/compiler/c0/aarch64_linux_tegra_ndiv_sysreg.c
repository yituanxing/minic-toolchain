/* Linux 6.6 drivers/cpufreq/tegra194-cpufreq.c exact GNU operand forms.
 * The proprietary CPU NDIV register supports a read and a write, and the
 * write uses the GNU "r" / %0 encoding rather than "rZ" / %x0. */
unsigned long long minic_tegra_ndiv_read(void) {
    unsigned long long value;
    asm volatile("mrs %0, s3_0_c15_c0_4" : "=r"(value) : );
    return value;
}

void minic_tegra_ndiv_write(unsigned long long value) {
    asm volatile("msr s3_0_c15_c0_4, %0" : : "r"(value));
}
