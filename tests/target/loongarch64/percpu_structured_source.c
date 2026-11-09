/* Original Linux LoongArch hardware-source family through production MIR.
 * The kernel owns r21 and its privileged context; this fixture is compiled
 * to GNU ASM only, never executed in user mode. */
struct __attribute__((packed)) PerCpuPackedResult {
    unsigned char prefix;
    unsigned long old;
};

unsigned long percpu_read_b(void *p) {
    unsigned long old;
    __asm__ __volatile__("ldx.b %[ret], $r21, %[ptr]"
        : [ret] "=&r"(old) : [ptr] "r"(p) : "memory");
    return old;
}
unsigned long percpu_read_h(void *p) {
    unsigned long old;
    __asm__ __volatile__("ldx.h %[ret], $r21, %[ptr]"
        : [ret] "=&r"(old) : [ptr] "r"(p) : "memory");
    return old;
}
unsigned long percpu_read_w(void *p) {
    unsigned long old;
    __asm__ __volatile__("ldx.w %[ret], $r21, %[ptr]"
        : [ret] "=&r"(old) : [ptr] "r"(p) : "memory");
    return old;
}
unsigned long percpu_read_d(void *p) {
    unsigned long old;
    __asm__ __volatile__("ldx.d %[ret], $r21, %[ptr]"
        : [ret] "=&r"(old) : [ptr] "r"(p) : "memory");
    return old;
}

void percpu_write_b(void *p, unsigned long v) {
    __asm__ __volatile__("stx.b %[val], $r21, %[ptr]" : :
        [val] "r"(v), [ptr] "r"(p) : "memory");
}
void percpu_write_h(void *p, unsigned long v) {
    __asm__ __volatile__("stx.h %[val], $r21, %[ptr]" : :
        [val] "r"(v), [ptr] "r"(p) : "memory");
}
void percpu_write_w(void *p, unsigned long v) {
    __asm__ __volatile__("stx.w %[val], $r21, %[ptr]" : :
        [val] "r"(v), [ptr] "r"(p) : "memory");
}
void percpu_write_d(void *p, unsigned long v) {
    __asm__ __volatile__("stx.d %[val], $r21, %[ptr]" : :
        [val] "r"(v), [ptr] "r"(p) : "memory");
}

unsigned long percpu_amo_add_w(unsigned int *p, unsigned long v) {
    unsigned long old;
    __asm__ __volatile__("amadd.w %[ret], %[val], %[ptr]"
        : [ret] "=&r"(old), [ptr] "+ZB"(*p) : [val] "r"(v));
    return old;
}
unsigned long percpu_amo_add_d(unsigned long *p, unsigned long v) {
    unsigned long old;
    __asm__ __volatile__("amadd.d %[ret], %[val], %[ptr]"
        : [ret] "=&r"(old), [ptr] "+ZB"(*p) : [val] "r"(v));
    return old;
}
unsigned long percpu_amo_and_w(unsigned int *p, unsigned long v) {
    unsigned long old;
    __asm__ __volatile__("amand.w %[ret], %[val], %[ptr]"
        : [ret] "=&r"(old), [ptr] "+ZB"(*p) : [val] "r"(v));
    return old;
}
unsigned long percpu_amo_and_d(unsigned long *p, unsigned long v) {
    unsigned long old;
    __asm__ __volatile__("amand.d %[ret], %[val], %[ptr]"
        : [ret] "=&r"(old), [ptr] "+ZB"(*p) : [val] "r"(v));
    return old;
}
unsigned long percpu_amo_or_w(unsigned int *p, unsigned long v) {
    unsigned long old;
    __asm__ __volatile__("amor.w %[ret], %[val], %[ptr]"
        : [ret] "=&r"(old), [ptr] "+ZB"(*p) : [val] "r"(v));
    return old;
}
unsigned long percpu_amo_or_d(unsigned long *p, unsigned long v) {
    unsigned long old;
    __asm__ __volatile__("amor.d %[ret], %[val], %[ptr]"
        : [ret] "=&r"(old), [ptr] "+ZB"(*p) : [val] "r"(v));
    return old;
}

void percpu_reset_fcsr(void) {
    __asm__ __volatile__("movgr2fcsr $fcsr0, %0" : : "r"(0));
}

void percpu_packed_read(struct PerCpuPackedResult *result, void *p) {
    __asm__ __volatile__("ldx.d %[ret], $r21, %[ptr]"
        : [ret] "=&r"(result->old) : [ptr] "r"(p) : "memory");
}
void percpu_packed_amo(struct PerCpuPackedResult *result,
    unsigned long *p, unsigned long v) {
    __asm__ __volatile__("amadd.d %[ret], %[val], %[ptr]"
        : [ret] "=&r"(result->old), [ptr] "+ZB"(*p) : [val] "r"(v));
}
