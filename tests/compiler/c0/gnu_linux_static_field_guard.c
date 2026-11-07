/* GNU Linux bitfield.h-style diagnostics inside an ordinary TU-local static
 * function.  Its mask is a formal parameter, but every live caller supplies
 * a compile-time constant.  MiniC must specialize the call without a GCC C
 * fallback and must not synthesize definitions for diagnostic externs. */
extern void invalid_field_mask(void)
    __attribute__((error("field mask must be constant and contiguous")));

static unsigned long read_field(unsigned long reg, unsigned long mask)
{
    unsigned long shifted;
    if (!__builtin_constant_p(mask))
        invalid_field_mask();
    if (mask == 0UL)
        invalid_field_mask();
    shifted = mask >> __builtin_ctzl(mask);
    if ((shifted & (shifted + 1UL)) != 0UL)
        invalid_field_mask();
    return (reg & mask) >> __builtin_ctzl(mask);
}

static unsigned long use_two_fields(unsigned long reg)
{
    return read_field(reg, 0x00000070UL) +
           read_field(reg, 0x0000f000UL);
}

int main(void)
{
#if defined(TEST_DYNAMIC_FIELD_GUARD)
    volatile unsigned long dynamic_mask = 0x70UL;
    return (int)read_field(0x15d40UL, dynamic_mask);
#elif defined(TEST_INVALID_FIELD_GUARD)
    return (int)read_field(0x15d40UL, 0x000000a0UL);
#else
    return use_two_fields(0x15d40UL) != 9UL;
#endif
}
