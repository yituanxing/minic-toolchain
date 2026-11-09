/* Linux 6.6.143 arch/x86/include/asm/pvclock.h and vDSO syscall fallback.
 * All templates and GNU operand constraints are kept at their original
 * spelling. The syscall paths are built, never executed by this probe. */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned long ulong;

struct vdso_test_timespec {
    long tv_sec;
    long tv_nsec;
};

static u64 pvclock_scale_delta(u64 delta, u32 mul_frac, int shift) {
    u64 product;
    ulong tmp;
    if (shift < 0) delta >>= -shift;
    else delta <<= shift;
    __asm__("mulq %[mul_frac] ; shrd $32, %[hi], %[lo]"
        : [lo]"=a"(product), [hi]"=d"(tmp)
        : "0"(delta), [mul_frac]"rm"((u64)mul_frac));
    return product;
}

static u64 pvclock_scale_numeric(u64 delta, u64 mul_frac) {
    u64 product;
    ulong tmp;
    __asm__("mulq %3 ; shrd $32, %1, %0"
        : "=a"(product), "=d"(tmp) : "0"(delta), "rm"(mul_frac));
    return product;
}

long vdso_clock_gettime_fallback(int clockid, struct vdso_test_timespec *ts) {
    long ret;
    __asm__("syscall" : "=a"(ret), "=m"(*ts) :
        "0"(228), "D"(clockid), "S"(ts) : "rcx", "r11");
    return ret;
}

long vdso_gettimeofday_fallback(struct vdso_test_timespec *tv, void *tz) {
    long ret;
    __asm__("syscall" : "=a"(ret) :
        "0"(96), "D"(tv), "S"(tz) : "memory");
    return ret;
}

int main(void) {
    struct vdso_test_timespec ts = {0,0};
    return pvclock_scale_delta(1ULL << 32,0x40000000U,0) != 0x40000000ULL ||
           pvclock_scale_numeric(1ULL << 32,0x40000000ULL) != 0x40000000ULL ||
           vdso_clock_gettime_fallback(0,&ts) != 0 || ts.tv_sec <= 0 ||
           vdso_gettimeofday_fallback(&ts,0) != 0 || ts.tv_sec <= 0;
}
