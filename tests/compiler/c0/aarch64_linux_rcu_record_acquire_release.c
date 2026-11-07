/* Original Linux barrier.h smp_load_acquire/smp_store_release GNU operands
 * can reference a 1-byte union (RCU reader-special flags) via `Q` address,
 * while the hardware LDARB/STLRB still transports an unsigned byte GPR. */
union rcu_special_flags {
    unsigned char s;
    struct { unsigned char blocked : 1, need_qs : 1, exp : 1; } bits;
};
unsigned char load_rcu_special(const union rcu_special_flags *flags) {
    unsigned char value;
    asm volatile("ldarb %w0, %1" : "=r"(value) : "Q"(*flags) : "memory");
    return value;
}
void store_rcu_special(union rcu_special_flags *flags, unsigned char value) {
    asm volatile("stlrb %w1, %0" : "=Q"(*flags) : "rZ"(value) : "memory");
}
