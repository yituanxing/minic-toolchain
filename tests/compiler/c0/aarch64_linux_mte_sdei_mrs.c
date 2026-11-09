/* Linux 6.6 MTE's uppercase CNTVCT_EL0 and SDEI's VBAR_EL1 reads.
 * Both are privileged architectural MRS instructions, not plain loads. */
#define READ_REG(NAME)                                              \
unsigned long long minic_read_##NAME(void) {                         \
    unsigned long long value;                                       \
    asm volatile("mrs %0, " #NAME : "=r" (value));                 \
    return value;                                                   \
}

READ_REG(CNTVCT_EL0)
READ_REG(vbar_el1)
