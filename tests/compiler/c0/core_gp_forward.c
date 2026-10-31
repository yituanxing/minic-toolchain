long minic_gp_chain(long a, long b, long c, long d, long e) {
    return (((a + b) * c) ^ d) + e;
}

long minic_gp_anti_chain(long a, long b, long c, long d) {
    long x = a + b;
    long y = x * c;
    return (y ^ d) + x;
}

static volatile long observed;

__attribute__((noinline))
static long minic_gp_callback(long v) {
    observed += v;
    return observed - 3;
}

/* A singly consumed Core value is not eligible when a real CALL, a
 * compiler ASM barrier or a CFG edge intervenes between its operations. */
long minic_gp_call_barrier(long input) {
    long value = input + 11;
    long from_call = minic_gp_callback(input);
    return value + from_call;
}

long minic_gp_asm_barrier(long input) {
    long value = input + 11;
    __asm__ __volatile__("" ::: "memory");
    return value * 3;
}

long minic_gp_cfg_boundary(long input) {
    long value = input + 11;
    if (input < 0)
        return value * 3;
    return value - 3;
}

/* The same Core definition is consumed twice and must remain in its frame
 * slot. Counting unique user instructions instead of operand uses is wrong. */
long minic_gp_double_use(long input) {
    long value = input + 11;
    return value + value;
}

long minic_gp_volatile_single_store(long *out) {
    *out = observed;
    return 19;
}

long minic_gp_volatile(long *out) {
    long x = observed;
    *out = x;
    return x;
}

/* This exercises the LoongArch adjacent scalar-conversion path: preserving
 * the low 32 bits is not enough when a later consumer expects ld.w's signed
 * register representation, even if the final C result is unsigned. */
unsigned long minic_gp_convert_u32(unsigned long value) {
    unsigned int narrowed = (unsigned int)value;
    return (unsigned long)narrowed;
}

long minic_gp_convert_signed(long value) {
    int narrowed = (int)value;
    return (long)narrowed;
}

int minic_gp_zero_after_convert(unsigned long value) {
    unsigned int narrowed = (unsigned int)value;
    return narrowed == 0U;
}

int main(void) {
    long value = 0;
    observed = 17;
    if (minic_gp_chain(7, 3, 5, 12, 9) != 71 ||
        minic_gp_anti_chain(7, 3, 5, 12) != 72 ||
        minic_gp_call_barrier(6) != 37 || observed != 23 ||
        minic_gp_asm_barrier(6) != 51 ||
        minic_gp_cfg_boundary(-4) != 21 ||
        minic_gp_cfg_boundary(6) != 14 ||
        minic_gp_double_use(6) != 34 ||
        minic_gp_volatile_single_store(&value) != 19 || value != 23)
        return 1;
    observed = 17;
    if (minic_gp_volatile(&value) != 17 || value != 17 ||
        minic_gp_convert_u32(0x1ffffffffUL) != 0xffffffffUL ||
        minic_gp_convert_signed(0x1ffffffffL) != -1L ||
        minic_gp_zero_after_convert(0x100000000UL) != 1 ||
        minic_gp_zero_after_convert(1UL) != 0)
        return 1;
    return 42;
}
