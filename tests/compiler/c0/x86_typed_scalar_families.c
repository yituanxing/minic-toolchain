/* Independent source-owner probes: exactly one source-qualified form per
 * migrated family. No GNU kernel headers or privileged instructions run. */
void typed_port_io(unsigned char value, unsigned short port) {
    __asm__ volatile("outb %b0, %w1" : : "a"(value), "Nd"(port));
}

void typed_atomic_rmw(unsigned int *memory, unsigned int value) {
    __asm__ volatile("addl %1,%0" : "+m"(*memory) : "ir"(value) : "memory");
}

unsigned int typed_raw_cmpxchg(unsigned int *memory, unsigned int value) {
    unsigned int old = *memory;
    __asm__ volatile("cmpxchgl %2,%1"
        : "=a"(old), "+m"(*memory) : "r"(value), "0"(old) : "memory");
    return old;
}

_Bool typed_trycas(unsigned int *memory, unsigned int value) {
    unsigned int old = *memory;
    _Bool success;
    __asm__ volatile("cmpxchgl %[new], %[ptr]\n\t/* output condition code z */"
        : "=@ccz"(success), [ptr] "+m"(*memory), "+a"(old)
        : [new] "r"(value) : "memory");
    return success;
}

_Bool typed_bitops(unsigned long *memory, unsigned long bit) {
    _Bool carry;
    __asm__ volatile("btq %2,%1\n\t/* output condition code c */"
        : "=@ccc"(carry) : "m"(*memory), "Ir"(bit) : "memory");
    return carry;
}

_Bool typed_atomic_cc(int *memory) {
    _Bool equal;
    __asm__ volatile("incl %0\n\t/* output condition code e */"
        : "+m"(*memory), "=@cce"(equal) : : "memory");
    return equal;
}

#ifdef NEGATIVE_PORT_IO
void bad_port_io(unsigned char value, unsigned short port) {
    __asm__ volatile("outb %b0, %w1; cli" : : "a"(value), "Nd"(port));
}
#endif

#ifdef NEGATIVE_ATOMIC_RMW
void bad_atomic_rmw(unsigned int *memory, unsigned int value) {
    __asm__ volatile("addl %1,%0; cli" : "+m"(*memory) : "ir"(value) : "memory");
}
#endif

#ifdef NEGATIVE_RAW_CMPXCHG
unsigned int bad_raw_cmpxchg(unsigned int *memory, unsigned int value) {
    unsigned int old = *memory;
    __asm__ volatile("cmpxchgl %2,%1; cli"
        : "=a"(old), "+m"(*memory) : "r"(value), "0"(old) : "memory");
    return old;
}
#endif

#ifdef NEGATIVE_TRYCAS
_Bool bad_trycas(unsigned int *memory, unsigned int value) {
    unsigned int old = *memory;
    _Bool success;
    __asm__ volatile("cmpxchgl %[new], %[ptr]; cli\n\t/* output condition code z */"
        : "=@ccz"(success), [ptr] "+m"(*memory), "+a"(old)
        : [new] "r"(value) : "memory");
    return success;
}
#endif

#ifdef NEGATIVE_BITOPS
_Bool bad_bitops(unsigned long *memory, unsigned long bit) {
    _Bool carry;
    __asm__ volatile("btq %2,%1; cli\n\t/* output condition code c */"
        : "=@ccc"(carry) : "m"(*memory), "Ir"(bit) : "memory");
    return carry;
}
#endif

#ifdef NEGATIVE_ATOMIC_CC
_Bool bad_atomic_cc(int *memory) {
    _Bool equal;
    __asm__ volatile("incl %0; cli\n\t/* output condition code e */"
        : "+m"(*memory), "=@cce"(equal) : : "memory");
    return equal;
}
#endif
