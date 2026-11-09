/* The decimal tail is outside binary64 but inside x87 extended precision.
 * The final long binary128 literal also exercises the shared 113-bit path
 * on four non-x86 targets even though this harness executes on x86 only. */
_Static_assert(sizeof(long double) == 16,
               "supported Linux target long double uses 16-byte storage");

int main(void) {
    volatile long double decimal_tail = 1.0000000000000001L;
    volatile long double hex_ulp = 0x1.0000000000000002p0L;
    volatile long double hex_half_ulp = 0x1.0000000000000001p0L;
    volatile long double x87_min_subnormal = 0x1p-16445L;
    volatile long double quad_tail = 1.000000000000000000000000000000001L;
    volatile double hex_double = 0x1.8p+1;
    volatile float hex_float = 0x1.2p+2f;
    volatile double hex_leading_dot = 0x.8p+2;
    (void)quad_tail;
    /* Equality isolates literal precision from the separate existing x87
     * relational-helper ordering bug (even 2.0L > 1.0L currently fails). */
    if (decimal_tail == 1.0L) return 1;
    if (hex_ulp == 1.0L) return 2;
    if (hex_half_ulp != 1.0L) return 3;
    if (x87_min_subnormal == 0.0L) return 4;
    if (hex_double != 3.0 || hex_float != 4.5f ||
        hex_leading_dot != 2.0) return 5;
    return 42;
}
