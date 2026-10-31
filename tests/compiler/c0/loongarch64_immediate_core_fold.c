/* A single-use Core INTEGER_CONSTANT is a machine-immediate operand only
 * when the LoongArch selector can encode it. Tests also exercise a fallback
 * constant, a dynamic RHS, and a volatile LOAD->ALU sequence. */
static volatile long minic_immediate_volatile = 20;

__attribute__((noinline)) long minic_immediate_add(long value)
{
    return value + 7L;
}

__attribute__((noinline)) long minic_immediate_sub(long value)
{
    return value - 2048L;
}

__attribute__((noinline)) long minic_immediate_mask(long value)
{
    return (value & 4095L) ^ 7L;
}

__attribute__((noinline)) long minic_immediate_out_of_range(long value)
{
    return value + 2048L;
}

__attribute__((noinline)) long minic_immediate_dynamic(long left, long right)
{
    return left + right;
}

__attribute__((noinline)) long minic_immediate_load_chain(void)
{
    return minic_immediate_volatile + 7L;
}

__attribute__((noinline)) long minic_immediate_barrier(void)
{
    long value = minic_immediate_volatile;
    __asm__ __volatile__("" ::: "memory");
    return value + 7L;
}

int main(void)
{
    if (minic_immediate_add(13) != 20) return 1;
    if (minic_immediate_sub(2057) != 9) return 2;
    if (minic_immediate_mask(4101) != 2) return 3;
    if (minic_immediate_out_of_range(13) != 2061) return 4;
    if (minic_immediate_dynamic(13, 7) != 20) return 5;
    if (minic_immediate_load_chain() != 27) return 6;
    if (minic_immediate_barrier() != 27) return 7;
    return 0;
}
