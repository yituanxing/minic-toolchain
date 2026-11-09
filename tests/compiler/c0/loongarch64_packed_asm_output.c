/* GNU C allows a packed field as a register-output lvalue. The GNU ASM
 * instruction itself is target-qualified; its *compiler-generated writeback*
 * must never assume the destination address is naturally aligned. */
struct __attribute__((packed)) PackedOutput {
    unsigned char tag;
    unsigned long value;
};

void loong_fpr_packed(struct PackedOutput *dst) {
    __asm__ __volatile__("movfr2gr.d %0, $f0" : "=r"(dst->value));
}

void loong_percpu_packed(struct PackedOutput *dst, void *offset) {
    __asm__ __volatile__("ldx.d %[ret], $r21, %[ptr]"
        : [ret] "=&r"(dst->value) : [ptr] "r"(offset) : "memory");
}

void loong_fpr_native(unsigned long *dst) {
    __asm__ __volatile__("movfr2gr.d %0, $f0" : "=r"(*dst));
}
