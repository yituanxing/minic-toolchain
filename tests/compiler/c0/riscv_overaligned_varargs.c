/* MC-014 / MC-015: a GNU variadic callee can realign its local frame to
 * 64 bytes, but the caller only guarantees 16-byte entry-SP alignment.
 * The saved a-registers and the caller's stack arguments form ONE linear
 * va_list regardless of the 0/16/32/48-byte entry-SP alignment phase. */
struct Aligned64 {
    unsigned long words[8];
} __attribute__((aligned(64)));

__attribute__((noinline))
unsigned long va_frame_one(unsigned long tag, ...) {
    struct Aligned64 storage;
    __builtin_va_list ap;
    unsigned long total = 0;
    unsigned int i;
    storage.words[0] = tag;
    total += (unsigned long)&storage & 63UL;
    __builtin_va_start(ap, tag);
    for (i = 0; i < 9U; ++i)
        total += __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    return total + storage.words[0];
}

__attribute__((noinline))
unsigned long va_frame_seven(
    unsigned long a0, unsigned long a1, unsigned long a2,
    unsigned long a3, unsigned long a4, unsigned long a5,
    unsigned long a6, ...) {
    struct Aligned64 storage;
    __builtin_va_list ap;
    unsigned long total;
    storage.words[0] = a0 + a1 + a2 + a3 + a4 + a5 + a6;
    total = (unsigned long)&storage & 63UL;
    __builtin_va_start(ap, a6);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    return total + storage.words[0];
}

__attribute__((noinline))
unsigned long va_frame_eight(
    unsigned long a0, unsigned long a1, unsigned long a2,
    unsigned long a3, unsigned long a4, unsigned long a5,
    unsigned long a6, unsigned long a7, ...) {
    struct Aligned64 storage;
    __builtin_va_list ap;
    unsigned long total;
    storage.words[0] = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
    total = (unsigned long)&storage & 63UL;
    __builtin_va_start(ap, a7);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    return total + storage.words[0];
}

/* The first unnamed word is one caller-stack slot after the ninth named
 * parameter. RV32 must not accidentally use E or realigned S here. */
__attribute__((noinline))
unsigned long va_frame_nine(
    unsigned long a0, unsigned long a1, unsigned long a2,
    unsigned long a3, unsigned long a4, unsigned long a5,
    unsigned long a6, unsigned long a7, unsigned long a8, ...) {
    struct Aligned64 storage;
    __builtin_va_list ap;
    unsigned long total;
    storage.words[0] = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
    total = (unsigned long)&storage & 63UL;
    __builtin_va_start(ap, a8);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    return total + storage.words[0];
}

/* A moving alloca-SP must not change the entry-SP ABI anchor either. */
__attribute__((noinline))
unsigned long va_frame_dynamic(unsigned long tag, unsigned long count, ...) {
    struct Aligned64 storage;
    unsigned char *scratch = __builtin_alloca(count + 16UL);
    __builtin_va_list ap;
    unsigned long total;
    scratch[0] = (unsigned char)count;
    storage.words[0] = tag + scratch[0];
    total = (unsigned long)&storage & 63UL;
    __builtin_va_start(ap, count);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    total += __builtin_va_arg(ap, unsigned long);
    __builtin_va_end(ap);
    return total + storage.words[0];
}
